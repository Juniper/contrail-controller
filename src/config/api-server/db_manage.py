import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import socket
import re
import logging
import time
from cfgm_common import jsonutils as json
from netaddr import IPAddress, IPNetwork
import argparse
from cStringIO import StringIO
import time

import kazoo.client
import kazoo.exceptions
import cfgm_common
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_cassandra import VncCassandraClient
import pycassa
import utils

from vnc_db import VncServerCassandraClient
import schema_transformer.db

MAX_COL = 10000000

# All possible errors from audit
class AuditError(object):
    def __init__(self, msg):
        self.msg = msg
    # end __init__
# class AuditError

class ZkStandaloneError(AuditError): pass
class ZkFollowersError(AuditError): pass
class ZkNodeCountsError(AuditError): pass
class CassWrongRFError(AuditError): pass
class FQNIndexMissingError(AuditError): pass
class FQNStaleIndexError(AuditError): pass
class FQNMismatchError(AuditError): pass
class MandatoryFieldsMissingError(AuditError): pass
class IpSubnetMissingError(AuditError): pass
class VirtualNetworkMissingError(AuditError): pass
class VirtualNetworkIdMissingError(AuditError): pass
class IpAddressMissingError(AuditError): pass
class UseragentSubnetExtraError(AuditError): pass
class UseragentSubnetMissingError(AuditError): pass
class SubnetCountMismatchError(AuditError): pass
class SubnetUuidMissingError(AuditError): pass
class SubnetKeyToIdMissingError(AuditError): pass
class SubnetIdToKeyMissingError(AuditError): pass
class ZkRTgtIdMissingError(AuditError): pass
class ZkRTgtIdExtraError(AuditError): pass
class ZkSGIdMissingError(AuditError): pass
class ZkSGIdExtraError(AuditError): pass
class SG0UnreservedError(AuditError): pass
class ZkVNIdExtraError(AuditError): pass
class ZkVNIdMissingError(AuditError): pass
class RTCountMismatchError(AuditError): pass
class CassRTRangeError(AuditError): pass
class ZkRTRangeError(AuditError): pass
class ZkIpMissingError(AuditError): pass
class ZkIpExtraError(AuditError): pass
class ZkSubnetMissingError(AuditError): pass
class ZkSubnetExtraError(AuditError): pass
class ZkVNMissingError(AuditError): pass
class ZkVNExtraError(AuditError): pass

class DatabaseManager(object):
    OBJ_MANDATORY_COLUMNS = ['type', 'fq_name', 'prop:id_perms']
    BASE_VN_ID_ZK_PATH = '/id/virtual-networks'
    BASE_RTGT_ID_ZK_PATH = '/id/bgp/route-targets'
    BASE_SG_ID_ZK_PATH = '/id/security-groups/id'
    BASE_SUBNET_ZK_PATH = '/api-server/subnets'
    AUTO_RTGT_START = 8000000
    def __init__(self, args_str=''):
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
        db_info = VncServerCassandraClient.get_db_info()
        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._cf_dict = {}
        cred = None
        if self._args.cassandra_user is not None and \
           self._args.cassandra_password is not None:
               cred={'username':self._args.cassandra_user,
                     'password':self._args.cassandra_password}
        for ks_name, cf_name_list in db_info:
            pool = pycassa.ConnectionPool(keyspace=ks_name,
                       server_list=self._cassandra_servers, prefill=False,
                       credentials=cred)
            for cf_name in cf_name_list:
                self._cf_dict[cf_name] = pycassa.ColumnFamily(pool, cf_name,
                                         read_consistency_level=rd_consistency)

        # zookeeper connection
        zk_server = self._api_args.zk_server_ip.split(',')[0]
        self._zk_client = kazoo.client.KazooClient(zk_server)
        self._zk_client.start()
    # end __init__

    def _parse_args(self, args_str):
        parser = argparse.ArgumentParser()

        help="Path to contrail-api conf file, default /etc/contrail-api.conf"
        parser.add_argument(
            "--api-conf", help=help, default="/etc/contrail/contrail-api.conf")
        parser.add_argument(
            "--dry-run", help="Reads are real, report instead of doing writes",
            action='store_true', default=False)
        parser.add_argument(
            "--verbose", help="Run in verbose/INFO mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--debug", help="Run in debug mode, default False",
            action='store_true', default=False)
        parser.add_argument("--cassandra-user")
        parser.add_argument("--cassandra-password")

        args_obj, remaining_argv = parser.parse_known_args(args_str.split())
        self._args = args_obj

        self._api_args = utils.parse_args('-c %s %s'
            %(self._args.api_conf, ' '.join(remaining_argv)))[0]
    # end _parse_args

    def audit_subnet_uuid(self):
        ret_errors = []

        # check in useragent table whether net-id subnet -> subnet-uuid
        # and vice-versa exist for all subnets
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        ua_subnet_info = {}
        for key, cols in ua_kv_cf.get_range():
            mch = re.match('(.* .*/.*)', key)
            if mch: # subnet key -> uuid
                subnet_key = mch.group(1)
                subnet_id = cols['value']
                try:
                    reverse_map = ua_kv_cf.get(subnet_id)
                except pycassa.NotFoundException:
                    errmsg = "Missing id(%s) to key(%s) mapping in useragent"\
                             %(subnet_id, subnet_key)
                    ret_errors.append(SubnetIdToKeyMissingError(errmsg))
            else: # uuid -> subnet key
                subnet_id = key
                subnet_key = cols['value']
                ua_subnet_info[subnet_id] = subnet_key
                try:
                    reverse_map = ua_kv_cf.get(subnet_key)
                except pycassa.NotFoundException:
                    errmsg = "Missing key(%s) to id(%s) mapping in useragent"\
                             %(subnet_key, subnet_id)
                    ret_errors.append(SubnetKeyToIdMissingError(errmsg))

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
                        errmsg = "Missing uuid in ipam-subnet for %s %s" \
                                 %(vn_id, subnet['subnet']['ip_prefix'])
                        ret_errors.append(SubnetUuidMissingError(errmsg))

        return ua_subnet_info, vnc_all_subnet_uuids, ret_errors
    # end audit_subnet_uuid

    def audit_route_targets_id(self):
        logger = self._logger
        ret_errors = []

        # read in route-target ids from zookeeper
        base_path = self.BASE_RTGT_ID_ZK_PATH
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_rtgts = {}
        num_bad_rtgts = 0
        for rtgt_id in self._zk_client.get_children(base_path) or []:
            rtgt_fq_name_str = self._zk_client.get(base_path+'/'+rtgt_id)[0]
            zk_all_rtgts[int(rtgt_id)] = rtgt_fq_name_str
            if int(rtgt_id) >= self.AUTO_RTGT_START:
                continue # all good

            errmsg = 'Wrong route-target range in zookeeper %s' %(rtgt_id)
            ret_errors.append(ZkRTRangeError(errmsg))
            num_bad_rtgts += 1

        logger.debug("Got %d route-targets with id in zookeeper %d from wrong range",
            len(zk_all_rtgts), num_bad_rtgts)

        # read in route-targets from cassandra to get id+fq_name
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading route-target objects from cassandra")
        try:
            rtgt_row = fq_name_table.get('route_target', column_count=MAX_COL)
        except pycassa.NotFoundException:
            rtgt_row = []
        rtgt_uuids = [x.split(':')[-1] for x in rtgt_row]
        cassandra_all_rtgts = {}
        num_bad_rtgts = 0
        for rtgt_uuid in rtgt_uuids:
            rtgt_cols = obj_uuid_table.get(rtgt_uuid,
                columns=['fq_name'],
                column_count=MAX_COL)
            rtgt_fq_name_str = ':'.join(json.loads(rtgt_cols['fq_name']))
            rtgt_id = json.loads(rtgt_cols['fq_name']).split(':')[-1]
            if rtgt_id >= self.AUTO_RTGT_START:
                errmsg = 'Wrong route-target range in cassandra %s' %(rtgt_id)
                ret_errors.append(CassRTRangeError(errmsg))
                num_bad_rtgts += 1
            cassandra_all_rtgts[rtgt_id] = rtgt_fq_name_str

        logger.debug("Got %d route-targets with id in cassandra %d from wrong range",
            len(cassandra_all_rtgts), num_bad_rtgts)
        zk_set = set([(id, fqns) for id, fqns in zk_all_rtgts.items()])
        cassandra_set = set([(id-self.AUTO_RTGT_START, fqns)
            for id, fqns in cassandra_all_rtgts.items()
            if id >= self.AUTO_RTGT_START])

        return zk_set, cassandra_set, ret_errors
    # end audit_route_targets_id

    def audit_security_groups_id(self):
        logger = self._logger
        ret_errors = []

        # read in security-group ids from zookeeper
        base_path = self.BASE_SG_ID_ZK_PATH
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_sgs = {}
        for sg_id in self._zk_client.get_children(base_path) or []:
            # sg-id of 0 is reserved
            if int(sg_id) == 0:
                sg_val = self._zk_client.get(base_path+'/'+sg_id)[0]
                if sg_val != '__reserved__':
                    ret_errors.append(SG0UnreservedError(''))
                continue

            sg_fq_name_str = self._zk_client.get(base_path+'/'+sg_id)[0]
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
        min_id = 8000000
        zk_set = set([(id, fqns) for id, fqns in zk_all_sgs.items()])
        cassandra_set = set([(id-min_id, fqns) for id, fqns in cassandra_all_sgs.items() if id >= min_id])

        return zk_set, cassandra_set, ret_errors
    # end audit_security_groups_id

    def audit_virtual_networks_id(self):
        logger = self._logger
        ret_errors = []

        # read in virtual-network ids from zookeeper
        base_path = self.BASE_VN_ID_ZK_PATH
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_vns = {}
        for vn_id in self._zk_client.get_children(base_path) or []:
            vn_fq_name_str = self._zk_client.get(base_path+'/'+vn_id)[0]
            # VN-id in zk starts from 0, in cassandra starts from 1
            zk_all_vns[int(vn_id)+1] = vn_fq_name_str

        logger.debug("Got %d virtual-networks with id in ZK.",
            len(zk_all_vns))

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
                columns=['prop:virtual_network_properties', 'fq_name',
                         'prop:virtual_network_network_id'],
                column_count=MAX_COL)
            try:
                vn_id = json.loads(vn_cols['prop:virtual_network_network_id'])
            except KeyError:
                try:
                    # upgrade case older VNs had it in composite prop
                    vn_props = json.loads(vn_cols['prop:virtual_network_properties'])
                    vn_id = vn_props['network_id']
                except KeyError:
                    errmsg = 'Missing network-id in cassandra for vn %s' \
                             %(vn_uuid)
                    ret_errors.append(VirtualNetworkIdMissingError(errmsg))
                    continue
            vn_fq_name_str = ':'.join(json.loads(vn_cols['fq_name']))
            cassandra_all_vns[vn_id] = vn_fq_name_str

        logger.debug("Got %d virtual-networks with id in Cassandra.",
            len(cassandra_all_vns))

        zk_set = set([(id, fqns) for id, fqns in zk_all_vns.items()])
        cassandra_set = set([(id, fqns) for id, fqns in cassandra_all_vns.items()])

        return zk_set, cassandra_set, ret_errors
    # end audit_virtual_networks_id

# end class DatabaseManager

class DatabaseChecker(DatabaseManager):
    def checker(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Checker %s: Success' %(func.__name__))
                else:
                    self._logger.error('Checker %s: Failed:\n%s\n'
                        %(func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Checker %s: Exception, %s' %(func.__name__, err_msg))
                raise
        # end wrapper

        return wrapper
    # end checker

    @checker
    def check_zk_mode_and_node_count(self):
        ret_errors = []
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
                err_msg = "Error, Single zookeeper server and modes %s." \
                          %(str(modes))
                ret_errors.append(ZkStandaloneError(err_msg))
        else:
            # good-case: 1 node in leader, >=1 in followers
            if (modes['leader'] == 1) and (modes['follower'] >= 1):
                pass # ok
            else:
                ret_errors.append(ZkFollowersError(
                    "Error, Incorrect modes %s." %(str(modes))))

        # Check node count
        node_counts = []
        for stat_out in stats.values():
            nc = int(re.search('Node count:(.*)\n', stat_out).group(1))
            node_counts.append(nc)
        # all nodes should have same count, so set should have 1 elem
        if len(set(node_counts)) != 1:
            ret_errors.append(ZkNodeCountsError(
                "Error, Differing node counts %s." %(str(node_counts))))

        return ret_errors
    # end check_zk_mode_and_node_count

    @checker
    def check_cassandra_keyspace_replication(self):
        ret_errors = []
        logger = self._logger
        cred = None
        if self._args.cassandra_user is not None and \
           self._args.cassandra_password is not None:
               cred={'username':self._args.cassandra_user,
                     'password':self._args.cassandra_password}
        for server in self._cassandra_servers:
            sys_mgr = pycassa.SystemManager(server, credentials=cred)
            db_info = VncCassandraClient.get_db_info() + \
                schema_transformer.db.SchemaTransformerDB.get_db_info()
            for ks_name, _ in db_info:
                logger.debug("Reading keyspace properties for %s on %s: ",
                             ks_name, server)
                ks_prop = sys_mgr.get_keyspace_properties(ks_name)
                logger.debug("Got %s", ks_prop)

                repl_factor = int(ks_prop['strategy_options']['replication_factor'])
                if (repl_factor != len(self._cassandra_servers)):
                    errmsg = 'Incorrect replication factor %d for keyspace %s' \
                             %(repl_factor, ks_name)
                    ret_errors.append(CassWrongRFError(errmsg))

        return ret_errors
    # end check_cassandra_keyspace_replication

    @checker
    def check_rabbitmq_queue(self):
        ret_errors = []

        return ret_errors
    # end check_rabbitmq_queue

    @checker
    def check_fq_name_uuid_match(self):
        # ensure items in obj-fq-name-table match to obj-uuid-table
        ret_errors = []
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
                        ret_errors.append(FQNMismatchError(
                            'Mismatched FQ Name %s (index) vs %s (object)' \
                                   %(fq_name_str, obj_fq_name_str)))
                except pycassa.NotFoundException:
                    ret_errors.append(FQNStaleIndexError(
                        'Missing object %s %s %s in uuid table'
                        %(obj_uuid, obj_type, fq_name_str)))
            # end for all objects in a type
        # end for all obj types
        logger.debug("Got %d objects", len(fq_name_table_all))

        uuid_table_all = []
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            try:
                obj_type = json.loads(cols['type'])
                fq_name_str = ':'.join(json.loads(cols['fq_name']))
                uuid_table_all.append((obj_type, fq_name_str, obj_uuid))
            except Exception as e:
                ret_errors.append(MandatoryFieldsMissingError(
                    'uuid %s cols %s' %(obj_uuid, cols)))

        logger.debug("Got %d objects", len(uuid_table_all))

        for extra in set(fq_name_table_all) - set(uuid_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_errors.append(FQNStaleIndexError(
                'Stale index %s %s %s in obj_fq_name_table'
                       %(obj_type, fq_name_str, obj_uuid)))

        for extra in set(uuid_table_all) - set(fq_name_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_errors.append(FQNIndexMissingError(
                'Extra object %s %s %s in obj_uuid_table'
                %(obj_type, fq_name_str, obj_uuid)))

        return ret_errors
    # end check_fq_name_uuid_match

    @checker
    def check_obj_mandatory_fields(self):
        # ensure fq_name, type, uuid etc. exist
        ret_errors = []
        logger = self._logger

        logger.debug("Reading all objects from obj_uuid_table")
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        num_objs = 0
        num_bad_objs = 0
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            num_objs += 1
            for col_name in self.OBJ_MANDATORY_COLUMNS:
                if col_name in cols:
                    continue
                num_bad_objs += 1
                ret_errors.append(MandatoryFieldsMissingError(
                    'Error, obj %s missing column %s' %(obj_uuid, col_name)))

        logger.debug("Got %d objects %d with missing mandatory fields",
                     num_objs, num_bad_objs)

        return ret_errors
    # end check_obj_mandatory_fields

    @checker
    def check_subnet_uuid(self):
        # whether useragent subnet uuid and uuid in subnet property match
        ret_errors = []

        ua_subnet_info, vnc_subnet_uuids, errors = self.audit_subnet_uuid()
        ret_errors.extend(errors)

        # check #subnets in useragent table vs #subnets in obj_uuid_table
        if len(ua_subnet_info.keys()) != len(vnc_subnet_uuids):
            ret_errors.append(SubnetCountMismatchError(
                "Mismatch #subnets useragent %d #subnets ipam-subnet %d"
                %(len(ua_subnet_info.keys()), len(vnc_subnet_uuids))))

        # check if subnet-uuids match in useragent table vs obj_uuid_table
        extra_ua_subnets = set(ua_subnet_info.keys()) - set(vnc_subnet_uuids)
        if extra_ua_subnets:
            ret_errors.append(UseragentSubnetExtraError(
                "Extra useragent subnets %s" %(str(extra_ua_subnets))))

        extra_vnc_subnets = set(vnc_subnet_uuids) - set(ua_subnet_info.keys())
        if extra_vnc_subnets:
            ret_errors.append(UseragentSubnetMissingError(
                "Missing useragent subnets %s" %(extra_vnc_subnets)))

        return ret_errors
    # end check_subnet_uuid

    @checker
    def check_subnet_addr_alloc(self):
        # whether ip allocated in subnet in zk match iip+fip in cassandra
        ret_errors = []
        logger = self._logger

        zk_all_vns = {}
        base_path = self.BASE_SUBNET_ZK_PATH
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        num_addrs = 0
        try:
            subnets = self._zk_client.get_children(base_path)
        except kazoo.exceptions.NoNodeError:
            subnets = []
        for subnet in subnets:
            vn_fq_name_str = ':'.join(subnet.split(':')[:-1])
            pfx = subnet.split(':')[-1]
            zk_all_vns[vn_fq_name_str] = {}
            pfxlen_path = base_path + '/' + subnet
            pfxlen = self._zk_client.get_children(pfxlen_path)
            if not pfxlen:
                continue
            subnet_key = '%s/%s' %(pfx, pfxlen[0])
            zk_all_vns[vn_fq_name_str][subnet_key] = []
            addrs = self._zk_client.get_children(pfxlen_path+'/'+pfxlen[0])
            if not addrs:
                continue
            zk_all_vns[vn_fq_name_str][subnet_key] = \
                [str(IPAddress(int(addr))) for addr in addrs]
            num_addrs += len(zk_all_vns[vn_fq_name_str])
        # end for all subnet paths
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
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, 'instance-ip', iip_uuids))

        num_addrs += len(iip_uuids)

        try:
            fip_row = fq_name_table.get('floating_ip', column_count=MAX_COL)
        except pycassa.NotFoundException:
            fip_row = []

        fip_uuids = [x.split(':')[-1] for x in fip_row]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, 'floating-ip', fip_uuids))

        num_addrs += len(fip_uuids)

        logger.debug("Got %d networks %d addresses",
                     len(cassandra_all_vns), num_addrs)

        # check for differences in networks
        extra_vn = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        if extra_vn:
            errmsg = 'Extra VN in zookeeper (vs. cassandra) for %s' \
                     %(str(extra_vn))
            ret_errors.append(ZkVNExtraError(errmsg))

        extra_vn = set(cassandra_all_vns.keys()) - set(zk_all_vns.keys())
        for vn in list(extra_vn):
            # It really is extra in zookeeper only if there is a subnet
            # configured but missing in zookeeper
            if not cassandra_all_vns[vn]:
                extra_vn.remove(vn)
        if extra_vn:
            errmsg = 'Missing VN in zookeeper (vs.cassandra) for %s' \
                     %(str(extra_vn))
            ret_errors.append(ZkVNMissingError(errmsg))

        # check for differences in subnets
        zk_all_vn_sn = []
        for vn_key, vn in zk_all_vns.items():
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        cassandra_all_vn_sn = []
        for vn_key, vn in cassandra_all_vns.items():
            cassandra_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        extra_vn_sn = set(zk_all_vn_sn) - set(cassandra_all_vn_sn)
        if extra_vn_sn:
            errmsg = 'Extra VN/SN in zookeeper for %s' %(extra_vn_sn)
            ret_errors.append(ZkSubnetExtraError(errmsg))

        extra_vn_sn = set(cassandra_all_vn_sn) - set(zk_all_vn_sn)
        if extra_vn_sn:
            errmsg = 'Missing VN/SN in zookeeper for %s' %(extra_vn_sn)
            ret_errors.append(ZkSubnetMissingError(errmsg))

        # check for differences in ip addresses
        for vn, sn_key in set(zk_all_vn_sn) & set(cassandra_all_vn_sn):
            sn_start = cassandra_all_vns[vn][sn_key]['start']
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            sn_dns = cassandra_all_vns[vn][sn_key]['dns']
            zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']
            extra_ips = set(zk_ips) - set(cassandra_ips)
            for ip_addr in extra_ips:
                # ignore network, bcast and gateway ips
                if (IPAddress(ip_addr) == IPNetwork(sn_key).network or
                    IPAddress(ip_addr) == IPNetwork(sn_key).broadcast):
                    continue
                if (ip_addr == sn_gw_ip or ip_addr == sn_dns or
                    ip_addr == sn_start):
                    continue

                errmsg = 'Extra IP %s in zookeeper for vn %s' \
                    %(ip_addr, vn)
                ret_errors.append(ZkIpExtraError(errmsg))
            # end all zk extra ips

            extra_ips = set(cassandra_ips) - set(zk_ips)
            for ip_addr in extra_ips:
                errmsg = 'Missing IP %s in zookeeper for vn %s' \
                    %(ip_addr, vn)
                ret_errors.append(ZkIpMissingError(errmsg))
            # end all cassandra extra ips

        # for all common VN/subnets

        return ret_errors
    # end check_subnet_addr_alloc

    @checker
    def check_route_targets_id(self):
        ret_errors = []
        logger = self._logger

        # read in route-target ids from zookeeper and cassandra
        zk_set, cassandra_set, errors = self.audit_route_targets_id()
        ret_errors.extend(errors)

        extra_rtgt_ids = zk_set - cassandra_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = 'Extra route target ID in zookeeper for RTgt %s %s' \
                    %(rtgt_fq_name_str, rtgt_id)
            ret_errors.append(ZkRTgtIdExtraError(errmsg))

        extra_rtgt_ids = cassandra_set - zk_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = 'Missing route target ID in zookeeper for RTgt %s %s' \
                    %(rtgt_fq_name_str, rtgt_id)
            ret_errors.append(ZkRTgtIdMissingError(errmsg))

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
                if rtgt_num < self.AUTO_RTGT_START:
                    num_user_rtgts += 1
                    continue # all good

                num_bad_rtgts += 1
                errmsg = 'Wrong route-target range in cassandra %d' %(rtgt_num)
                ret_errors.append(CassRTRangeError(errmsg))
            # end for all rtgt
        # end for all vns

        logger.debug("Got %d user configured route-targets, %d in bad range",
                     num_user_rtgts, num_bad_rtgts)

        if len(cassandra_set) != (num_user_rtgts + len(zk_set)):
            errmsg = 'Mismatch in route-target count: %d user, %d zookeeper, %d cassandra' \
                     %(num_user_rtgts, len(zk_set), len(cassandra_set))
            ret_errors.append(RTCountMismatchError(errmsg))

        return ret_errors
    # end check_route_targets_id

    @checker
    def check_virtual_networks_id(self):
        ret_errors = []
        logger = self._logger

        zk_set, cassandra_set, errors = self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        extra_vn_ids = zk_set - cassandra_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            ret_ok = False
            errmsg = 'Extra VN IDs in zookeeper for vn %s %s' \
                    %(vn_fq_name_str, vn_id)
            ret_errors.append(ZkVNIdExtraError(errmsg))

        extra_vn_ids = cassandra_set - zk_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            ret_ok = False
            errmsg = 'Missing VN IDs in zookeeper for vn %s %s' \
                    %(vn_fq_name_str, vn_id)
            ret_errors.append(ZkVNIdMissingError(errmsg))

        return ret_errors
    # end check_virtual_networks_id

    @checker
    def check_security_groups_id(self):
        ret_errors = []
        logger = self._logger

        zk_set, cassandra_set, errors = self.audit_security_groups_id()
        ret_errors.extend(errors)

        extra_sg_ids = zk_set - cassandra_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            errmsg = 'Extra SG IDs in zookeeper for sg %s %s' \
                    %(sg_fq_name_str, sg_id)
            ret_errors.append(ZkSGIdExtraError(errmsg))

        extra_sg_ids = cassandra_set - zk_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            ret_ok = False
            errmsg = 'Missing SG IDs in zookeeper for sg %s %s' \
                    %(sg_fq_name_str, sg_id)
            ret_errors.append(ZkSGIdMissingError(errmsg))

        return ret_errors
    # end check_security_groups_id

    @checker
    def check_schema_db_mismatch(self):
        # TODO detect all objects persisted that have discrepancy from
        # defined schema
        pass
    # end check_schema_db_mismatch

    def _addr_alloc_process_ip_objects(self, cassandra_all_vns,
                                       ip_type, ip_uuids):
        logger = self._logger
        ret_errors = []

        if ip_type == 'instance-ip':
            addr_prop = 'prop:instance_ip_address'
            vn_is_ref = True
        elif ip_type == 'floating-ip':
            addr_prop = 'prop:floating_ip_address'
            vn_is_ref = False
        else:
            raise Exception('Unknown ip type %s' %(ip_type))

        # walk vn fqn index, pick default-gw/dns-server-addr
        # and set as present in cassandra
        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        def set_reserved_addrs_in_cassandra(vn_id, fq_name_str):
            if fq_name_str in cassandra_all_vns:
                # already parsed and handled
                return

            # find all subnets on this VN and add for later check
            try:
                ipam_refs = obj_uuid_table.get(vn_id,
                    column_start='ref:network_ipam:',
                    column_finish='ref:network_ipam;',
                    column_count=MAX_COL)
            except pycassa.NotFoundException:
                logger.debug('VN %s (%s) has no ipam refs',
                    vn_id, fq_name_str)
                return

            cassandra_all_vns[fq_name_str] = {}
            for ipam in ipam_refs:
                ipam_col = obj_uuid_table.get(vn_id,
                    columns=[ipam], column_count=MAX_COL)
                attr_dict = json.loads(ipam_col[ipam])['attr']
                for subnet in attr_dict['ipam_subnets']:
                    sn_key = '%s/%s' %(subnet['subnet']['ip_prefix'],
                                       subnet['subnet']['ip_prefix_len'])
                    gw = subnet['default_gateway']
                    dns = subnet.get('dns_server_address')
                    cassandra_all_vns[fq_name_str][sn_key] = {
                        'start': subnet['subnet']['ip_prefix'],
                        'gw': gw,
                        'dns': dns,
                        'addrs': []}
        # end set_reserved_addrs_in_cassandra

        for fq_name_str_uuid in obj_fq_name_table.get('virtual_network',
                                                      column_count=MAX_COL):
            fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
            vn_id = fq_name_str_uuid.split(':')[-1]
            set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
        # end for all VNs

        for ip_id in ip_uuids:

            # get addr
            try:
                ip_cols = obj_uuid_table.get(ip_id, column_count=MAX_COL)
            except pycassa.NotFoundException:
                errmsg = 'Missing object in uuid table for %s %s' %(ip_type, ip_id)
                ret_errors.append(FQNStaleIndexError(errmsg))
                continue

            try:
                ip_addr = json.loads(ip_cols[addr_prop])
            except KeyError:
                errmsg = 'Missing ip addr in %s %s' %(ip_type, ip_id)
                ret_errors.append(IpAddressMissingError(errmsg))
                continue

            # get vn uuid
            vn_id = None
            if vn_is_ref:
                for col_name in ip_cols.keys():
                    mch = re.match('ref:virtual_network:(.*)', col_name)
                    if not mch:
                        continue
                    vn_id = mch.group(1)
            else:
                vn_fq_name_str = ':'.join(json.loads(ip_cols['fq_name'])[:-2])
                vn_cols = obj_fq_name_table.get('virtual_network',
                    column_start='%s:' %(vn_fq_name_str),
                    column_finish='%s;' %(vn_fq_name_str))
                vn_id = vn_cols.keys()[0].split(':')[-1]

            if not vn_id:
                ret_errors.append(VirtualNetworkMissingError(
                    'Missing vn in %s %s.\n' %(ip_type, ip_id)))
                continue

            fq_name_json = obj_uuid_table.get(vn_id,
                               columns=['fq_name'],
                               column_count=MAX_COL)['fq_name']
            fq_name_str = ':'.join(json.loads(fq_name_json))
            if fq_name_str not in cassandra_all_vns:
                msg = "Found IP %s %s on VN %s (%s) thats not in FQ NAME index" \
                      %(ip_type, ip_id, vn_id, fq_name_str)
                ret_errors.append(FQNIndexMissingError(msg))
                # find all subnets on this VN and add for later check
                set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
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
                errmsg = 'Missing subnet for ip %s %s' %(ip_type, ip_id)
                ret_errors.append(IpSubnetMissingError(errmsg))
            # end handled the ip
        # end for all ip_uuids

        return ret_errors
    # end _addr_alloc_process_ip_objects

# end class DatabaseChecker


class DatabaseCleaner(DatabaseManager):
    def cleaner(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Cleaner %s: Success' %(func.__name__))
                else:
                    self._logger.error('Cleaner %s: Failed:\n%s\n'
                        %(func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Cleaner %s: Exception, %s' %(func.__name__, err_msg))
                raise
        # end wrapper

        return wrapper
    # end cleaner

    @cleaner
    def clean_stale_fq_names(self):
        logger = self._logger
        ret_errors = []

        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_fq_name_table")
        for obj_type, cols in obj_fq_name_table.get_range(column_count=MAX_COL):
            stale_cols = []
            for fq_name_str_uuid in cols:
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                try:
                    _ = obj_uuid_table.get(obj_uuid)
                except pycassa.NotFoundException:
                    logger.info("Found stale fq_name index entry: %s",
                        fq_name_str_uuid)
                    stale_cols.append(fq_name_str_uuid)

            if stale_cols:
                obj_fq_name_table.remove(obj_type, columns=stale_cols)
                logger.info("Removed stale fq_names: %s", stale_cols)

        # TODO do same for zookeeper
        return ret_errors
    # end clean_stale_fq_names

    @cleaner
    def clean_stale_back_refs(self):
        return self._remove_stale_from_uuid_table('backref')
    # end clean_stale_back_refs

    @cleaner
    def clean_stale_children(self):
        return self._remove_stale_from_uuid_table('children')
    # end clean_stale_back_refs

    @cleaner
    def clean_obj_missing_mandatory_fields(self):
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            missing_cols = set(self.OBJ_MANDATORY_COLUMNS) - set(cols.keys())
            if not missing_cols:
                continue
            logger.info("Found object %s with missing columns %s",
                obj_uuid, missing_cols)
            obj_uuid_table.remove(obj_uuid)
            logger.info("Removed object %s", obj_uuid)

        return ret_errors
    # end clean_obj_missing_mandatory_fields

    @cleaner
    def clean_vm_with_no_vmi(self):
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        stale_vm_uuids = []
        logger.debug("Reading all VMs from obj_uuid_table")
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            obj_type = json.loads(cols.get('type', '""'))
            if not obj_type or obj_type != 'virtual_machine':
                continue
            vm_uuid = obj_uuid
            col_names = cols.keys()
            if (any(['backref' in col_name for col_name in col_names]) or
                any(['children' in col_name for col_name in col_names])):
                continue
            logger.info("Found stale VM %s columns %s", vm_uuid, col_names)
            stale_vm_uuids.append(vm_uuid)

        logger.info("Total %s VMs with no VMIs", len(stale_vm_uuids))
        for vm_uuid in stale_vm_uuids:
            obj_uuid_table.remove(vm_uuid)
            logger.info("Removed stale VM %s", vm_uuid)

        return ret_errors
    # end clean_vm_with_no_vmi

    @cleaner
    def clean_stale_route_targets_id(self):
        logger = self._logger
        ret_errors = []

        zk_set, cassandra_set, errors = self.audit_route_targets_id()
        ret_errors.extend(errors)

        stale_rtgt_ids = zk_set - cassandra_set
        for rtgt_id, rtgt_fq_name_str in stale_rtgt_ids:
            id_str = "%(#)010d" % {'#': rtgt_id}
            self._zk_client.delete('%s/%s' %(self.BASE_RTGT_ID_ZK_PATH, id_str))
            logger.info("Removed stale route-target id %s for %s",
                rtgt_id, rtgt_fq_name_str)

        return ret_errors
    # end clean_stale_route_targets_id

    @cleaner
    def clean_stale_security_groups_id(self):
        logger = self._logger
        ret_errors = []

        zk_set, cassandra_set, errors = self.audit_security_groups_id()
        ret_errors.extend(errors)

        stale_sg_ids = zk_set - cassandra_set
        for sg_id, sg_fq_name_str in stale_sg_ids:
            id_str = "%(#)010d" % {'#': sg_id}
            self._zk_client.delete('%s/%s' %(self.BASE_SG_ID_ZK_PATH, id_str))
            logger.info("Removed stale security group id %s for %s",
                sg_id, sg_fq_name_str)

        return ret_errors
    # end clean_stale_security_groups_id

    @cleaner
    def clean_stale_virtual_networks_id(self):
        logger = self._logger
        ret_errors = []

        zk_set, cassandra_set, errors = self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        stale_vn_ids = zk_set - cassandra_set
        for vn_id, vn_fq_name_str in stale_vn_ids:
            id_str = "%(#)010d" % {'#': vn_id-1}
            self._zk_client.delete('%s/%s' %(self.BASE_VN_ID_ZK_PATH, id_str))
            logger.info("Removed stale virtual network id %s for %s",
                vn_id, vn_fq_name_str)

        return ret_errors
    # end clean_stale_virtual_networks_id

    @cleaner
    def clean_stale_subnet_uuid(self):
        logger = self._logger
        ret_errors = []

        ua_subnet_info, vnc_subnet_uuids, errors = self.audit_subnet_uuid()
        ret_errors.extend(errors)

        extra_ua_subnets = set(ua_subnet_info.keys()) - set(vnc_subnet_uuids)
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        for subnet_uuid in extra_ua_subnets:
            subnet_key = ua_subnet_info[subnet_uuid]
            ua_kv_cf.remove(subnet_uuid)
            logger.info("Removed stale subnet uuid %s in useragent keyspace",
                subnet_uuid)
            ua_kv_cf.remove(ua_subnet_info[subnet_key])
            logger.info("Removed stale subnet key %s in useragent keyspace",
                subnet_key)

        return ret_errors
    # end clean_stale_subnet_uuid

    def _remove_stale_from_uuid_table(self, dangle_prefix):
        logger = self._logger
        ret_errors = []

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL):
            obj_type = json.loads(cols.get('type', '"UnknownType"'))
            fq_name = json.loads(cols.get('fq_name', '"UnknownFQN"'))
            stale_cols = []
            for col_name in cols:
                if not dangle_prefix in col_name:
                    continue
                _, _, dangle_check_uuid = col_name.split(':')
                try:
                    _ = obj_uuid_table.get(dangle_check_uuid)
                except pycassa.NotFoundException:
                    msg = "Found stale %s index: %s in %s (%s %s)" \
                        %(dangle_prefix, col_name,
                          obj_uuid, obj_type, fq_name)
                    logger.info(msg)
                    stale_cols.append(col_name)

            if stale_cols:
                obj_uuid_table.remove(obj_uuid, columns=stale_cols)
                logger.info("Removed stale %s: %s", dangle_prefix, stale_cols)

        return ret_errors
    # end _remove_stale_from_uuid_table
# end class DatabaseCleaner


class DatabaseHealer(DatabaseManager):
    def healer(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Healer %s: Success' %(func.__name__))
                else:
                    self._logger.error('Healer %s: Failed:\n%s\n'
                        %(func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Healer %s: Exception, %s' %(func.__name__, err_msg))
                raise
        # end wrapper

        return wrapper
    # end healer

    @healer
    def heal_fq_name_index(self):
        logger = self._logger
        ret_errors = []

        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is row-key val is set of col-names
        fixups = {}
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=MAX_COL,
                                  columns=['type', 'fq_name']):
            obj_type = json.loads(cols.get('type', ""))
            fq_name = json.loads(cols.get('fq_name', ""))
            if not obj_type:
                logger.info("Unknown obj_type for object %s", obj_uuid)
                continue
            if not fq_name:
                logger.info("Unknown fq_name for object %s", obj_uuid)
                continue
            fq_name_str = ':'.join(fq_name)
            try:
                _ = obj_fq_name_table.get(obj_type,
                        columns=['%s:%s' %(fq_name_str, obj_uuid)])
            except pycassa.NotFoundException:
                msg = "Found missing fq_name index: %s (%s %s)" \
                    %(obj_uuid, obj_type, fq_name)
                logger.info(msg)

                if obj_type not in fixups:
                    fixups[obj_type] = set([])
                fixups[obj_type].add(
                    '%s:%s' %(fq_name_str, obj_uuid))
        # for all objects in uuid table

        for obj_type in fixups:
            cols = list(fixups[obj_type])
            if self._args.dry_run:
                logger.info("Would insert row/columns: %s %s", obj_type, cols)
            else:
                logger.info("Inserting row/columns: %s %s", obj_type, cols)
                obj_fq_name_table.insert(obj_type,
                    columns=dict((x, json.dumps(None)) for x in cols))

        return ret_errors
    # end heal_fq_name_index

    @healer
    def heal_back_ref_index(self):
        return []
    # end heal_back_ref_index

    @healer
    def heal_children_index(self):
        logger = self._logger
        ret_errors = []

        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is parent row-key val is set of col-names
        fixups = {}
        for obj_uuid, cols in self._cf_dict['obj_uuid_table'].get_range(
                column_count=MAX_COL, column_start='parent:',
                column_finish='parent;'):
            if not cols:
                continue # no parent
            if len(cols) > 1:
                logger.info('Multiple parents %s for %s', cols, obj_uuid)
                continue

            parent_uuid = cols.keys()[0].split(':')[-1]
            try:
                _ = self._cf_dict['obj_uuid_table'].get(parent_uuid)
            except pycassa.NotFoundException:
                msg = "Missing parent %s for object %s" \
                    %(parent_uuid, obj_uuid)
                logger.info(msg)
                continue

            cols = self._cf_dict['obj_uuid_table'].get(
                obj_uuid, columns=['type'], column_count=MAX_COL)
            obj_type = json.loads(cols.get('type', ""))
            if not obj_type:
                logger.info("Missing type for object %s", obj_uuid)
                continue

            child_col = 'children:%s:%s' %(obj_type, obj_uuid)
            try:
                _ = self._cf_dict['obj_uuid_table'].get(
                        parent_uuid, columns=[child_col])
                # found it, this object is indexed by parent fine
                continue
            except pycassa.NotFoundException:
                msg = "Found missing children index %s for parent %s" \
                    %(child_col, parent_uuid)
                logger.info(msg)

            if parent_uuid not in fixups:
                fixups[parent_uuid] = set([])
            fixups[parent_uuid].add(child_col)
        # for all objects in uuid table

        for parent_uuid in fixups:
            cols = list(fixups[parent_uuid])
            if self._args.dry_run:
                logger.info("Would insert row/columns: %s %s",
                    parent_uuid, cols)
            else:
                logger.info("Inserting row/columns: %s %s",
                    parent_uuid, cols)
                self._cf_dict['obj_uuid_table'].insert(parent_uuid,
                    columns=dict((x, json.dumps(None)) for x in cols))

        return ret_errors
    # end heal_children_index

    def heal_subnet_uuid(self):
        pass
    # end heal_subnet_uuid

    def heal_route_targets_id(self):
        pass
    # end heal_route_targets_id

    def heal_virtual_networks_id(self):
        pass
    # end heal_virtual_networks_id

    def heal_security_groups_id(self):
        pass
    # end heal_security_groups_id
# end class DatabaseCleaner

def db_check(args_str=''):
    vnc_cgitb.enable(format='text')

    db_checker = DatabaseChecker(args_str)
    # Mode and node count check across all nodes
    db_checker.check_zk_mode_and_node_count()
    db_checker.check_cassandra_keyspace_replication()
    db_checker.check_obj_mandatory_fields()
    db_checker.check_fq_name_uuid_match()
    db_checker.check_subnet_uuid()
    db_checker.check_subnet_addr_alloc()
    db_checker.check_route_targets_id()
    db_checker.check_virtual_networks_id()
    db_checker.check_security_groups_id()
    db_checker.check_schema_db_mismatch()
# end db_check

def db_clean(args_str=''):
    vnc_cgitb.enable(format='text')

    db_cleaner = DatabaseCleaner(args_str)
    db_cleaner.clean_obj_missing_mandatory_fields()
    db_cleaner.clean_vm_with_no_vmi()
    db_cleaner.clean_stale_fq_names()
    db_cleaner.clean_stale_back_refs()
    db_cleaner.clean_stale_children()
    db_cleaner.clean_stale_subnet_uuid()
    db_cleaner.clean_stale_route_targets_id()
    db_cleaner.clean_stale_virtual_networks_id()
    db_cleaner.clean_stale_security_groups_id()
# end db_clean

def db_heal(args_str=''):
    vnc_cgitb.enable(format='text')

    db_healer = DatabaseHealer(args_str)
    db_healer.heal_fq_name_index()
    db_healer.heal_back_ref_index()
    db_healer.heal_children_index()
    db_healer.heal_subnet_uuid()
    db_healer.heal_route_targets_id()
    db_healer.heal_virtual_networks_id()
    db_healer.heal_security_groups_id()
# end db_heal

def db_touch_latest(args_str=''):
    vnc_cgitb.enable(format='text')

    db_mgr = DatabaseManager(args_str)
    obj_uuid_table = db_mgr._cf_dict['obj_uuid_table']

    for obj_uuid, cols in obj_uuid_table.get_range(column_count=1):
        db_mgr._cf_dict['obj_uuid_table'].insert(obj_uuid,
                    columns={'META:latest_col_ts': json.dumps(None)})
# end db_touch_latest

def main(verb, args_str):
    if 'db_%s' %(verb) in globals():
        return globals()['db_%s' %(verb)](' '.join(sys.argv[1:]))

    if getattr(DatabaseChecker, verb, None):
        db_checker = DatabaseChecker((' '.join(sys.argv[1:])))
        return getattr(db_checker, verb)()

    if getattr(DatabaseCleaner, verb, None):
        db_cleaner = DatabaseCleaner((' '.join(sys.argv[1:])))
        return getattr(db_cleaner, verb)()

    if getattr(DatabaseHealer, verb, None):
        db_healer = DatabaseHealer((' '.join(sys.argv[1:])))
        return getattr(db_healer, verb)()
# end main

if __name__ == '__main__':
    sys.argv, verb = sys.argv[:-1], sys.argv[-1]
    main(verb, sys.argv)
