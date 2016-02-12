import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import socket
import re
import logging
from cfgm_common import jsonutils as json
from netaddr import IPAddress, IPNetwork
import argparse
from cStringIO import StringIO
import cgitb

import kazoo.client
import kazoo.exceptions
import cfgm_common
from cfgm_common.utils import cgitb_hook
from cfgm_common.ifmap.client import client
from cfgm_common.ifmap.request import NewSessionRequest
from cfgm_common.ifmap.response import newSessionResult
from cfgm_common.imid import ifmap_read_all, parse_search_result
import pycassa
import utils

import vnc_cfg_ifmap
from schema_transformer.db import SchemaTransformerDB
from discovery import disc_cassdb
from svc_monitor import db as svc_monitor_db

MAX_COL = 10000000
class DatabaseChecker(object):
    def __init__(self, args_str):
        self._parse_args(args_str)

        self._logger = utils.ColorLog(logging.getLogger(__name__))
        log_level = 'ERROR'
        if self._args.verbose:
            log_level = 'INFO'
        if self._args.debug:
            log_level = 'DEBUG'
        self._logger.setLevel(log_level)
        logformat = logging.Formatter("%(levelname)s: %(message)s")
        stdout = logging.StreamHandler()
        stdout.setLevel(log_level)
        stdout.setFormatter(logformat)
        self._logger.addHandler(stdout)

        # cassandra connection
        self._cassandra_servers = self._api_args.cassandra_server_list
        db_info = vnc_cfg_ifmap.VncServerCassandraClient.get_db_info()
        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._cf_dict = {}
        for ks_name, cf_name_list in db_info:
            pool = pycassa.ConnectionPool(keyspace=ks_name,
                       server_list=self._cassandra_servers, prefill=False)
            for cf_name in cf_name_list:
                self._cf_dict[cf_name] = pycassa.ColumnFamily(pool, cf_name,
                                         read_consistency_level=rd_consistency)

        # ifmap connection
        self._connect_to_ifmap_servers()
    # end __init__

    def _parse_args(self, args_str):
        parser = argparse.ArgumentParser()

        help="Path to contrail-api conf file, default /etc/contrail-api.conf"
        parser.add_argument(
            "--api-conf", help=help, default="/etc/contrail/contrail-api.conf")
        parser.add_argument(
            "--verbose", help="Run in verbose/INFO mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--debug", help="Run in debug mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--ifmap-servers",
            help="List of ifmap-ip:ifmap-port, default from api-conf")
        parser.add_argument(
            "--ifmap-credentials",
            help="<username>:<password> for read-only user",
            required=True)

        args_obj, remaining_argv = parser.parse_known_args(args_str.split())
        self._args = args_obj

        self._api_args = utils.parse_args('-c %s %s'
            %(self._args.api_conf, ' '.join(remaining_argv)))[0]
    # end _parse_args

    def checker(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                ok, msg = func(*args, **kwargs)
                if ok:
                    self._logger.info('Checker %s: Success' %(func.__name__))
                else:
                    self._logger.error('Checker %s: Failed, %s' %(func.__name__, msg))

                return ok, msg
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Checker %s: Exception, %s' %(func.__name__, err_msg))

        # end wrapper

        return wrapper
    # end checker

    @checker
    def check_zk_mode_and_node_count(self):
        ret_ok = True
        ret_msg = ''
        stats = {}
        modes = {}
        modes['leader'] = 0
        modes['follower'] = 0
        modes['standalone'] = 0
        # Collect stats
        for server in self._api_args.zk_server_ip.split(','):
            zk_client = kazoo.client.KazooClient(server)
            zk_client.start()
            self._logger.debug("Issuing 'stat' on %s: ", server)
            stat_out = zk_client.command('stat')
            self._logger.debug("Got: %s" %(stat_out))
            zk_client.stop()
            stats[server] = stat_out

        # Check mode
        for stat_out in stats.values():
            mode = re.search('Mode:(.*)\n', stat_out).group(1).strip()
            modes[mode] += 1
        n_zk_servers = len(self._api_args.zk_server_ip.split(','))
        if n_zk_servers == 1:
            # good-case: 1 node in standalone
            if not modes['standalone'] == 1:
                ret_ok = False
                ret_msg += "Error, Single zookeeper node and modes %s. " \
                           %(str(modes))
        else:
            # good-case: 1 node in leader, >=1 in followers
            if (modes['leader'] == 1) and (modes['follower'] >= 1):
                pass # ok
            else:
                ret_ok = False
                ret_msg += "Error, Incorrect modes %s. " %(str(modes))

        # Check node count
        node_counts = []
        for stat_out in stats.values():
            nc = int(re.search('Node count:(.*)\n', stat_out).group(1))
            node_counts.append(nc)
        # all nodes should have same count, so set should have 1 elem
        if len(set(node_counts)) != 1:
            ret_ok = False
            ret_msg += "Error, Differing node counts %s. " %(str(node_counts))

        return ret_ok, ret_msg
    # end check_zk_mode_and_node_count

    @checker
    def check_cassandra_keyspace_replication(self):
        ret_ok = True
        ret_msg = ''
        logger = self._logger
        for server in self._cassandra_servers:
            sys_mgr = pycassa.SystemManager(server)
            db_info = vnc_cfg_ifmap.VncCassandraClient.get_db_info() + \
                      SchemaTransformerDB.get_db_info() + \
                      disc_cassdb.DiscoveryCassandraClient.get_db_info() + \
                      svc_monitor_db.ServiceMonitorDB.get_db_info()
            for ks_name, _ in db_info:
                logger.debug("Reading keyspace properties for %s on %s: ",
                             ks_name, server)
                ks_prop = sys_mgr.get_keyspace_properties(ks_name)
                logger.debug("Got %s", ks_prop)

                repl_factor = int(ks_prop['strategy_options']['replication_factor'])
                if (repl_factor != len(self._cassandra_servers)):
                    errmsg = 'Incorrect replication factor %d for keyspace %s' \
                             %(repl_factor, ks_name)
                    ret_msg += "Error, %s. " %(errmsg)

        return ret_ok, ret_msg
    # end check_cassandra_keyspace_replication

    @checker
    def check_rabbitmq_queue(self):
        ret_ok = True
        ret_msg = ''

        return ret_ok, ret_msg
    # end check_rabbitmq_queue

    @checker
    def check_fq_name_uuid_ifmap_match(self):
        # ensure items in obj-fq-name-table match to
        # a. obj-uuid-table, b. local ifmap server
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        fq_name_table_all = []
        logger.debug("Reading all objects from obj_fq_name_table")
        for obj_type, cols in obj_fq_name_table.get_range(column_count=MAX_COL):
            for fq_name_str_uuid in cols:
                fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
                fq_name_str = cfgm_common.utils.decode_string(fq_name_str)
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                fq_name_table_all.append((obj_type, fq_name_str, obj_uuid))
                try:
                    obj_cols = obj_uuid_table.get(obj_uuid, column_count=MAX_COL)
                    obj_fq_name_str = ':'.join(json.loads(obj_cols['fq_name']))
                    if fq_name_str != obj_fq_name_str:
                        ret_ok = False
                        ret_msg += 'Error, Mismatched FQ Name %s vs %s. ' \
                                   %(fq_name_str, obj_fq_name_str)
                except pycassa.NotFoundException:
                    ret_ok = False
                    ret_msg += 'Error, Missing object %s %s %s in uuid table. '\
                                   %(obj_uuid, obj_type, fq_name_str)
            # end for all objects in a type
        # end for all obj types
        logger.debug("Got %d objects", len(fq_name_table_all))

        uuid_table_all = []
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            obj_type = json.loads(cols['type'])
            fq_name_str = ':'.join(json.loads(cols['fq_name']))
            uuid_table_all.append((obj_type, fq_name_str, obj_uuid))

        logger.debug("Got %d objects", len(uuid_table_all))

        for extra in set(fq_name_table_all) - set(uuid_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_ok = False
            ret_msg += 'Error, Extra object %s %s %s in obj_fq_name_table. ' \
                       %(obj_type, fq_name_str, obj_uuid)

        for extra in set(uuid_table_all) - set(fq_name_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_ok = False
            ret_msg += 'Error, Extra object %s %s %s in obj_uuid_table. ' \
                       %(obj_type, fq_name_str, obj_uuid)

        for mapclient in self._mapclients:
            search_results = parse_search_result(ifmap_read_all(mapclient))
            # parse_search_result returns in form of
            # [ ({'config-root': 'root'}, <Element metadata at 0x1e19b48>)
            # ({'config-root': 'root',
            #   'global-system-config': 'default-global-system-config'},
            #   <Element metadata at 0x1e19680>) ]
            # convert it into set of identifiers
            all_ifmap_idents = []
            for ident_s, meta in search_results:
                all_ifmap_idents.extend(ident_s.values())
            all_ifmap_idents = set(all_ifmap_idents)
            all_cassandra_idents = set([item[1] for item in fq_name_table_all])
            logger.debug("Got %d idents from %s server",
                len(all_ifmap_idents), mapclient._client__url)

            extra = all_ifmap_idents - all_cassandra_idents
            if (len(extra) == 1) and (extra == set(['root'])):
                pass # good
            else:
                ret_ok = False
                ret_msg += 'Error, Extra identifiers %s in ifmap %s vs obj_fq_name_table. ' \
                       %(extra, mapclient._client__url)
            extra = all_cassandra_idents - all_ifmap_idents
            if extra:
                ret_ok = False
                ret_msg += 'Error, Missing identifiers %s in ifmap %s vs obj_fq_name_table. ' \
                       %(extra, mapclient._client__url)


        return ret_ok, ret_msg
    # end check_fq_name_uuid_ifmap_match

    @checker
    def check_obj_mandatory_fields(self):
        # ensure fq_name, type, uuid etc. exist
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        logger.debug("Reading all objects from obj_uuid_table")
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        num_objs = 0
        num_bad_objs = 0
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            num_objs += 1
            reqd_col_names = ['type', 'fq_name', 'prop:id_perms']
            for col_name in reqd_col_names:
                if col_name in cols:
                    continue
                num_bad_objs += 1
                ret_ok = False
                ret_msg += 'Error, obj %s missing column %s. ' \
                           %(obj_uuid, col_name)

        logger.debug("Got %d objects %d with missing mandatory fields",
                     num_objs, num_bad_objs)

        return ret_ok, ret_msg
    # end check_obj_mandatory_fields

    @checker
    def check_subnet_uuid(self):
        # whether useragent subnet uuid and uuid in subnet property match
        ret_ok = True
        ret_msg = ''

        # check in useragent table whether net-id subnet -> subnet-uuid
        # and vice-versa exist for all subnets
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        ua_all_subnet_uuids = []
        for key, cols in ua_kv_cf.get_range():
            mch = re.match('(.* .*/.*)', key)
            if mch: # subnet key -> uuid
                subnet_key = mch.group(1)
                subnet_id = cols['value']
                try:
                    rev_map = ua_kv_cf.get(subnet_id)
                except pycassa.NotFoundException:
                    ret_ok = False
                    errmsg = "Mismatch no subnet id %s for %s in useragent. " \
                             %(subnet_id, subnet_key)
                    ret_msg += "Error, %s" %(errmsg)
            else: # uuid -> subnet key
                subnet_id = key
                subnet_key = cols['value']
                ua_all_subnet_uuids.append(subnet_id)
                try:
                    rev_map = ua_kv_cf.get(subnet_key)
                except pycassa.NotFoundException:
                    ret_ok = False
                    errmsg = "Mismatch no subnet key %s for %s in useragent. " \
                             %(subnet_id, subnet_key)
                    ret_msg += "Error, %s" %(errmsg)

        # check all subnet prop in obj_uuid_table to see if subnet-uuid exists
        vnc_all_subnet_uuids = []
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        vn_row = fq_name_table.get('virtual_network', column_count=MAX_COL)
        vn_uuids = [x.split(':')[-1] for x in vn_row]
        for vn_id in vn_uuids:
            try:
                ipam_refs = obj_uuid_table.get(vn_id,
                    column_start='ref:network_ipam:',
                    column_finish='ref:network_ipam;',
                    column_count=MAX_COL)
            except pycassa.NotFoundException:
                continue

            for ipam in ipam_refs:
                ipam_col = obj_uuid_table.get(vn_id,
                    columns=[ipam], column_count=MAX_COL)
                attr_dict = json.loads(ipam_col[ipam])['attr']
                for subnet in attr_dict['ipam_subnets']:
                    try:
                        sn_id = subnet['subnet_uuid']
                        vnc_all_subnet_uuids.append(sn_id)
                    except KeyError:
                        ret_ok = False
                        errmsg = "Missing subnet uuid in vnc for %s %s" \
                                 %(vn_id, subnet['subnet']['ip_prefix'])
                        ret_msg += "Error, %s" %(errmsg)

        # check #subnets in useragent table vs #subnets in obj_uuid_table
        if len(ua_all_subnet_uuids) != len(vnc_all_subnet_uuids):
            ret_ok = False
            ret_msg += "Error, Mismatch #subnets usergent %d #subnets vnc %d. " \
                       %(len(ua_all_subnet_uuids), len(vnc_all_subnet_uuids))

        # check if subnet-uuids match in useragent table vs obj_uuid_table
        extra_ua_subnets = set(ua_all_subnet_uuids) - set(vnc_all_subnet_uuids)
        if extra_ua_subnets:
            ret_ok = False
            ret_msg += "Error, Extra useragent subnets %s. " \
                       %(str(extra_ua_subnets))
        extra_vnc_subnets = set(vnc_all_subnet_uuids) - set(ua_all_subnet_uuids)
        if extra_vnc_subnets:
            ret_ok = False
            ret_msg += "Error, Extra vnc subnets %s. " \
                       %(str(extra_vnc_subnets))

        return ret_ok, ret_msg
    # end check_subnet_uuid

    @checker
    def check_subnet_addr_alloc(self):
        # whether ip allocated in subnet in zk match iip+fip in cassandra
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        zk_server = self._api_args.zk_server_ip.split(',')[0]
        zk_client = kazoo.client.KazooClient(zk_server)
        zk_client.start()
        zk_all_vns = {}
        base_path = '/api-server/subnets'
        logger.debug("Doing recursive read from %s from %s",
                     base_path, zk_server)
        num_addrs = 0
        subnets = []
        try:
            subnets = zk_client.get_children(base_path)
        except kazoo.exceptions.NoNodeError:
            pass
        for subnet in subnets:
            vn_fq_name_str = ':'.join(subnet.split(':')[:-1])
            pfx = subnet.split(':')[-1]
            zk_all_vns[vn_fq_name_str] = {}
            pfxlen_path = base_path + '/' + subnet
            pfxlen = zk_client.get_children(pfxlen_path)
            if not pfxlen:
                continue
            subnet_key = '%s/%s' %(pfx, pfxlen[0])
            zk_all_vns[vn_fq_name_str][subnet_key] = []
            addrs = zk_client.get_children(pfxlen_path+'/'+pfxlen[0])
            if not addrs:
                continue
            zk_all_vns[vn_fq_name_str][subnet_key] = \
                [str(IPAddress(int(addr))) for addr in addrs]
            num_addrs += len(zk_all_vns[vn_fq_name_str])
        # end for all subnet paths
        zk_client.stop()
        logger.debug("Got %d networks %d addresses",
                     len(zk_all_vns), num_addrs)

        logger.debug("Reading instance/floating-ip objects from cassandra")
        cassandra_all_vns = {}
        num_addrs = 0
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        try:
            iip_row = fq_name_table.get('instance_ip', column_count=MAX_COL)
        except pycassa.NotFoundException:
            iip_row = []
        iip_uuids = [x.split(':')[-1] for x in iip_row]
        ok, msg = self._addr_alloc_process_ip_objects(cassandra_all_vns,
                                                      'instance-ip', iip_uuids)
        if not ok:
            ret_ok = False
            ret_msg += msg

        num_addrs += len(iip_uuids)

        try:
            fip_row = fq_name_table.get('floating_ip', column_count=MAX_COL)
        except pycassa.NotFoundException:
            fip_row = []

        fip_uuids = [x.split(':')[-1] for x in fip_row]
        ok, msg = self._addr_alloc_process_ip_objects(cassandra_all_vns,
                  'floating-ip', fip_uuids)
        if not ok:
            ret_ok = False
            ret_msg += msg

        num_addrs += len(fip_uuids)

        logger.debug("Got %d networks %d addresses", 
                     len(cassandra_all_vns), num_addrs)

        # check for differences in networks
        extra_vn = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        if extra_vn:
            ret_ok = False
            errmsg = 'Extra VN in zookeeper for %s' %(str(extra_vn))
            ret_msg += 'Error, %s. ' %(errmsg)

        extra_vn = set(cassandra_all_vns.keys()) - set(zk_all_vns.keys())
        if extra_vn:
            ret_ok = False
            errmsg = 'Extra VN in cassandra for %s' %(str(extra_vn))
            ret_msg += 'Error, %s. ' %(errmsg)

        # check for differences in subnets
        zk_all_vn_sn = []
        for vn_key, vn in zk_all_vns.items():
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        cassandra_all_vn_sn = []
        for vn_key, vn in cassandra_all_vns.items():
            cassandra_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        extra_vn_sn = set(zk_all_vn_sn) - set(cassandra_all_vn_sn)
        if extra_vn_sn:
            ret_ok = False
            errmsg = 'Extra VN/SN in zookeeper for %s' %(str(extra_vn_sn))
            ret_msg += 'Error, %s. ' %(errmsg)

        extra_vn_sn = set(cassandra_all_vn_sn) - set(zk_all_vn_sn)
        if extra_vn_sn:
            ret_ok = False
            errmsg = 'Extra VN/SN in cassandra for %s' %(str(extra_vn_sn))
            ret_msg += 'Error, %s. ' %(errmsg)

        # check for differences in ip addresses
        for vn, sn_key in set(zk_all_vn_sn) & set(cassandra_all_vn_sn):
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']
            extra_ips = set(zk_ips) - set(cassandra_ips)
            for ip_addr in extra_ips:
                # ignore network and gateway ips
                if IPAddress(ip_addr) == IPNetwork(sn_key).network:
                    continue
                if ip_addr == sn_gw_ip:
                    continue

                ret_ok = False
                errmsg = 'Extra IPs in zookeeper for vn %s %s' \
                    %(vn, str(extra_ips))
                ret_msg += 'Error, %s. ' %(errmsg)
            # end all zk extra ips

            extra_ips = set(cassandra_ips) - set(zk_ips)
            for ip_addr in extra_ips:
                ret_ok = False
                errmsg = 'Extra IPs in cassandra for vn %s %s' \
                    %(vn, str(extra_ips))
                ret_msg += 'Error, %s. ' %(errmsg)
            # end all cassandra extra ips

            # check gateway ip present/reserved in zookeeper
            if sn_gw_ip not in zk_ips:
                ret_ok = False
                errmsg = 'Gateway ip not reserved in zookeeper %s %s' \
                    %(vn, str(extra_ips))
                ret_msg += 'Error, %s. ' %(errmsg)
        # for all common VN/subnets

        return ret_ok, ret_msg
    # end check_subnet_addr_alloc

    @checker
    def check_route_targets_id(self):
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        # read in route-target ids from zookeeper
        zk_server = self._api_args.zk_server_ip.split(',')[0]
        zk_client = kazoo.client.KazooClient(zk_server)
        zk_client.start()
        base_path = '/id/bgp/route-targets'
        logger.debug("Doing recursive read from %s from %s",
                     base_path, zk_server)
        zk_all_rtgts = {}
        auto_rtgt_start = 8000000
        num_bad_rtgts = 0 
        for rtgt in zk_client.get_children(base_path) or []:
            ri_fq_name_str = zk_client.get(base_path+'/'+rtgt)[0]
            zk_all_rtgts[rtgt] = ri_fq_name_str
            if int(rtgt) >= auto_rtgt_start:
                continue # all good

            ret_ok = False
            errmsg = 'Wrong route-target range in zookeeper %s' %(rtgt)
            ret_msg += 'Error, %s. ' %(errmsg)
            num_bad_rtgts += 1

        logger.debug("Got %d route-targets, %d of them from wrong range",
                     len(zk_all_rtgts), num_bad_rtgts)

        # read in virtual-networks from cassandra to find user allocated rtgts
        # and ensure they are not from auto-allocated range
        num_user_rtgts = 0
        num_bad_rtgts = 0
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading virtual-network objects from cassandra")
        try:
            vn_row = fq_name_table.get('virtual_network', column_count=MAX_COL)
        except pycassa.NotFoundException:
            vn_row = []
        vn_uuids = [x.split(':')[-1] for x in vn_row]
        for vn_id in vn_uuids:
            try:
                rtgt_list_json = obj_uuid_table.get(vn_id,
                    columns=['prop:route_target_list'],
                    column_count=MAX_COL)['prop:route_target_list']
                rtgt_list = json.loads(rtgt_list_json).get('route_target', [])
            except pycassa.NotFoundException:
                continue

            for rtgt in rtgt_list:
                rtgt_num = int(rtgt.split(':')[-1])
                if rtgt_num < auto_rtgt_start:
                    num_user_rtgts += 1
                    continue # all good

                num_bad_rtgts += 1
                ret_ok = False
                errmsg = 'Wrong route-target range in cassandra %d' %(rtgt_num)
                ret_msg += 'Error, %s. ' %(errmsg)
            # end for all rtgt
        # end for all vns

        logger.debug("Got %d user configured route-targets, %d in bad range",
                     num_user_rtgts, num_bad_rtgts)

        # read in route-target objects from cassandra and ensure their count
        # matches user-defined + auto allocated and also match RI names(TODO)
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading route-target objects from cassandra")
        try:
            rtgt_row = fq_name_table.get('route_target', column_count=MAX_COL)
        except pycassa.NotFoundException:
            rtgt_row = []
        rtgt_uuids = [x.split(':')[-1] for x in rtgt_row]
        logger.debug("Got %d route-target objects from cassandra",
                     len(rtgt_uuids))

        if len(rtgt_uuids) != (num_user_rtgts + len(zk_all_rtgts)):
            ret_ok = False
            errmsg = 'Mismatch in route-target count, %d user %d auto, cassandra has %d' \
                     %(num_user_rtgts, len(zk_all_rtgts), len(rtgt_uuids))
            ret_msg += 'Error, %s. ' %(errmsg)

        return ret_ok, ret_msg
    # end check_route_targets_id

    @checker
    def check_virtual_networks_id(self):
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        # read in virtual-network ids from zookeeper
        zk_server = self._api_args.zk_server_ip.split(',')[0]
        zk_client = kazoo.client.KazooClient(zk_server)
        zk_client.start()
        base_path = '/id/virtual-networks'
        logger.debug("Doing recursive read from %s from %s",
                     base_path, zk_server)
        zk_all_vns = {}
        for vn_id in zk_client.get_children(base_path) or []:
            vn_fq_name_str = zk_client.get(base_path+'/'+vn_id)[0]
            # VN-id in zk starts from 0, in cassandra starts from 1
            zk_all_vns[int(vn_id)+1] = vn_fq_name_str

        logger.debug("Got %d virtual-networks with id", len(zk_all_vns))

        # read in virtual-networks from cassandra to get id+fq_name
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading virtual-network objects from cassandra")
        try:
            vn_row = fq_name_table.get('virtual_network', column_count=MAX_COL)
        except pycassa.NotFoundException:
            vn_row = []
        vn_uuids = [x.split(':')[-1] for x in vn_row]
        cassandra_all_vns = {}
        for vn_uuid in vn_uuids:
            vn_cols = obj_uuid_table.get(vn_uuid,
                columns=['prop:virtual_network_properties', 'fq_name'],
                column_count=MAX_COL)
            vn_props = json.loads(vn_cols['prop:virtual_network_properties'])
            vn_id = vn_props['network_id']
            vn_fq_name_str = ':'.join(json.loads(vn_cols['fq_name']))
            cassandra_all_vns[vn_id] = vn_fq_name_str

        logger.debug("Got %d virtual-networks with id", len(cassandra_all_vns))

        zk_set = set([(id, fqns) for id, fqns in zk_all_vns.items()])
        cassandra_set = set([(id, fqns) for id, fqns in cassandra_all_vns.items()])
        extra_vn_ids = zk_set - cassandra_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            ret_ok = False
            errmsg = 'Extra VN IDs in zookeeper for vn %s %s' \
                    %(vn_fq_name_str, vn_id)
            ret_msg += 'Error, %s. ' %(errmsg)

        extra_vn_ids = cassandra_set - zk_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            ret_ok = False
            errmsg = 'Extra VN IDs in cassandra for vn %s %s' \
                    %(vn_fq_name_str, vn_id)
            ret_msg += 'Error, %s. ' %(errmsg)

        return ret_ok, ret_msg
    # end check_virtual_networks_id

    @checker
    def check_security_groups_id(self):
        ret_ok = True
        ret_msg = ''
        logger = self._logger

        # read in virtual-network ids from zookeeper
        zk_server = self._api_args.zk_server_ip.split(',')[0]
        zk_client = kazoo.client.KazooClient(zk_server)
        zk_client.start()
        base_path = '/id/security-groups/id'
        logger.debug("Doing recursive read from %s from %s",
                     base_path, zk_server)
        zk_all_sgs = {}
        for sg_id in zk_client.get_children(base_path) or []:
            # sg-id of 0 is reserved
            if int(sg_id) == 0:
                sg_val = zk_client.get(base_path+'/'+sg_id)[0]
                if sg_val != '__reserved__':
                    ret_ok = False
                    ret_msg += 'Error, SG-ID 0 not reserved %s. ' %(sg_val)
                continue
                
            sg_fq_name_str = zk_client.get(base_path+'/'+sg_id)[0]
            zk_all_sgs[int(sg_id)] = sg_fq_name_str

        logger.debug("Got %d security-groups with id", len(zk_all_sgs))

        # read in security-groups from cassandra to get id+fq_name
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading security-group objects from cassandra")
        try:
            sg_row = fq_name_table.get('security_group', column_count=MAX_COL)
        except pycassa.NotFoundException:
            sg_row = []
        sg_uuids = [x.split(':')[-1] for x in sg_row]
        cassandra_all_sgs = {}
        for sg_uuid in sg_uuids:
            sg_cols = obj_uuid_table.get(sg_uuid,
                columns=['prop:security_group_id', 'fq_name'],
                column_count=MAX_COL)
            sg_id = json.loads(sg_cols['prop:security_group_id'])
            sg_fq_name_str = ':'.join(json.loads(sg_cols['fq_name']))
            cassandra_all_sgs[sg_id] = sg_fq_name_str

        logger.debug("Got %d security-groups with id", len(cassandra_all_sgs))

        zk_set = set([(id, fqns) for id, fqns in zk_all_sgs.items()])
        cassandra_set = set([(id, fqns) for id, fqns in cassandra_all_sgs.items()])
        extra_sg_ids = zk_set - cassandra_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            ret_ok = False
            errmsg = 'Extra SG IDs in zookeeper for sg %s %s' \
                    %(sg_fq_name_str, sg_id)
            ret_msg += 'Error, %s. ' %(errmsg)

        extra_sg_ids = cassandra_set - zk_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            ret_ok = False
            errmsg = 'Extra SG IDs in cassandra for sg %s %s' \
                    %(sg_fq_name_str, sg_id)
            ret_msg += 'Error, %s. ' %(errmsg)

        return ret_ok, ret_msg
    # end check_security_groups_id

    def _addr_alloc_process_ip_objects(self, cassandra_all_vns,
                                       ip_type, ip_uuids):
        ret_ok = True
        ret_msg = ''

        if ip_type == 'instance-ip':
            addr_prop = 'prop:instance_ip_address'
        elif ip_type == 'floating-ip':
            addr_prop = 'prop:floating_ip_address'
        else:
            raise Exception('Unknown ip type %s' %(ip_type))

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        for ip_id in ip_uuids:
            # get addr
            ip_cols = obj_uuid_table.get(ip_id, column_count=MAX_COL)
            try:
                ip_addr = json.loads(ip_cols[addr_prop])
            except KeyError:
                ret_ok = False
                errmsg = 'Missing ip addr in %s %s' %(ip_type, ip_id)
                ret_msg += 'Error, %s.' %(errmsg)
                continue

            # get vn uuid
            vn_id = None
            for col_name in ip_cols.keys():
                mch = re.match('ref:virtual_network:(.*)', col_name)
                if not mch:
                    continue
                vn_id = mch.group(1)

            if not vn_id:
                ret_ok = False
                ret_msg += 'Error, Missing vn-id in %s %s. ' %(ip_type, ip_id)
                continue

            fq_name_json = obj_uuid_table.get(vn_id,
                               columns=['fq_name'],
                               column_count=MAX_COL)['fq_name']
            fq_name_str = ':'.join(json.loads(fq_name_json))
            if fq_name_str not in cassandra_all_vns:
                # find all subnets on this VN and add for later check
                cassandra_all_vns[fq_name_str] = {}
                ipam_refs = obj_uuid_table.get(vn_id,
                    column_start='ref:network_ipam:',
                    column_finish='ref:network_ipam;',
                    column_count=MAX_COL)
                for ipam in ipam_refs:
                    ipam_col = obj_uuid_table.get(vn_id,
                        columns=[ipam], column_count=MAX_COL)
                    attr_dict = json.loads(ipam_col[ipam])['attr']
                    for subnet in attr_dict['ipam_subnets']:
                        sn_key = '%s/%s' %(subnet['subnet']['ip_prefix'],
                                           subnet['subnet']['ip_prefix_len'])
                        gw = subnet['default_gateway']
                        cassandra_all_vns[fq_name_str][sn_key] = {'gw': gw,
                                                                  'addrs': []}
            # end first encountering vn

            for sn_key in cassandra_all_vns[fq_name_str]:
                addr_added = False
                subnet = cassandra_all_vns[fq_name_str][sn_key]
                if not IPAddress(ip_addr) in IPNetwork(sn_key):
                    continue
                cassandra_all_vns[fq_name_str][sn_key]['addrs'].append(ip_addr)
                addr_added = True
                break
            # end for all subnets in vn

            if not addr_added:
                ret_ok = False
                errmsg = 'Missing subnet for ip %s %s' %(ip_type, ip_id)
                ret_msg += 'Error, %s. ' %(errmsg)
            # end handled the ip
        # end for all ip_uuids

        return ret_ok, ret_msg
    # end _addr_alloc_process_ip_objects

    def _connect_to_ifmap_servers(self):
        self._mapclients = []
        mapclients = []
        NAMESPACES = {
            'env':   "http://www.w3.org/2003/05/soap-envelope",
            'ifmap':   "http://www.trustedcomputinggroup.org/2010/IFMAP/2",
            'meta':
            "http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2",
            'contrail':   "http://www.contrailsystems.com/vnc_cfg.xsd"
        }
        ifmap_user, ifmap_passwd = self._args.ifmap_credentials.split(':')
        # pick ifmap servers from args to this utility and if absent
        # pick it from contrail-api conf file
        if self._args.ifmap_servers:
            ifmap_ips_ports = [(ip_port.split(':'))
                                   for ip_port in self._args.ifmap_servers]
        else:
            ifmap_ips_ports = [(self._api_args.ifmap_server_ip,
                                    self._api_args.ifmap_server_port)]
        for ifmap_ip, ifmap_port in ifmap_ips_ports:
            mapclient = client((ifmap_ip, ifmap_port),
                               ifmap_user, ifmap_passwd,
                               NAMESPACES)
            self._mapclients.append(mapclient)
            connected = False
            while not connected:
                try:
                    result = mapclient.call('newSession', NewSessionRequest())
                    connected = True
                except socket.error as e:
                    time.sleep(3)

            mapclient.set_session_id(newSessionResult(result).get_session_id())
            mapclient.set_publisher_id(newSessionResult(result).get_publisher_id())

    # end _connect_to_ifmap_servers

# end class DatabaseChecker


def db_check():
    cgitb.enable(format='text')

    args_str = ' '.join(sys.argv[1:])
    db_checker = DatabaseChecker(args_str)

    # Mode and node count check across all nodes
    db_checker.check_zk_mode_and_node_count()
    db_checker.check_cassandra_keyspace_replication()
    db_checker.check_obj_mandatory_fields()
    db_checker.check_fq_name_uuid_ifmap_match()
    db_checker.check_subnet_uuid()
    db_checker.check_subnet_addr_alloc()
    db_checker.check_route_targets_id()
    db_checker.check_virtual_networks_id()
    db_checker.check_security_groups_id()
# end db_check
