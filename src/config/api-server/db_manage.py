import sys
import os
reload(sys)
sys.setdefaultencoding('UTF8')
import socket
import re
import logging
from cfgm_common import jsonutils as json
from netaddr import IPAddress, IPNetwork
from netaddr.core import AddrFormatError
import argparse
from cStringIO import StringIO

import kazoo.client
import kazoo.exceptions
import cfgm_common
try:
    from cfgm_common import vnc_cgitb
except ImportError:
    import cgitb as vnc_cgitb
from cfgm_common.utils import cgitb_hook
import pycassa
import utils

try:
    from vnc_db import VncServerCassandraClient
except ImportError:
    from vnc_cfg_ifmap import VncServerCassandraClient
import schema_transformer.db

SG_ID_MIN_ALLOC = cfgm_common.SGID_MIN_ALLOC
RT_ID_MIN_ALLOC = cfgm_common.BGP_RTGT_MIN_ID

def _parse_rt(rt):
    if isinstance(rt, basestring):
        prefix, asn, target = rt.split(':')
    else:
        prefix, asn, target = rt
    if prefix != 'target':
        raise ValueError()
    target = int(target)
    if not asn.isdigit():
        try:
            IPAddress(asn)
        except AddrFormatError:
            raise ValueError()
    else:
        asn = int(asn)
    return asn, target

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
class SubnetIdToKeyMissingError(AuditError): pass
class ZkRTgtIdMissingError(AuditError): pass
class ZkRTgtIdExtraError(AuditError): pass
class ZkSGIdMissingError(AuditError): pass
class ZkSGIdExtraError(AuditError): pass
class SG0UnreservedError(AuditError): pass
class SGDuplicateIdError(AuditError): pass
class ZkVNIdExtraError(AuditError): pass
class ZkVNIdMissingError(AuditError): pass
class VNDuplicateIdError(AuditError): pass
class RTDuplicateIdError(AuditError): pass
class CassRTRangeError(AuditError): pass
class ZkRTRangeError(AuditError): pass
class ZkIpMissingError(AuditError): pass
class ZkIpExtraError(AuditError): pass
class ZkSubnetMissingError(AuditError): pass
class ZkSubnetExtraError(AuditError): pass
class ZkVNMissingError(AuditError): pass
class ZkVNExtraError(AuditError): pass
class CassRTgtIdExtraError(AuditError): pass
class CassRTgtIdMissingError(AuditError): pass


class DatabaseManager(object):
    OBJ_MANDATORY_COLUMNS = ['type', 'fq_name', 'prop:id_perms']
    BASE_VN_ID_ZK_PATH = '/id/virtual-networks'
    BASE_RTGT_ID_ZK_PATH = '/id/bgp/route-targets'
    BASE_SG_ID_ZK_PATH = '/id/security-groups/id'
    BASE_SUBNET_ZK_PATH = '/api-server/subnets'
    def __init__(self, args_str=''):
        self._parse_args(args_str)

        self._logger = utils.ColorLog(logging.getLogger(__name__))
        self._svc_name = os.path.basename(sys.argv[0])
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
        cluster_id = self._api_args.cluster_id

        # cassandra connection
        self._cassandra_servers = self._api_args.cassandra_server_list
        self._db_info = VncServerCassandraClient.get_db_info() + \
            schema_transformer.db.SchemaTransformerDB.get_db_info()
        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._cf_dict = {}
        cred = None
        if self._args.cassandra_user is not None and \
           self._args.cassandra_password is not None:
               cred={'username':self._args.cassandra_user,
                     'password':self._args.cassandra_password}
        for ks_name, cf_name_list in self._db_info:
            if cluster_id:
                full_ks_name = '%s_%s' %(cluster_id, ks_name)
            else:
                full_ks_name = ks_name
            pool = pycassa.ConnectionPool(keyspace=full_ks_name,
                       server_list=self._cassandra_servers, prefill=False,
                       credentials=cred)
            for cf_name in cf_name_list:
                self._cf_dict[cf_name] = pycassa.ColumnFamily(pool, cf_name,
                                         read_consistency_level=rd_consistency)

        # zookeeper connection
        self.base_vn_id_zk_path = cluster_id + self.BASE_VN_ID_ZK_PATH
        self.base_rtgt_id_zk_path = cluster_id + self.BASE_RTGT_ID_ZK_PATH
        self.base_sg_id_zk_path = cluster_id + self.BASE_SG_ID_ZK_PATH
        self.base_subnet_zk_path = cluster_id + self.BASE_SUBNET_ZK_PATH

        while True:
            try:
                self._logger.error("Api Server zk start")
                self._zk_client = kazoo.client.KazooClient(self._api_args.zk_server_ip)
                self._zk_client.start()
                break
            except Exception as e:
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                try:
                    self._zk_client.stop()
                    self._zk_client.close()
                except Exception as ex:
                    template = "Exception {0} in ApiServer zkstart. Args:\n{1!r}"
                    messag = template.format(type(ex).__name__, ex.args)
                    self._logger.error("%s : traceback %s for %s" % \
                        (messag, traceback.format_exc(), self._svc_name))
                finally:
                    self._zk_client = None
                gevent.sleep(1)

        # Get the system global autonomous system
        self.global_asn = self.get_autonomous_system()
    # end __init__

    def _parse_args(self, args_str):
        parser = argparse.ArgumentParser()

        help="Path to contrail-api conf file, default /etc/contrail-api.conf"
        parser.add_argument(
            "--api-conf", help=help, default="/etc/contrail/contrail-api.conf")
        parser.add_argument(
            "--execute", help="Exceute database modifications",
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

    def get_autonomous_system(self):
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        cols = fq_name_table.get(
            'global_system_config',
            column_start='default-global-system-config:',
            column_finish='default-global-system-config;')
        gsc_uuid = cols.popitem()[0].split(':')[-1]
        cols = obj_uuid_table.get(gsc_uuid, columns=['prop:autonomous_system'])
        return int(json.loads(cols['prop:autonomous_system']))

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
                    # Since release 3.2, only subnet_id/subnet_key are store in
                    # key/value store, the reverse was removed
                    continue

        # check all subnet prop in obj_uuid_table to see if subnet-uuid exists
        vnc_all_subnet_uuids = []
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        vn_row = fq_name_table.xget('virtual_network')
        vn_uuids = [x.split(':')[-1] for x, _ in vn_row]
        for vn_id in vn_uuids:
            ipam_refs = obj_uuid_table.xget(
                vn_id,
                column_start='ref:network_ipam:',
                column_finish='ref:network_ipam;')
            if not ipam_refs:
                continue

            for _, attr_json_dict in ipam_refs:
                attr_dict = json.loads(attr_json_dict)['attr']
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
        fq_name_table = self._cf_dict['obj_fq_name_table']
        rt_table = self._cf_dict['route_target_table']

        # read in route-target ids from zookeeper
        base_path = self.base_rtgt_id_zk_path
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_rtgts = {}
        num_bad_rtgts = 0
        for rtgt_id in self._zk_client.get_children(base_path) or []:
            rtgt_fq_name_str = self._zk_client.get(base_path+'/'+rtgt_id)[0]
            zk_all_rtgts[int(rtgt_id) - RT_ID_MIN_ALLOC] = rtgt_fq_name_str
            if int(rtgt_id) >= RT_ID_MIN_ALLOC:
                continue  # all good
            errmsg = 'Wrong route-target range in zookeeper %s' % rtgt_id
            ret_errors.append(ZkRTRangeError(errmsg))
            num_bad_rtgts += 1
        logger.debug("Got %d route-targets with id in zookeeper %d from wrong "
                     "range", len(zk_all_rtgts), num_bad_rtgts)

        # read route-targets from schema transformer cassandra keyspace
        logger.debug("Reading route-target IDs from cassandra schema "
                     "transformer keyspace")
        cassandra_schema_all_rtgts = {}
        cassandra_schema_duplicate_rtgts = {}
        for fq_name_str, rtgt_num in rt_table.get_range(columns=['rtgt_num']):
            rtgt_id = int(rtgt_num['rtgt_num'])
            if rtgt_id < RT_ID_MIN_ALLOC:
                # Should never append
                logger.error("Route-target ID %d allocated for %s by the "
                             "schema transformer is not contained in the "
                             "system range", rtgt_id, fq_name_str)
            if rtgt_id in cassandra_schema_all_rtgts:
                rtgt_cols = fq_name_table.get(
                    'route_target',
                    column_start='%s:' % fq_name_str,
                    column_finish='%s;' % fq_name_str)
                rtgt_uuid = rtgt_cols.keys()[0].split(':')[-1]
                cassandra_schema_duplicate_rtgts.setdefault(
                    rtgt_id - RT_ID_MIN_ALLOC, []).append(
                        (fq_name_str, rtgt_uuid))
            else:
                cassandra_schema_all_rtgts[rtgt_id - RT_ID_MIN_ALLOC] =\
                    fq_name_str

        # read in route-targets from API server cassandra keyspace
        logger.debug("Reading route-target objects from cassandra API server "
                     "keyspace")
        rtgt_row = fq_name_table.xget('route_target')
        rtgt_fq_name_uuid = [(x.split(':')[:-1], x.split(':')[-1])
                             for x, _ in rtgt_row]
        cassandra_api_all_rtgts = {}
        cassandra_api_duplicate_rtgts = {}
        user_rtgts = 0
        for rtgt_fq_name, rtgt_uuid in rtgt_fq_name_uuid:
            rtgt_asn, rtgt_id = _parse_rt(rtgt_fq_name)
            if rtgt_asn != self.global_asn or rtgt_id < RT_ID_MIN_ALLOC:
                user_rtgts += 1
                continue  # Ignore user defined RT
            rtgt_id = rtgt_id - RT_ID_MIN_ALLOC
            fq_name_str = cassandra_schema_all_rtgts.get(rtgt_id)
            if rtgt_id in cassandra_api_all_rtgts:
                cassandra_api_duplicate_rtgts.setdefault(
                    rtgt_id, []).append((fq_name_str, rtgt_uuid))
            else:
                cassandra_api_all_rtgts[rtgt_id] = fq_name_str

        logger.debug("Got %d system defined route-targets id in cassandra and "
                     "%d defined by users", len(cassandra_schema_all_rtgts),
                     user_rtgts)
        zk_set = set([(id, fqns) for id, fqns in zk_all_rtgts.items()])
        cassandra_schema_set = set([(id, fqns) for id, fqns
                                    in cassandra_schema_all_rtgts.items()])
        cassandra_api_set = set([(id, fqns) for id, fqns
                                 in cassandra_api_all_rtgts.items()])

        return (zk_set, cassandra_schema_set, cassandra_schema_duplicate_rtgts,
                cassandra_api_set, cassandra_api_duplicate_rtgts, ret_errors)
    # end audit_route_targets_id

    def audit_security_groups_id(self):
        logger = self._logger
        ret_errors = []

        # read in security-group ids from zookeeper
        base_path = self.base_sg_id_zk_path
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
        sg_uuids = [x.split(':')[-1] for x, _ in
                    fq_name_table.xget('security_group')]
        cassandra_all_sgs = {}
        duplicate_sg_ids = {}
        for sg_uuid in sg_uuids:
            try:
                sg_cols = obj_uuid_table.get(
                    sg_uuid, columns=['prop:security_group_id', 'fq_name'])
            except pycassa.NotFoundException:
                continue
            sg_id = json.loads(sg_cols['prop:security_group_id'])
            sg_fq_name_str = ':'.join(json.loads(sg_cols['fq_name']))
            if sg_id in cassandra_all_sgs:
                if sg_id < SG_ID_MIN_ALLOC:
                    continue
                duplicate_sg_ids.setdefault(
                    sg_id - SG_ID_MIN_ALLOC, []).append(
                        (sg_fq_name_str, sg_uuid))
            else:
                cassandra_all_sgs[sg_id] = sg_fq_name_str

        logger.debug("Got %d security-groups with id", len(cassandra_all_sgs))
        zk_set = set([(id, fqns) for id, fqns in zk_all_sgs.items()])
        cassandra_set = set([(id - SG_ID_MIN_ALLOC, fqns)
                             for id, fqns in cassandra_all_sgs.items()
                             if id >= SG_ID_MIN_ALLOC])

        return zk_set, cassandra_set, ret_errors, duplicate_sg_ids
    # end audit_security_groups_id

    def audit_virtual_networks_id(self):
        logger = self._logger
        ret_errors = []

        # read in virtual-network ids from zookeeper
        base_path = self.base_vn_id_zk_path
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
        vn_uuids = [x.split(':')[-1] for x, _ in
                    fq_name_table.xget('virtual_network')]
        cassandra_all_vns = {}
        duplicate_vn_ids = {}
        for vn_uuid in vn_uuids:
            vn_cols = obj_uuid_table.get(vn_uuid,
                columns=['prop:virtual_network_properties', 'fq_name',
                         'prop:virtual_network_network_id'])
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
            if vn_id in cassandra_all_vns:
                duplicate_vn_ids.setdefault(vn_id, []).append(
                    (vn_fq_name_str, vn_uuid))
            else:
                cassandra_all_vns[vn_id] = vn_fq_name_str

        logger.debug("Got %d virtual-networks with id in Cassandra.",
            len(cassandra_all_vns))

        zk_set = set([(id, fqns) for id, fqns in zk_all_vns.items()])
        cassandra_set = set([(id, fqns) for id, fqns in cassandra_all_vns.items()])

        return zk_set, cassandra_set, ret_errors, duplicate_vn_ids
    # end audit_virtual_networks_id

    def _addr_alloc_process_ip_objects(self, cassandra_all_vns, ip_type,
                                       ip_uuids):
        logger = self._logger
        ret_errors = []

        if ip_type == 'instance-ip':
            addr_prop = 'prop:instance_ip_address'
            vn_is_ref = True
        elif ip_type == 'floating-ip':
            addr_prop = 'prop:floating_ip_address'
            vn_is_ref = False
        elif ip_type == 'alias-ip':
            addr_prop = 'prop:alias_ip_address'
            vn_is_ref = False
        else:
            raise Exception('Unknown ip type %s' % (ip_type))

        # walk vn fqn index, pick default-gw/dns-server-addr
        # and set as present in cassandra
        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        def set_reserved_addrs_in_cassandra(vn_id, fq_name_str):
            if fq_name_str in cassandra_all_vns:
                # already parsed and handled
                return

            # find all subnets on this VN and add for later check
            ipam_refs = obj_uuid_table.xget(vn_id,
                                            column_start='ref:network_ipam:',
                                            column_finish='ref:network_ipam;')
            if not ipam_refs:
                logger.debug('VN %s (%s) has no ipam refs', vn_id, fq_name_str)
                return

            cassandra_all_vns[fq_name_str] = {}
            for _, attr_json_dict in ipam_refs:
                attr_dict = json.loads(attr_json_dict)['attr']
                for subnet in attr_dict['ipam_subnets']:
                    sn_key = '%s/%s' % (subnet['subnet']['ip_prefix'],
                                        subnet['subnet']['ip_prefix_len'])
                    gw = subnet['default_gateway']
                    dns = subnet.get('dns_server_address')
                    cassandra_all_vns[fq_name_str][sn_key] = {
                        'start': subnet['subnet']['ip_prefix'],
                        'gw': gw,
                        'dns': dns,
                        'addrs': []}
        # end set_reserved_addrs_in_cassandra

        for fq_name_str_uuid, _ in obj_fq_name_table.xget('virtual_network'):
            fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
            vn_id = fq_name_str_uuid.split(':')[-1]
            set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
        # end for all VNs

        for ip_id in ip_uuids:

            # get addr
            ip_cols = dict(obj_uuid_table.xget(ip_id))
            if not ip_cols:
                errmsg = ('Missing object in uuid table for %s %s' %
                          (ip_type, ip_id))
                ret_errors.append(FQNStaleIndexError(errmsg))
                continue

            try:
                ip_addr = json.loads(ip_cols[addr_prop])
            except KeyError:
                errmsg = 'Missing ip addr in %s %s' % (ip_type, ip_id)
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
                vn_cols = obj_fq_name_table.get(
                    'virtual_network',
                    column_start='%s:' % (vn_fq_name_str),
                    column_finish='%s;' % (vn_fq_name_str))
                vn_id = vn_cols.keys()[0].split(':')[-1]

            if not vn_id:
                ret_errors.append(VirtualNetworkMissingError(
                    'Missing vn in %s %s.\n' % (ip_type, ip_id)))
                continue

            col = obj_uuid_table.get(vn_id, columns=['fq_name'])
            fq_name_str = ':'.join(json.loads(col['fq_name']))
            if fq_name_str not in cassandra_all_vns:
                msg = ("Found IP %s %s on VN %s (%s) thats not in FQ NAME "
                       "index" % (ip_type, ip_id, vn_id, fq_name_str))
                ret_errors.append(FQNIndexMissingError(msg))
                # find all subnets on this VN and add for later check
                set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
            # end first encountering vn

            for sn_key in cassandra_all_vns[fq_name_str]:
                addr_added = False
                subnet = cassandra_all_vns[fq_name_str][sn_key]
                if not IPAddress(ip_addr) in IPNetwork(sn_key):
                    continue
                cassandra_all_vns[fq_name_str][sn_key]['addrs'].append(
                    (ip_id, ip_addr))
                addr_added = True
                break
            # end for all subnets in vn

            if not addr_added:
                errmsg = 'Missing subnet for ip %s %s' % (ip_type, ip_id)
                ret_errors.append(IpSubnetMissingError(errmsg))
            # end handled the ip
        # end for all ip_uuids

        # Remove subnets without IP and network without subnets
        for vn_fq_name_str, sn_keys in cassandra_all_vns.items():
            for sn_key, subnet in sn_keys.items():
                if not subnet['addrs']:
                    del cassandra_all_vns[vn_fq_name_str][sn_key]
            if not cassandra_all_vns[vn_fq_name_str]:
                del cassandra_all_vns[vn_fq_name_str]

        return ret_errors

    def audit_subnet_addr_alloc(self):
        ret_errors = []
        logger = self._logger

        zk_all_vns = {}
        base_path = self.base_subnet_zk_path
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        num_addrs = 0
        try:
            subnets = self._zk_client.get_children(base_path)
        except kazoo.exceptions.NoNodeError:
            subnets = []
        for subnet in subnets:
            vn_fq_name_str = ':'.join(subnet.split(':', 3)[:-1])
            pfx = subnet.split(':', 3)[-1]
            zk_all_vns.setdefault(vn_fq_name_str, {})
            pfxlen_path = base_path + '/' + subnet
            pfxlen = self._zk_client.get_children(pfxlen_path)
            if not pfxlen:
                zk_all_vns[vn_fq_name_str][pfx] = []
                continue
            subnet_key = '%s/%s' % (pfx, pfxlen[0])
            zk_all_vns[vn_fq_name_str][subnet_key] = []
            addrs = self._zk_client.get_children(pfxlen_path+'/'+pfxlen[0])
            if not addrs:
                continue
            for addr in addrs:
                iip_uuid = self._zk_client.get(
                    pfxlen_path + '/' + pfxlen[0] + '/' + addr)
                if iip_uuid is not None:
                    zk_all_vns[vn_fq_name_str].setdefault(subnet_key, []).\
                        append((iip_uuid[0], str(IPAddress(int(addr)))))
            num_addrs += len(zk_all_vns[vn_fq_name_str][subnet_key])
        # end for all subnet paths
        logger.debug("Got %d networks %d addresses",
                     len(zk_all_vns), num_addrs)

        logger.debug("Reading instance/floating-ip objects from cassandra")
        cassandra_all_vns = {}
        num_addrs = 0
        fq_name_table = self._cf_dict['obj_fq_name_table']

        iip_rows = fq_name_table.xget('instance_ip')
        iip_uuids = [x.split(':')[-1] for x, _ in iip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, 'instance-ip', iip_uuids))

        num_addrs += len(iip_uuids)

        fip_rows = fq_name_table.xget('floating_ip')
        fip_uuids = [x.split(':')[-1] for x, _ in fip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, 'floating-ip', fip_uuids))

        num_addrs += len(fip_uuids)

        aip_rows = fq_name_table.xget('alias_ip')
        aip_uuids = [x.split(':')[-1] for x, _ in aip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, 'alias-ip', aip_uuids))

        num_addrs += len(aip_uuids)

        logger.debug("Got %d networks %d addresses",
                     len(cassandra_all_vns), num_addrs)

        return zk_all_vns, cassandra_all_vns, ret_errors

# end class DatabaseManager

class DatabaseChecker(DatabaseManager):
    def checker(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Checker %s: Success' % func.__name__)
                else:
                    self._logger.error(
                        'Checker %s: Failed:\n%s\n' %
                        (func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Checker %s: Exception, %s' %
                                       (func.__name__, err_msg))
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
            try:
                zk_client = kazoo.client.KazooClient(server)
                zk_client.start()
                self._logger.debug("Issuing 'stat' on %s: ", server)
                stat_out = zk_client.command('stat')
                self._logger.debug("Got: %s" %(stat_out))
                zk_client.stop()
                zk_client.close()
                stats[server] = stat_out
            except Exception as e:
                msg = "Cannot get stats on zk node %s: %s" % (server, str(e))
                ret_errors.append(ZkStandaloneError(msg))
            finally:
                zk_client = None

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
            try:
                sys_mgr = pycassa.SystemManager(server, credentials=cred)
            except Exception as e:
                msg = "Cannot connect to cassandra node %s: %s" % (server,
                                                                   str(e))
                ret_errors.append(CassWrongRFError(msg))
                continue

            for ks_name, _ in self._db_info:
                if self._api_args.cluster_id:
                    full_ks_name = '%s_%s' %(self._api_args.cluster_id, ks_name)
                else:
                    full_ks_name = ks_name
                logger.debug("Reading keyspace properties for %s on %s: ",
                             ks_name, server)
                ks_prop = sys_mgr.get_keyspace_properties(full_ks_name)
                logger.debug("Got %s", ks_prop)

                repl_factor = int(ks_prop['strategy_options']['replication_factor'])
                if (repl_factor != len(self._cassandra_servers)):
                    errmsg = 'Incorrect replication factor %d for keyspace %s' \
                             %(repl_factor, ks_name)
                    ret_errors.append(CassWrongRFError(errmsg))

        return ret_errors
    # end check_cassandra_keyspace_replication

    def check_rabbitmq_queue(self):
        pass
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
        for obj_type, _ in obj_fq_name_table.get_range(column_count=1):
            for fq_name_str_uuid, _ in obj_fq_name_table.xget(obj_type):
                fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
                fq_name_str = cfgm_common.utils.decode_string(fq_name_str)
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                fq_name_table_all.append((obj_type, fq_name_str, obj_uuid))
                try:
                    obj_cols = obj_uuid_table.get(obj_uuid,
                                                  columns=['fq_name'])
                except pycassa.NotFoundException:
                    ret_errors.append(FQNStaleIndexError(
                        'Missing object %s %s %s in uuid table'
                        %(obj_uuid, obj_type, fq_name_str)))
                    continue
                obj_fq_name_str = ':'.join(json.loads(obj_cols['fq_name']))
                if fq_name_str != obj_fq_name_str:
                    ret_errors.append(FQNMismatchError(
                        'Mismatched FQ Name %s (index) vs %s (object)' \
                               %(fq_name_str, obj_fq_name_str)))
            # end for all objects in a type
        # end for all obj types
        logger.debug("Got %d objects", len(fq_name_table_all))

        uuid_table_all = []
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, _ in obj_uuid_table.get_range(column_count=1):
            try:
                cols = obj_uuid_table.get(obj_uuid, columns=['type', 'fq_name'])
            except pycassa.NotFoundException:
                ret_errors.append( MandatoryFieldsMissingError(
                    'uuid %s cols %s' % (obj_uuid, cols)))
            obj_type = json.loads(cols['type'])
            fq_name_str = ':'.join(json.loads(cols['fq_name']))
            uuid_table_all.append((obj_type, fq_name_str, obj_uuid))

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
        for obj_uuid, _ in obj_uuid_table.get_range(column_count=1):
            cols = dict(obj_uuid_table.xget(obj_uuid))
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
        zk_all_vns, cassandra_all_vns, ret_errors =\
            self.audit_subnet_addr_alloc()

        # check for differences in networks
        extra_vn = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        if extra_vn:
            errmsg = 'Extra VN in zookeeper (vs. cassandra) for %s' \
                     % (str(extra_vn))
            ret_errors.append(ZkVNExtraError(errmsg))

        extra_vn = set(cassandra_all_vns.keys()) - set(zk_all_vns.keys())
        if extra_vn:
            errmsg = 'Missing VN in zookeeper (vs.cassandra) for %s' \
                     % (str(extra_vn))
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
            errmsg = 'Extra VN/SN in zookeeper for %s' % (extra_vn_sn)
            ret_errors.append(ZkSubnetExtraError(errmsg))

        extra_vn_sn = set(cassandra_all_vn_sn) - set(zk_all_vn_sn)
        if extra_vn_sn:
            errmsg = 'Missing VN/SN in zookeeper for %s' % (extra_vn_sn)
            ret_errors.append(ZkSubnetMissingError(errmsg))

        # check for differences in ip addresses
        for vn, sn_key in set(zk_all_vn_sn) & set(cassandra_all_vn_sn):
            sn_start = cassandra_all_vns[vn][sn_key]['start']
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            sn_dns = cassandra_all_vns[vn][sn_key]['dns']
            zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']
            extra_ips = set(zk_ips) - set(cassandra_ips)
            for iip_uuid, ip_addr in extra_ips:
                # ignore network, bcast and gateway ips
                if (IPAddress(ip_addr) == IPNetwork(sn_key).network or
                        IPAddress(ip_addr) == IPNetwork(sn_key).broadcast):
                    continue
                if (ip_addr == sn_gw_ip or ip_addr == sn_dns or
                        ip_addr == sn_start):
                    continue

                errmsg = ('Extra IP %s (IIP %s) in zookeeper for vn %s' %
                          (ip_addr, iip_uuid, vn))
                ret_errors.append(ZkIpExtraError(errmsg))
            # end all zk extra ips

            extra_ips = set(cassandra_ips) - set(zk_ips)
            for iip_uuid, ip_addr in extra_ips:
                errmsg = ('Missing IP %s (IIP %s) in zookeeper for vn %s' %
                          (ip_addr, iip_uuid, vn))
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
        zk_set, schema_set, schema_duplicate, api_set, api_duplicate, errors =\
            self.audit_route_targets_id()
        ret_errors.extend(errors)

        for vn_id, fq_name_uuids in schema_duplicate.items():
            errmsg = ("Duplicate RT ID in schema cassandra keyspace: '%s'"
                      "used for RT(s) %s already used by another one" %
                      (vn_id, fq_name_uuids))
            ret_errors.append(RTDuplicateIdError(errmsg))

        for vn_id, fq_name_uuids in api_duplicate.items():
            errmsg = ("Duplicate RT ID in API server cassandra keyspace: '%s'"
                      "used for RT(s) %s already used by another one" %
                      (vn_id, fq_name_uuids))
            ret_errors.append(RTDuplicateIdError(errmsg))

        extra_rtgt_ids = zk_set - schema_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = ("Extra route target ID in zookeeper for RTgt %s %s" %
                      (rtgt_fq_name_str, rtgt_id))
            ret_errors.append(ZkRTgtIdExtraError(errmsg))

        extra_rtgt_ids = schema_set - zk_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = ("Missing route target ID in zookeeper for RTgt %s %s" %
                      (rtgt_fq_name_str, rtgt_id))
            ret_errors.append(ZkRTgtIdMissingError(errmsg))

        extra_rtgt_ids = api_set - schema_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = ("Extra route target ID in API server cassandra "
                      "keyspace for RTgt %s %s" % (rtgt_fq_name_str, rtgt_id))
            ret_errors.append(CassRTgtIdExtraError(errmsg))

        extra_rtgt_ids = schema_set - api_set
        for rtgt_id, rtgt_fq_name_str in extra_rtgt_ids:
            errmsg = ("Missing route target ID in API server cassandra "
                      "keyspace for RTgt %s %s" % (rtgt_fq_name_str, rtgt_id))
            ret_errors.append(CassRTgtIdMissingError(errmsg))

        # read in virtual-networks from cassandra to find user allocated rtgts
        # and ensure they are not from auto-allocated range
        num_user_rtgts = 0
        num_bad_rtgts = 0
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading virtual-network objects from cassandra")
        vn_row = fq_name_table.xget('virtual_network')
        vn_uuids = [x.split(':')[-1] for x, _ in vn_row]
        for vn_id in vn_uuids:
            try:
                rtgt_list_json = obj_uuid_table.get(
                    vn_id,
                    columns=['prop:route_target_list'])\
                    ['prop:route_target_list']
                rtgt_list = json.loads(rtgt_list_json).get('route_target', [])
            except pycassa.NotFoundException:
                continue

            for rtgt in rtgt_list:
                rtgt_asn, rtgt_id = _parse_rt(rtgt)
                if rtgt_asn != self.global_asn or rtgt_id < RT_ID_MIN_ALLOC:
                    num_user_rtgts += 1
                    continue  # all good

                num_bad_rtgts += 1
                errmsg = 'Wrong route-target range in cassandra %d' % rtgt_id
                ret_errors.append(CassRTRangeError(errmsg))
            # end for all rtgt
        # end for all vns

        logger.debug("Got %d user configured route-targets, %d in bad range",
                     num_user_rtgts, num_bad_rtgts)
        return ret_errors
    # end check_route_targets_id

    @checker
    def check_virtual_networks_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, duplicate_ids =\
            self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        for vn_id, fq_name_uuids in duplicate_ids.items():
            errmsg = ("Duplicate VN ID: '%s' used for VN(s) %s already used "
                      "by another one" % (vn_id, fq_name_uuids))
            ret_errors.append(VNDuplicateIdError(errmsg))

        extra_vn_ids = zk_set - cassandra_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            errmsg = ('Extra VN IDs in zookeeper for vn %s %s' %
                      (vn_fq_name_str, vn_id))
            ret_errors.append(ZkVNIdExtraError(errmsg))

        extra_vn_ids = cassandra_set - zk_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            errmsg = ('Missing VN IDs in zookeeper for vn %s %s' %
                      (vn_fq_name_str, vn_id))
            ret_errors.append(ZkVNIdMissingError(errmsg))

        return ret_errors
    # end check_virtual_networks_id

    @checker
    def check_security_groups_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, duplicate_ids =\
            self.audit_security_groups_id()
        ret_errors.extend(errors)

        for sg_id, fq_name_uuids in duplicate_ids.items():
            errmsg = ("Duplicate SG ID: '%s' used for SG(s) %s already used "
                      "by another one" % (sg_id, fq_name_uuids))
            ret_errors.append(SGDuplicateIdError(errmsg))

        extra_sg_ids = zk_set - cassandra_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            errmsg = ('Extra SG IDs in zookeeper for sg %s %s' %
                      (sg_fq_name_str, sg_id))
            ret_errors.append(ZkSGIdExtraError(errmsg))

        extra_sg_ids = cassandra_set - zk_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            errmsg = ('Missing SG IDs in zookeeper for sg %s %s' %
                      (sg_fq_name_str, sg_id))
            ret_errors.append(ZkSGIdMissingError(errmsg))

        return ret_errors
    # end check_security_groups_id

    def check_schema_db_mismatch(self):
        # TODO detect all objects persisted that have discrepancy from
        # defined schema
        pass
    # end check_schema_db_mismatch
# end class DatabaseChecker


class DatabaseCleaner(DatabaseManager):
    def cleaner(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Cleaner %s: Success' % func.__name__)
                else:
                    self._logger.error(
                        'Cleaner %s: Failed:\n%s\n' %
                        (func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Cleaner %s: Exception, %s' %
                                       (func.__name__, err_msg))
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
        for obj_type, _ in obj_fq_name_table.get_range(column_count=1):
            stale_cols = []
            for fq_name_str_uuid, _ in obj_fq_name_table.xget(obj_type):
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                try:
                    obj_uuid_table.get(obj_uuid)
                except pycassa.NotFoundException:
                    logger.info("Found stale fq_name index entry: %s",
                                fq_name_str_uuid)
                    stale_cols.append(fq_name_str_uuid)

            if stale_cols:
                if not self._args.execute:
                    logger.info("Would removed stale fq_names: %s", stale_cols)
                else:
                    logger.info("Removing stale fq_names: %s", stale_cols)
                    obj_fq_name_table.remove(obj_type, columns=stale_cols)

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
        for obj_uuid, _ in obj_uuid_table.get_range(column_count=1):
            cols = dict(obj_uuid_table.xget(obj_uuid))
            missing_cols = set(self.OBJ_MANDATORY_COLUMNS) - set(cols.keys())
            if not missing_cols:
                continue
            logger.info("Found object %s with missing columns %s", obj_uuid,
                        missing_cols)
            if not self._args.execute:
                logger.info("Would removed object %s", obj_uuid)
            else:
                logger.info("Removing object %s", obj_uuid)
                obj_uuid_table.remove(obj_uuid)

        return ret_errors
    # end clean_obj_missing_mandatory_fields

    @cleaner
    def clean_vm_with_no_vmi(self):
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        stale_vm_uuids = []
        logger.debug("Reading all VMs from obj_uuid_table")
        for obj_uuid, _ in obj_uuid_table.get_range(column_count=1):
            cols = dict(obj_uuid_table.xget(obj_uuid))
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

        logger.debug("Total %s VMs with no VMIs", len(stale_vm_uuids))
        for vm_uuid in stale_vm_uuids:
            if not self._args.execute:
                logger.info("Would removed stale VM %s", vm_uuid)
            else:
                logger.info("Removing stale VM %s", vm_uuid)
                obj_uuid_table.remove(vm_uuid)

        return ret_errors
    # end clean_vm_with_no_vmi

    @cleaner
    def clean_stale_route_target_id(self):
        logger = self._logger
        ret_errors = []
        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        zk_set, schema_set, _, api_set, _, errors =\
            self.audit_route_targets_id()
        ret_errors.extend(errors)

        for id, _ in api_set - schema_set:
            fq_name_str = 'target:%d:%d' % (self.global_asn, id)
            cols = obj_fq_name_table.get(
                'route_target',
                column_start='%s:' % fq_name_str,
                column_finish='%s;' % fq_name_str)
            uuid = cols.keys()[0].split(':')[-1]
            fq_name_uuid_str = '%s:%s' % (fq_name_str, uuid)
            if not self._args.execute:
                logger.info("Would removed stale route target %s (%s) in API "
                            "server cassandra keyspace ", fq_name_str, uuid)
            else:
                logger.info("Removing stale route target %s (%s) in API "
                            "server cassandra keyspace ", fq_name_str, uuid)
                obj_uuid_table.remove(uuid)
                obj_fq_name_table.remove('route_target',
                                         columns=[fq_name_uuid_str])

        self._clean_zk_id_allocation(self.base_rtgt_id_zk_path,
                                     schema_set,
                                     zk_set,
                                     id_oper='%%s + %d' % RT_ID_MIN_ALLOC)

        return ret_errors
    # end clean_stale_route_target_id

    @cleaner
    def clean_stale_security_group_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, _ = self.audit_security_groups_id()
        ret_errors.extend(errors)

        self._clean_zk_id_allocation(self.base_sg_id_zk_path,
                                     cassandra_set,
                                     zk_set)

        return ret_errors
    # end clean_stale_security_group_id

    @cleaner
    def clean_stale_virtual_network_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, _ = self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        self._clean_zk_id_allocation(self.base_vn_id_zk_path,
                                     cassandra_set,
                                     zk_set,
                                     id_oper='%s - 1')

        return ret_errors
    # end clean_stale_virtual_network_id

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
            if not self._args.execute:
                logger.info("Would remove stale subnet uuid %s in useragent "
                            "keyspace", subnet_uuid)
                logger.info("Would remove stale subnet key %s in useragent "
                            "keyspace", subnet_key)
            else:
                logger.info("Removing stale subnet uuid %s in useragent "
                            "keyspace", subnet_uuid)
                ua_kv_cf.remove(subnet_uuid)
                logger.info("Removing stale subnet key %s in useragent "
                            "keyspace", subnet_key)
                ua_kv_cf.remove(ua_subnet_info[subnet_key])

        return ret_errors
    # end clean_stale_subnet_uuid

    def _remove_stale_from_uuid_table(self, dangle_prefix):
        logger = self._logger
        ret_errors = []

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, _ in obj_uuid_table.get_range(column_count=1):
            cols = dict(obj_uuid_table.xget(obj_uuid))
            obj_type = json.loads(cols.get('type', '"UnknownType"'))
            fq_name = json.loads(cols.get('fq_name', '"UnknownFQN"'))
            stale_cols = []
            for col_name in cols:
                if not col_name.startswith(dangle_prefix):
                    continue
                _, _, dangle_check_uuid = col_name.split(':')
                try:
                    obj_uuid_table.get(dangle_check_uuid)
                except pycassa.NotFoundException:
                    msg = ("Found stale %s index: %s in %s (%s %s)" %
                           (dangle_prefix, col_name, obj_uuid, obj_type,
                            fq_name))
                    logger.info(msg)
                    stale_cols.append(col_name)

            if stale_cols:
                if not self._args.execute:
                    logger.info("Would remove stale %s: %s", dangle_prefix,
                                stale_cols)
                else:
                    logger.info("Removing stale %s: %s", dangle_prefix,
                                stale_cols)
                    obj_uuid_table.remove(obj_uuid, columns=stale_cols)

        return ret_errors
    # end _remove_stale_from_uuid_table

    def _clean_zk_id_allocation(self, zk_path, cassandra_set, zk_set,
                                id_oper=None):
        logger = self._logger
        zk_path = '%s/%%s' % zk_path

        for id, fq_name_str in zk_set - cassandra_set:
            if id_oper is not None:
                id = eval(id_oper % id)
            id_str = "%(#)010d" % {'#': id}
            if not self._args.execute:
                logger.info("Would removed stale id %s for %s",
                            zk_path % id_str, fq_name_str)
            else:
                logger.info("Removing stale id %s for %s", zk_path % id_str,
                            fq_name_str)
                self._zk_client.delete(zk_path % id_str)

    @cleaner
    def clean_subnet_addr_alloc(self):
        logger = self._logger
        zk_all_vns, cassandra_all_vns, ret_errors =\
            self.audit_subnet_addr_alloc()
        zk_all_vn_sn = []
        for vn_key, vn in zk_all_vns.items():
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])
        cassandra_all_vn_sn = []
        for vn_key, vn in cassandra_all_vns.items():
            cassandra_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        # Clean extra net in zk
        extra_vn = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        for vn in extra_vn:
            for sn_key in zk_all_vns[vn]:
                path = '%s/%s:%s' % (self.base_subnet_zk_path, vn,
                                     str(IPNetwork(sn_key).network))
                if not self._args.execute:
                    logger.info("Would delete zk: %s", path)
                else:
                    logger.info("Deleting zk path: %s", path)
                    self._zk_client.delete(path, recursive=True)
            zk_all_vns.pop(vn, None)

        # Clean extra subnet in zk
        extra_vn_sn = set(zk_all_vn_sn) - set(cassandra_all_vn_sn)
        for vn, sn_key in extra_vn_sn:
            path = '%s/%s:%s' % (self.base_subnet_zk_path, vn, sn_key)
            if not self._args.execute:
                logger.info("Would delete zk: %s", path)
            else:
                logger.info("Deleting zk path: %s", path)
                self._zk_client.delete(path, recursive=True)
            if vn in zk_all_vns:
                zk_all_vns[vn].pop(sn_key, None)

        # Check for extra IP addresses in zk
        for vn, sn_key in cassandra_all_vn_sn:
            if vn not in zk_all_vns or sn_key not in zk_all_vns[vn]:
                zk_ips = []
            else:
                zk_ips = zk_all_vns[vn][sn_key]
            sn_start = cassandra_all_vns[vn][sn_key]['start']
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            sn_dns = cassandra_all_vns[vn][sn_key]['dns']
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']

            for ip_addr in set(zk_ips) - set(cassandra_ips):
                # ignore network, bcast and gateway ips
                if (IPAddress(ip_addr[1]) == IPNetwork(sn_key).network or
                        IPAddress(ip_addr[1]) == IPNetwork(sn_key).broadcast):
                    continue
                if (ip_addr[1] == sn_gw_ip or ip_addr[1] == sn_dns or
                        ip_addr[1] == sn_start):
                    continue

                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would delete zk: %s", path)
                else:
                    logger.info("Deleting zk path: %s", path)
                    self._zk_client.delete(path, recursive=True)
# end class DatabaseCleaner


class DatabaseHealer(DatabaseManager):
    def healer(func):
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('Healer %s: Success' % func.__name__)
                else:
                    self._logger.error(
                        'Healer %s: Failed:\n%s\n' %
                        (func.__name__, '\n'.join(e.msg for e in errors)))
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('Healer %s: Exception, %s' %
                                       (func.__name__, err_msg))
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
        for obj_uuid, cols in obj_uuid_table.get_range(
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
            if not self._args.execute:
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

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is parent row-key val is set of col-names
        fixups = {}
        for obj_uuid, cols in obj_uuid_table.get_range(column_count=1):
            cols = dict(obj_uuid_table.xget(obj_uuid, column_start='parent:',
                                            column_finish='parent;'))
            if not cols:
                continue # no parent
            if len(cols) > 1:
                logger.info('Multiple parents %s for %s', cols, obj_uuid)
                continue

            parent_uuid = cols.keys()[0].split(':')[-1]
            try:
                _ = obj_uuid_table.get(parent_uuid)
            except pycassa.NotFoundException:
                msg = "Missing parent %s for object %s" \
                    %(parent_uuid, obj_uuid)
                logger.info(msg)
                continue

            try:
                cols = obj_uuid_table.get(obj_uuid, columns=['type'])
            except pycassa.NotFoundException:
                logger.info("Missing type for object %s", obj_uuid)
                continue
            obj_type = json.loads(cols['type'])

            child_col = 'children:%s:%s' %(obj_type, obj_uuid)
            try:
                _ = obj_uuid_table.get(parent_uuid, columns=[child_col])
                # found it, this object is indexed by parent fine
                continue
            except pycassa.NotFoundException:
                msg = "Found missing children index %s for parent %s" \
                    %(child_col, parent_uuid)
                logger.info(msg)

            fixups.setdefault(parent_uuid, []).append(child_col)
        # for all objects in uuid table

        for parent_uuid in fixups:
            cols = list(fixups[parent_uuid])
            if not self._args.execute:
                logger.info("Would insert row/columns: %s %s",
                    parent_uuid, cols)
            else:
                logger.info("Inserting row/columns: %s %s",
                    parent_uuid, cols)
                obj_uuid_table.insert(parent_uuid,
                    columns=dict((x, json.dumps(None)) for x in cols))

        return ret_errors
    # end heal_children_index

    def heal_subnet_uuid(self):
        pass
    # end heal_subnet_uuid

    @healer
    def heal_route_targets_id(self):
        ret_errors = []

        zk_set, schema_set, _, _, _, errors =\
            self.audit_route_targets_id()
        ret_errors.extend(errors)

        # TODO: Create missing rt in api cassandra keyspace
        #       (schema_set - api_set)

        self._heal_zk_id_allocation(self.base_rtgt_id_zk_path,
                                    schema_set,
                                    zk_set,
                                    id_oper='%%s + %d' % RT_ID_MIN_ALLOC)

        return ret_errors
    # end heal_route_targets_id

    @healer
    def heal_virtual_networks_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, _ = self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        self._heal_zk_id_allocation(self.base_vn_id_zk_path,
                                    cassandra_set,
                                    zk_set,
                                    id_oper='%s - 1')

        return ret_errors
    # end heal_virtual_networks_id

    @healer
    def heal_security_groups_id(self):
        ret_errors = []

        zk_set, cassandra_set, errors, _ = self.audit_security_groups_id()
        ret_errors.extend(errors)

        self._heal_zk_id_allocation(self.base_sg_id_zk_path,
                                    cassandra_set,
                                    zk_set)

        return ret_errors
    # end heal_security_groups_id

    def _heal_zk_id_allocation(self, zk_path, cassandra_set, zk_set,
                               id_oper=None):
        logger = self._logger
        zk_path = '%s/%%s' % zk_path

        # Add missing IDs in zk
        for id, fq_name_str in cassandra_set - zk_set:
            if id_oper is not None:
                id = eval(id_oper % id)
            id_str = "%(#)010d" % {'#': id}
            if not self._args.execute:
                logger.info("Would add missing id %s for %s", zk_path % id_str,
                            fq_name_str)
            else:
                logger.info("Adding missing id %s for %s", zk_path % id_str,
                            fq_name_str)
                self._zk_client.create(zk_path % id_str, str(fq_name_str))

    @healer
    def heal_subnet_addr_alloc(self):
        logger = self._logger
        zk_all_vns, cassandra_all_vns, ret_errors =\
            self.audit_subnet_addr_alloc()
        zk_all_vn_sn = []
        for vn_key, vn in zk_all_vns.items():
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])
        cassandra_all_vn_sn = []
        for vn_key, vn in cassandra_all_vns.items():
            cassandra_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        # Re-create missing vn/subnet in zk
        for vn, sn_key in set(cassandra_all_vn_sn) - set(zk_all_vn_sn):
            for ip_addr in cassandra_all_vns[vn][sn_key]['addrs']:
                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would create zk: %s", path)
                else:
                    logger.info("Creating zk path: %s", path)
                    self._zk_client.create(path, ip_addr[0], makepath=True)

        # Re-create missing IP addresses in zk
        for vn, sn_key in cassandra_all_vn_sn:
            if vn not in zk_all_vns or sn_key not in zk_all_vns[vn]:
                zk_ips = []
            else:
                zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']

            for ip_addr in set(cassandra_ips) - set(zk_ips):
                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would create zk: %s", path)
                else:
                    logger.info("Creating zk path: %s", path)
                    self._zk_client.create(path, ip_addr[0], makepath=True)

        return ret_errors
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
    db_cleaner.clean_stale_route_target_id()
    db_cleaner.clean_stale_virtual_network_id()
    db_cleaner.clean_stale_security_group_id()
    db_cleaner.clean_subnet_addr_alloc()
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
    db_healer.heal_subnet_addr_alloc()
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
