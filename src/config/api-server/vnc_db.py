#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Layer that transforms VNC config objects to database representation
"""
from cfgm_common.zkclient import ZookeeperClient, IndexAllocator
from gevent import monkey
monkey.patch_all()
import gevent
import gevent.event

import time
from pprint import pformat

import socket
from netaddr import IPNetwork, IPAddress
from context import get_request

from cfgm_common.uve.vnc_api.ttypes import *
from cfgm_common import ignore_exceptions
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from cfgm_common.vnc_cassandra import VncCassandraClient
from vnc_rdbms import VncServerRDBMSClient
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.utils import cgitb_hook
from cfgm_common.utils import shareinfo_from_perms2
from cfgm_common import vnc_greenlets
from cfgm_common import SGID_MIN_ALLOC

import copy
from cfgm_common import jsonutils as json
import uuid
import datetime
import pycassa
import pycassa.util
import pycassa.cassandra.ttypes
from pycassa.system_manager import *
from pycassa.util import *

import os

from provision_defaults import *
from cfgm_common.exceptions import *
from vnc_quota import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import USERAGENT_KEYSPACE_NAME
from sandesh.traces.ttypes import DBRequestTrace, MessageBusNotifyTrace

@ignore_exceptions
def get_trace_id():
    try:
        req_id = gevent.getcurrent().trace_request_id
    except Exception:
        req_id = 'req-%s' % str(uuid.uuid4())
        gevent.getcurrent().trace_request_id = req_id

    return req_id
# end get_trace_id

@ignore_exceptions
def trace_msg(trace_objs=[], trace_name='', sandesh_hdl=None, error_msg=None):
    for trace_obj in trace_objs:
        if error_msg:
            trace_obj.error = error_msg
        trace_obj.trace_msg(name=trace_name, sandesh=sandesh_hdl)
# end trace_msg


class VncServerCassandraClient(VncCassandraClient):
    # Useragent datastore keyspace + tables (used by neutron plugin currently)
    _USERAGENT_KEYSPACE_NAME = USERAGENT_KEYSPACE_NAME
    _USERAGENT_KV_CF_NAME = 'useragent_keyval_table'

    @classmethod
    def get_db_info(cls):
        db_info = VncCassandraClient.get_db_info() + \
                  [(cls._USERAGENT_KEYSPACE_NAME, [cls._USERAGENT_KV_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, db_client_mgr, cass_srv_list, reset_config, db_prefix,
                      cassandra_credential, walk, obj_cache_entries,
                      obj_cache_exclude_types):
        self._db_client_mgr = db_client_mgr
        keyspaces = self._UUID_KEYSPACE.copy()
        keyspaces[self._USERAGENT_KEYSPACE_NAME] = {
            self._USERAGENT_KV_CF_NAME: {}}
        super(VncServerCassandraClient, self).__init__(
            cass_srv_list, db_prefix, keyspaces, None, self.config_log,
            generate_url=db_client_mgr.generate_url, reset_config=reset_config,
            credential=cassandra_credential, walk=walk,
            obj_cache_entries=obj_cache_entries,
            obj_cache_exclude_types=obj_cache_exclude_types)
    # end __init__

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)
    # end config_log

    def prop_collection_update(self, obj_type, obj_uuid, updates):
        obj_class = self._db_client_mgr.get_resource_class(obj_type)
        bch = self._obj_uuid_cf.batch()
        for oper_param in updates:
            oper = oper_param['operation']
            prop_name = oper_param['field']
            if prop_name in obj_class.prop_list_fields:
                if oper == 'add':
                    prop_elem_val = oper_param['value']
                    prop_elem_pos = oper_param.get('position') or str(uuid.uuid4())
                    self._add_to_prop_list(bch, obj_uuid,
                        prop_name, prop_elem_val, prop_elem_pos)
                elif oper == 'modify':
                    prop_elem_val = oper_param['value']
                    prop_elem_pos = oper_param['position']
                    # modify is practically an insert so use add
                    self._add_to_prop_list(bch, obj_uuid,
                        prop_name, prop_elem_val, prop_elem_pos)
                elif oper == 'delete':
                    prop_elem_pos = oper_param['position']
                    self._delete_from_prop_list(bch, obj_uuid,
                        prop_name, prop_elem_pos)
            elif prop_name in obj_class.prop_map_fields:
                key_name = obj_class.prop_map_field_key_names[prop_name]
                if oper == 'set':
                    prop_elem_val = oper_param['value']
                    position = prop_elem_val[key_name]
                    self._set_in_prop_map(bch, obj_uuid,
                        prop_name, prop_elem_val, position)
                elif oper == 'delete':
                    position = oper_param['position']
                    self._delete_from_prop_map(bch, obj_uuid,
                        prop_name, position)
        # end for all updates

        self.update_last_modified(bch, obj_type, obj_uuid)
        bch.send()
    # end prop_collection_update

    def ref_update(self, obj_type, obj_uuid, ref_obj_type, ref_uuid,
                   ref_data, operation):
        bch = self._obj_uuid_cf.batch()
        if operation == 'ADD':
            self._create_ref(bch, obj_type, obj_uuid, ref_obj_type, ref_uuid,
                             ref_data)
        elif operation == 'DELETE':
            self._delete_ref(bch, obj_type, obj_uuid, ref_obj_type, ref_uuid)
        else:
            pass
        self.update_last_modified(bch, obj_type, obj_uuid)
        bch.send()
    # end ref_update

    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        bch = self._obj_uuid_cf.batch()
        self._relax_ref_for_delete(bch, obj_uuid, ref_uuid)
        bch.send()
    # end ref_relax_for_delete

    def _relax_ref_for_delete(self, bch, obj_uuid, ref_uuid):
        send = False
        if bch is None:
            send = True
            bch = self._obj_uuid_cf.batch()
        bch.insert(ref_uuid, {'relaxbackref:%s' % (obj_uuid):
                               json.dumps(None)})
        if send:
            bch.send()
    # end _relax_ref_for_delete

    def get_relaxed_refs(self, obj_uuid):
        relaxed_cols = self.get(self._OBJ_UUID_CF_NAME, obj_uuid,
                                start='relaxbackref:', finish='relaxbackref;')
        if not relaxed_cols:
            return []

        return [col.split(':')[1] for col in relaxed_cols]
    # end get_relaxed_refs

    def is_latest(self, id, tstamp):
        id_perms = self.uuid_to_obj_perms(id)
        if id_perms['last_modified'] == tstamp:
            return True
        else:
            return False
    # end is_latest

    # Insert new perms. Called on startup when walking DB
    def update_perms2(self, obj_uuid):
        bch = self._obj_uuid_cf.batch()
        perms2 = copy.deepcopy(Provision.defaults.perms2)
        perms2_json = json.dumps(perms2, default=lambda o: dict((k, v)
                               for k, v in o.__dict__.iteritems()))
        perms2 = json.loads(perms2_json)
        self._update_prop(bch, obj_uuid, 'perms2', {'perms2': perms2})
        bch.send()
        return perms2

    def enable_domain_sharing(self, obj_uuid, perms2):
        share_item = {
            'tenant': 'domain:%s' % obj_uuid,
            'tenant_access': cfgm_common.DOMAIN_SHARING_PERMS
        }
        perms2['share'].append(share_item)
        bch = self._obj_uuid_cf.batch()
        self._update_prop(bch, obj_uuid, 'perms2', {'perms2': perms2})
        bch.send()

    def uuid_to_obj_dict(self, id):
        obj_cols = self.get(self._OBJ_UUID_CF_NAME, id)
        if not obj_cols:
            raise NoIdError(id)
        return obj_cols
    # end uuid_to_obj_dict

    def uuid_to_obj_perms(self, id):
        return self.get_one_col(self._OBJ_UUID_CF_NAME, id, 'prop:id_perms')
    # end uuid_to_obj_perms

    # fetch perms2 for an object
    def uuid_to_obj_perms2(self, id):
        return self.get_one_col(self._OBJ_UUID_CF_NAME, id, 'prop:perms2')
    # end uuid_to_obj_perms2

    def useragent_kv_store(self, key, value):
        columns = {'value': value}
        self.add(self._USERAGENT_KV_CF_NAME, key, columns)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        if key:
            if isinstance(key, list):
                rows = self.multiget(self._USERAGENT_KV_CF_NAME, key)
                return [rows[row].get('value') for row in rows]
            else:
                row = self.get(self._USERAGENT_KV_CF_NAME, key)
                if not row:
                    raise NoUserAgentKey
                return row.get('value')
        else:  # no key specified, return entire contents
            kv_list = []
            for ua_key, ua_cols in self.get_range(self._USERAGENT_KV_CF_NAME):
                kv_list.append({'key': ua_key, 'value': ua_cols.get('value')})
            return kv_list
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        if not self.delete(self._USERAGENT_KV_CF_NAME, key):
            raise NoUserAgentKey
    # end useragent_kv_delete

# end class VncCassandraClient


class VncServerKombuClient(VncKombuClient):
    def __init__(self, db_client_mgr, rabbit_ip, rabbit_port,
                 rabbit_user, rabbit_password, rabbit_vhost, rabbit_ha_mode,
                 rabbit_health_check_interval, **kwargs):
        self._db_client_mgr = db_client_mgr
        self._sandesh = db_client_mgr._sandesh
        listen_port = db_client_mgr.get_server_port()
        q_name = 'vnc_config.%s-%s' % (socket.gethostname(), listen_port)
        super(VncServerKombuClient, self).__init__(
            rabbit_ip, rabbit_port, rabbit_user, rabbit_password, rabbit_vhost,
            rabbit_ha_mode, q_name, self._dbe_subscribe_callback,
            self.config_log, heartbeat_seconds=rabbit_health_check_interval,
            **kwargs)

    # end __init__

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)
    # end config_log

    @ignore_exceptions
    def _generate_msgbus_notify_trace(self, oper_info):
        req_id = oper_info.get('request-id',
            'req-%s' %(str(uuid.uuid4())))
        gevent.getcurrent().trace_request_id = req_id

        notify_trace = MessageBusNotifyTrace(request_id=req_id)
        notify_trace.operation = oper_info.get('oper', '')
        notify_trace.body = json.dumps(oper_info)

        return notify_trace
    # end _generate_msgbus_notify_trace

    def _dbe_subscribe_callback(self, oper_info):
        self._db_client_mgr.wait_for_resync_done()
        try:
            msg = "Notification Message: %s" %(pformat(oper_info))
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            trace = self._generate_msgbus_notify_trace(oper_info)

            self._db_client_mgr.dbe_uve_trace(**oper_info)
            if oper_info['oper'] == 'CREATE':
                self._dbe_create_notification(oper_info)
            elif oper_info['oper'] == 'UPDATE':
                self._dbe_update_notification(oper_info)
            elif oper_info['oper'] == 'DELETE':
                self._dbe_delete_notification(oper_info)

            trace_msg([trace], 'MessageBusNotifyTraceBuf', self._sandesh)
        except Exception:
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            errmsg = string_buf.getvalue()
            self.config_log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)
            trace_msg([trace], name='MessageBusNotifyTraceBuf',
                      sandesh=self._sandesh, error_msg=errmsg)
    # end _dbe_subscribe_callback

    def dbe_publish(self, oper, obj_type, obj_id, fq_name, obj_dict=None):
        req_id = get_trace_id()
        oper_info = {
            'request-id': req_id,
            'oper': oper,
            'type': obj_type,
            'uuid': obj_id,
            'fq_name': fq_name,
        }
        if obj_dict is not None:
            oper_info['obj_dict'] = obj_dict
        self.publish(oper_info)

    def _dbe_create_notification(self, obj_info):
        obj_type = obj_info['type']
        obj_uuid = obj_info['uuid']

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_type)
            if r_class:
                r_class.dbe_create_notification(self._db_client_mgr, obj_uuid)
        except Exception as e:
            err_msg = ("Failed in dbe_create_notification " + str(e))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
            raise
    # end _dbe_create_notification

    def _dbe_update_notification(self, obj_info):
        obj_type = obj_info['type']
        obj_uuid = obj_info['uuid']

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_type)
            if r_class:
                r_class.dbe_update_notification(obj_uuid)
        except Exception as e:
            msg = "Failure in dbe_update_notification" + str(e)
            self.config_log(msg, level=SandeshLevel.SYS_ERR)
            raise
    # end _dbe_update_notification

    def _dbe_delete_notification(self, obj_info):
        obj_type = obj_info['type']
        obj_uuid = obj_info['uuid']
        obj_dict = obj_info['obj_dict']

        db_client_mgr = self._db_client_mgr
        db_client_mgr._object_db.cache_uuid_to_fq_name_del(obj_uuid)

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_type)
            if r_class:
                r_class.dbe_delete_notification(obj_uuid, obj_dict)
        except Exception as e:
            msg = "Failure in dbe_delete_notification" + str(e)
            self.config_log(msg, level=SandeshLevel.SYS_ERR)
            raise
    # end _dbe_delete_notification

# end class VncKombuClient


class VncZkClient(object):
    _SUBNET_PATH = "/api-server/subnets"
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid"
    _MAX_SUBNET_ADDR_ALLOC = 65535

    _VN_ID_ALLOC_PATH = "/id/virtual-networks/"
    _VN_MAX_ID = 1 << 24

    _SG_ID_ALLOC_PATH = "/id/security-groups/id/"
    _SG_MAX_ID = 1 << 32

    def __init__(self, instance_id, zk_server_ip, reset_config, db_prefix,
                 sandesh_hdl):
        self._db_prefix = db_prefix
        if db_prefix:
            client_pfx = db_prefix + '-'
            zk_path_pfx = db_prefix + '/'
        else:
            client_pfx = ''
            zk_path_pfx = ''

        client_name = '%sapi-%s' %(client_pfx, instance_id)
        self._subnet_path = zk_path_pfx + self._SUBNET_PATH
        self._fq_name_to_uuid_path = zk_path_pfx + self._FQ_NAME_TO_UUID_PATH
        _vn_id_alloc_path = zk_path_pfx + self._VN_ID_ALLOC_PATH
        _sg_id_alloc_path = zk_path_pfx + self._SG_ID_ALLOC_PATH
        self._zk_path_pfx = zk_path_pfx

        self._sandesh = sandesh_hdl
        self._reconnect_zk_greenlet = None
        while True:
            try:
                self._zk_client = ZookeeperClient(client_name, zk_server_ip,
                                                  self._sandesh)
                # set the lost callback to always reconnect
                self._zk_client.set_lost_cb(self.reconnect_zk)
                break
            except gevent.event.Timeout as e:
                pass

        if reset_config:
            self._zk_client.delete_node(self._subnet_path, True)
            self._zk_client.delete_node(self._fq_name_to_uuid_path, True)
            self._zk_client.delete_node(_vn_id_alloc_path, True)
            self._zk_client.delete_node(_sg_id_alloc_path, True)

        self._subnet_allocators = {}

        # Initialize the virtual network ID allocator
        self._vn_id_allocator = IndexAllocator(self._zk_client,
                                               _vn_id_alloc_path,
                                               self._VN_MAX_ID)

        # Initialize the security group ID allocator
        self._sg_id_allocator = IndexAllocator(self._zk_client,
                                               _sg_id_alloc_path,
                                               self._SG_MAX_ID)
        # 0 is not a valid sg id any more. So, if it was previously allocated,
        # delete it and reserve it
        if self._sg_id_allocator.read(0) != '__reserved__':
            self._sg_id_allocator.delete(0)
        self._sg_id_allocator.reserve(0, '__reserved__')
    # end __init__

    def master_election(self, path, func, *args):
        self._zk_client.master_election(
            self._zk_path_pfx + path, os.getpid(),
            func, *args)
    # end master_election

    def _reconnect_zk(self):
        self._zk_client.connect()
        self._reconnect_zk_greenlet = None
    # end

    def reconnect_zk(self):
        if self._reconnect_zk_greenlet is None:
            self._reconnect_zk_greenlet =\
                   vnc_greenlets.VncGreenlet("VNC ZK Reconnect",
                                             self._reconnect_zk)
    # end

    def create_subnet_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        # TODO handle subnet resizing change, ignore for now
        if subnet not in self._subnet_allocators:
            if addr_from_start is None:
                addr_from_start = False
            self._subnet_allocators[subnet] = IndexAllocator(
                self._zk_client, self._subnet_path+'/'+subnet+'/',
                size=size/alloc_unit, start_idx=start_subnet/alloc_unit,
                reverse=not addr_from_start,
                alloc_list=[{'start': x['start']/alloc_unit, 'end':x['end']/alloc_unit}
                            for x in subnet_alloc_list],
                max_alloc=self._MAX_SUBNET_ADDR_ALLOC/alloc_unit)
    # end create_subnet_allocator

    def delete_subnet_allocator(self, subnet):
        self._subnet_allocators.pop(subnet, None)
        IndexAllocator.delete_all(self._zk_client,
                                  self._subnet_path+'/'+subnet+'/')
    # end delete_subnet_allocator

    def _get_subnet_allocator(self, subnet):
        return self._subnet_allocators.get(subnet)
    # end _get_subnet_allocator

    def subnet_is_addr_allocated(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.read(addr)
    # end subnet_is_addr_allocated

    def subnet_set_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.set_in_use(addr)
    # end subnet_set_in_use

    def subnet_reset_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.reset_in_use(addr)
    # end subnet_reset_in_use

    def subnet_reserve_req(self, subnet, addr, value):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.reserve(addr, value)
    # end subnet_reserve_req

    def subnet_alloc_count(self, subnet):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.get_alloc_count()
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None):
        allocator = self._get_subnet_allocator(subnet)
        try:
            return allocator.alloc(value=value)
        except ResourceExhaustionError:
            return None
    # end subnet_alloc_req

    def subnet_free_req(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        if allocator:
            allocator.delete(addr)
    # end subnet_free_req

    def create_fq_name_to_uuid_mapping(self, obj_type, fq_name, id):
        fq_name_str = ':'.join(fq_name)
        zk_path = self._fq_name_to_uuid_path+'/%s:%s' %(obj_type, fq_name_str)
        self._zk_client.create_node(zk_path, id)
    # end create_fq_name_to_uuid_mapping

    def get_fq_name_to_uuid_mapping(self, obj_type, fq_name):
        fq_name_str = ':'.join(fq_name)
        zk_path = self._fq_name_to_uuid_path+'/%s:%s' %(obj_type, fq_name_str)
        obj_uuid, znode_stat = self._zk_client.read_node(
            zk_path, include_timestamp=True)

        return obj_uuid, znode_stat.ctime
    # end get_fq_name_to_uuid_mapping

    def delete_fq_name_to_uuid_mapping(self, obj_type, fq_name):
        fq_name_str = ':'.join(fq_name)
        zk_path = self._fq_name_to_uuid_path+'/%s:%s' %(obj_type, fq_name_str)
        self._zk_client.delete_node(zk_path)
    # end delete_fq_name_to_uuid_mapping

    def is_connected(self):
        return self._zk_client.is_connected()
    # end is_connected

    def alloc_vn_id(self, name):
        if name is not None:
            return self._vn_id_allocator.alloc(name)

    def free_vn_id(self, vn_id):
        if vn_id is not None and vn_id < self._VN_MAX_ID:
            self._vn_id_allocator.delete(vn_id)

    def get_vn_from_id(self, vn_id):
        if vn_id is not None and vn_id < self._VN_MAX_ID:
            return self._vn_id_allocator.read(vn_id)

    def alloc_sg_id(self, name):
        if name is not None:
            return self._sg_id_allocator.alloc(name) + SGID_MIN_ALLOC

    def free_sg_id(self, sg_id):
        if (sg_id is not None and
                sg_id > SGID_MIN_ALLOC and
                sg_id < self._SG_MAX_ID):
            self._sg_id_allocator.delete(sg_id - SGID_MIN_ALLOC)

    def get_sg_from_id(self, sg_id):
        if (sg_id is not None and
                sg_id > SGID_MIN_ALLOC and
                sg_id < self._SG_MAX_ID):
            return self._sg_id_allocator.read(sg_id - SGID_MIN_ALLOC)
# end VncZkClient


class VncDbClient(object):
    def __init__(self, api_svr_mgr, db_srv_list, rabbit_servers, rabbit_port,
                 rabbit_user, rabbit_password, rabbit_vhost, rabbit_ha_mode,
                 reset_config=False, zk_server_ip=None, db_prefix='',
                 db_credential=None, obj_cache_entries=0,
                 obj_cache_exclude_types=None, db_engine='cassandra',
                 connection=None, **kwargs):
        self._db_engine = db_engine
        self._api_svr_mgr = api_svr_mgr
        self._sandesh = api_svr_mgr._sandesh

        self._UVEMAP = {
            "virtual_network" : ("ObjectVNTable", False),
            "virtual_machine" : ("ObjectVMTable", False),
            "virtual_machine_interface" : ("ObjectVMITable", False),
            "service_instance" : ("ObjectSITable", False),
            "virtual_router" : ("ObjectVRouter", True),
            "analytics_node" : ("ObjectCollectorInfo", True),
            "database_node" : ("ObjectDatabaseInfo", True),
            "config_node" : ("ObjectConfigNode", True),
            "service_chain" : ("ServiceChain", False),
            "physical_router" : ("ObjectPRouter", True),
            "bgp_router": ("ObjectBgpRouter", True),
        }

        self._db_resync_done = gevent.event.Event()

        msg = "Connecting to zookeeper on %s" % (zk_server_ip)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        if db_engine == 'cassandra':
            self._zk_db = VncZkClient(api_svr_mgr.get_worker_id(), zk_server_ip,
                                      reset_config, db_prefix, self.config_log)
            def db_client_init():
                msg = "Connecting to database on %s" % (db_srv_list)
                self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

                if api_svr_mgr.get_worker_id() == 0:
                    walk = False # done as part of db_resync()
                else:
                    walk = True

                self._object_db = VncServerCassandraClient(
                    self, db_srv_list, reset_config, db_prefix,
                    db_credential, walk, obj_cache_entries,
                    obj_cache_exclude_types)

            self._zk_db.master_election("/api-server-election", db_client_init)
        elif db_engine == 'rdbms':
            self._object_db = VncServerRDBMSClient(self,
                server_list=db_srv_list, reset_config=reset_config,
                generate_url=self.generate_url,
                connection=connection,
                db_prefix=db_prefix, credential=db_credential)
            self._zk_db = self._object_db

        self._msgbus = VncServerKombuClient(self, rabbit_servers,
            rabbit_port, rabbit_user, rabbit_password,
            rabbit_vhost, rabbit_ha_mode,
            api_svr_mgr.get_rabbit_health_check_interval(),
            **kwargs)
    # end __init__

    def _update_default_quota(self):
        """ Read the default quotas from the configuration
        and update it in the project object if not already
        updated.
        """
        default_quota = QuotaHelper.default_quota

        proj_id = self.fq_name_to_uuid('project',
                                       ['default-domain', 'default-project'])
        try:
            (ok, result) = self.dbe_read('project', proj_id)
        except NoIdError as e:
            ok = False
            result = 'Project Not Found: %s' %(proj_id)
        if not ok:
            self.config_log("Updating default quota failed: %s." %(result),
                level=SandeshLevel.SYS_ERR)
            return

        proj_dict = result
        proj_dict['quota'] = default_quota
        self.dbe_update('project', proj_id, proj_dict)
    # end _update_default_quota

    def get_api_server(self):
        return self._api_svr_mgr
    # end get_api_server

    def db_resync(self):
        # Read contents from cassandra and perform DB update if required
        start_time = datetime.datetime.utcnow()
        self._object_db.walk(self._dbe_resync)
        self.config_log("Cassandra DB walk completed.",
            level=SandeshLevel.SYS_INFO)
        self._update_default_quota()
        end_time = datetime.datetime.utcnow()
        msg = "Time elapsed in resyncing db: %s" % (str(end_time - start_time))
        self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        self._db_resync_done.set()
    # end db_resync

    def wait_for_resync_done(self):
        self._db_resync_done.wait()
    # end wait_for_resync_done

    def db_check(self):
        # Read contents from cassandra and report any read exceptions
        check_results = self._object_db.walk(self._dbe_check)

        return check_results
    # end db_check

    def db_read(self):
        # Read contents from cassandra
        read_results = self._object_db.walk(self._dbe_read)
        return read_results

    # end db_check

    def _uuid_to_longs(self, id):
        msb_id = id.int >> 64
        lsb_id = id.int & ((1 << 64) - 1)
        return {'uuid_mslong': msb_id, 'uuid_lslong': lsb_id}
    # end _uuid_to_longs

    def set_uuid(self, obj_type, obj_dict, id, do_lock=True):
        if do_lock:
            # set the mapping from name to uuid in zk to ensure single creator
            fq_name = obj_dict['fq_name']
            try:
                self._zk_db.create_fq_name_to_uuid_mapping(obj_type, fq_name,
                    str(id))
            except ResourceExistsError as rexist:
                # see if stale and if so delete stale
                _, ctime = self._zk_db.get_fq_name_to_uuid_mapping(
                                       obj_type, fq_name)
                epoch_msecs = ctime
                try:
                    self._object_db.uuid_to_fq_name(str(id))
                    # not stale
                    raise ResourceExistsError(fq_name, str(id), 'cassandra')
                except NoIdError:
                    lock_msecs = float(time.time()*1000 - epoch_msecs)
                    stale_msecs_cfg = 1000 * float(
                        self._api_svr_mgr.get_args().stale_lock_seconds)
                    if (lock_msecs < stale_msecs_cfg):
                        # not stale, race in create
                        raise rexist

                    # stale, release old and create new lock
                    msg = 'Releasing stale lock(%s sec) for %s %s' \
                        %(float(lock_msecs)/1000, obj_type, fq_name)
                    self.config_log(msg, level=SandeshLevel.SYS_NOTICE)
                    self._zk_db.delete_fq_name_to_uuid_mapping(
                        obj_type, fq_name)
                    self._zk_db.create_fq_name_to_uuid_mapping(
                        obj_type, fq_name, str(id))
        # end do_lock

        # set uuid in id_perms
        obj_dict['id_perms']['uuid'] = self._uuid_to_longs(id)
        obj_dict['uuid'] = str(id)

        return True
    # end set_uuid

    def _alloc_set_uuid(self, obj_type, obj_dict):
        id = uuid.uuid4()
        ok = self.set_uuid(obj_type, obj_dict, id)
        return (ok, obj_dict['uuid'])
    # end _alloc_set_uuid

    def match_uuid(self, obj_dict, obj_uuid):
        new_uuid = self._uuid_to_longs(uuid.UUID(obj_uuid))
        return (new_uuid == obj_dict['id_perms']['uuid'])
    # end match_uuid

    def update_subnet_uuid(self, subnets):
        updated = False
        if subnets is None:
            return updated

        for subnet in subnets:
            if subnet.get('subnet_uuid'):
                continue
            subnet_uuid = str(uuid.uuid4())
            subnet['subnet_uuid'] = subnet_uuid
            updated = True

        return updated
    # end update_subnet_uuid

    def update_bgp_router_type(self, obj_dict):
        """ Sets router_type property based on the vendor property only
        if router_type is not set.
        """
        router_params = obj_dict['bgp_router_parameters']
        if 'router_type' not in router_params:
            router_type = 'router'
            if router_params['vendor'] == 'contrail':
                router_type = 'control-node'
            router_params.update({'router_type': router_type})
            obj_uuid = obj_dict.get('uuid')
            self._object_db.object_update('bgp_router', obj_uuid, obj_dict)
    # end update_bgp_router_type

    def iip_update_subnet_uuid(self, iip_dict):
        """ Set the subnet uuid as instance-ip attribute """
        for vn_ref in iip_dict.get('virtual_network_refs', []):
            (ok, results) = self._object_db.object_read(
                'virtual_network', [vn_ref['uuid']],
                field_names=['network_ipam_refs'])
            if not ok:
                return
            vn_dict = results[0]
            for ipam in vn_dict.get('network_ipam_refs', []):
                ipam_subnets = ipam['attr']['ipam_subnets']

                for ipam_subnet in ipam_subnets:
                    if 'subnet' not in ipam_subnet or\
                            ipam_subnet['subnet'] is None:
                        # Ipam subnet info need not have ip/prefix info,
                        # instead they could hold the uuid of subnet info.
                        continue
                    pfx = ipam_subnet['subnet']['ip_prefix']
                    pfx_len = ipam_subnet['subnet']['ip_prefix_len']
                    cidr = '%s/%s' % (pfx, pfx_len)
                    if (IPAddress(iip_dict['instance_ip_address']) in
                            IPNetwork(cidr)):
                        iip_dict['subnet_uuid'] = ipam_subnet['subnet_uuid']
                        self._object_db.object_update('instance-ip',
                                                          iip_dict['uuid'],
                                                          iip_dict)
                        return

    def _dbe_resync(self, obj_type, obj_uuids):
        obj_class = cfgm_common.utils.obj_type_to_vnc_class(obj_type, __name__)
        obj_fields = list(obj_class.prop_fields) + list(obj_class.ref_fields)
        (ok, obj_dicts) = self._object_db.object_read(
                               obj_type, obj_uuids, field_names=obj_fields)
        uve_trace_list = []
        for obj_dict in obj_dicts:
            try:
                obj_uuid = obj_dict['uuid']
                uve_trace_list.append(("RESYNC", obj_type, obj_uuid, obj_dict))

                if obj_type == 'virtual_network':
                    # TODO remove backward compat (use RT instead of VN->LR ref)
                    for router in obj_dict.get('logical_router_refs', []):
                        self._object_db._delete_ref(None,
                                                       obj_type,
                                                       obj_uuid,
                                                       'logical_router',
                                                       router['uuid'])
                    if 'network_ipam_refs' in obj_dict:
                        ipam_refs = obj_dict['network_ipam_refs']
                        do_update = False
                        for ipam in ipam_refs:
                            vnsn = ipam['attr']
                            ipam_subnets = vnsn['ipam_subnets']
                            if (self.update_subnet_uuid(ipam_subnets)):
                                if not do_update:
                                    do_update = True
                        if do_update:
                            self.cassandra_db.object_update(
                                'virtual_network', obj_uuid, obj_dict)

                elif obj_type == 'virtual_machine_interface':
                    device_owner = obj_dict.get('virtual_machine_interface_device_owner')
                    li_back_refs = obj_dict.get('logical_interface_back_refs', [])
                    if not device_owner and li_back_refs:
                        obj_dict['virtual_machine_interface_device_owner'] = 'PhysicalRouter'
                        self._object_db.object_update('virtual_machine_interface',
                                                      obj_uuid, obj_dict)
                elif obj_type == 'access_control_list':
                    if not obj_dict.get('access_control_list_hash'):
                        rules = obj_dict.get('access_control_list_entries')
                        if rules:
                            rules_obj = AclEntriesType(params_dict=rules)
                            obj_dict['access_control_list_hash'] = hash(rules_obj)
                            self._object_db.object_update('access_control_list',
                                                          obj_uuid, obj_dict)

                # create new perms if upgrading
                perms2 = obj_dict.get('perms2')
                if perms2 is None:
                    perms2 = self._object_db.update_perms2(obj_uuid)
                if obj_type == 'domain' and len(perms2['share']) == 0:
                    self._object_db.enable_domain_sharing(obj_uuid, perms2)

                if (obj_type == 'bgp_router' and
                        'bgp_router_parameters' in obj_dict and
                        'router_type' not in obj_dict['bgp_router_parameters']):
                    self.update_bgp_router_type(obj_dict)

                if obj_type == 'instance_ip' and 'subnet_uuid' not in obj_dict:
                    self.iip_update_subnet_uuid(obj_dict)
            except Exception as e:
                tb = cfgm_common.utils.detailed_traceback()
                self.config_log(tb, level=SandeshLevel.SYS_ERR)
                continue
        # end for all objects

        # Send UVEs resync with a pool of workers
        uve_workers = gevent.pool.Group()
        def format_args_for_dbe_uve_trace(args):
            return self.dbe_uve_trace(*args)
        uve_workers.map(format_args_for_dbe_uve_trace, uve_trace_list)
    # end _dbe_resync


    def _dbe_check(self, obj_type, obj_uuids):
        for obj_uuid in obj_uuids:
            try:
                (ok, obj_dict) = self._object_db.object_read(obj_type, [obj_uuid])
            except Exception as e:
                return {'uuid': obj_uuid, 'type': obj_type, 'error': str(e)}
     # end _dbe_check

    def _dbe_read(self, obj_type, obj_uuids):
        results = []
        for obj_uuid in obj_uuids:
            try:
                (ok, obj_dict) = self._object_db.object_read(obj_type, [obj_uuid])
                result_dict = obj_dict[0]
                result_dict['type'] = obj_type
                result_dict['uuid'] = obj_uuid
                results.append(result_dict)
            except Exception as e:
                self.config_object_error(
                    obj_uuid, None, obj_type, '_dbe_read:cassandra_read', str(e))
                continue

        return results
    # end _dbe_read

    @ignore_exceptions
    def _generate_db_request_trace(self, oper, obj_type, obj_id, obj_dict):
        req_id = get_trace_id()

        body = dict(obj_dict)
        body['type'] = obj_type
        body['uuid'] = obj_id
        db_trace = DBRequestTrace(request_id=req_id)
        db_trace.operation = oper
        db_trace.body = json.dumps(body)
        return db_trace
    # end _generate_db_request_trace

    # Public Methods
    # Returns created uuid
    def dbe_alloc(self, obj_type, obj_dict, uuid_requested=None):
        try:
            if uuid_requested:
                obj_uuid = uuid_requested
                ok = self.set_uuid(obj_type, obj_dict,
                                   uuid.UUID(uuid_requested), False)
            else:
                (ok, obj_uuid) = self._alloc_set_uuid(obj_type, obj_dict)
        except ResourceExistsError as e:
            return (False, (409, str(e)))

        return (True, obj_dict['uuid'])
    # end dbe_alloc

    def dbe_uve_trace(self, oper, type, uuid, obj_dict=None, **kwargs):
        if type not in self._UVEMAP:
            return

        if obj_dict is None:
            try:
                (ok, obj_dict) = self.dbe_read(type, uuid)
                if not ok:
                    return
            except NoIdError:
                return

        if type == 'bgp_router':
            if (obj_dict.get('bgp_router_parameters', {}).get('router_type') !=
                'control-node'):
                return

        oper = oper.upper()
        req_id = get_trace_id()
        if 'fq_name' not in obj_dict:
            obj_dict['fq_name'] = self.uuid_to_fq_name(uuid)
        obj_json = {k: json.dumps(obj_dict[k]) for k in obj_dict or {}}

        db_trace = DBRequestTrace(request_id=req_id)
        db_trace.operation = oper
        db_trace.body = "name=%s type=%s value=%s" % (obj_dict['fq_name'],
                                                      type,
                                                      json.dumps(obj_dict))
        uve_table, global_uve = self._UVEMAP[type]
        if global_uve:
            uve_name = obj_dict['fq_name'][-1]
        else:
            uve_name = ':'.join(obj_dict['fq_name'])
        contrail_config = ContrailConfig(name=uve_name,
                                         elements=obj_json,
                                         deleted=oper=='DELETE')
        contrail_config_msg = ContrailConfigTrace(data=contrail_config,
                                                  table=uve_table,
                                                  sandesh=self._sandesh)

        contrail_config_msg.send(sandesh=self._sandesh)
        trace_msg([db_trace], 'DBUVERequestTraceBuf', self._sandesh)

    def dbe_trace(oper):
        def wrapper1(func):
            def wrapper2(self, obj_type, obj_id, obj_dict):
                trace = self._generate_db_request_trace(oper, obj_type,
                                                        obj_id, obj_dict)
                try:
                    ret = func(self, obj_type, obj_id, obj_dict)
                    trace_msg([trace], 'DBRequestTraceBuf',
                              self._sandesh)
                    return ret
                except Exception as e:
                    trace_msg([trace], 'DBRequestTraceBuf',
                              self._sandesh, error_msg=str(e))
                    raise

            return wrapper2
        return wrapper1
    # dbe_trace

    # create/update indexes if object is shared
    def build_shared_index(oper):
        def wrapper1(func):
            def wrapper2(self, obj_type, obj_id, obj_dict):

                # fetch current share information to identify what might have changed
                try:
                    cur_perms2 = self.uuid_to_obj_perms2(obj_id)
                except Exception as e:
                    cur_perms2 = self.get_default_perms2()

                # don't build sharing indexes if operation (create/update) failed
                (ok, result) = func(self, obj_type, obj_id, obj_dict)
                if not ok:
                    return (ok, result)

                # many updates don't touch perms2
                new_perms2 = obj_dict.get('perms2', None)
                if not new_perms2:
                    return (ok, result)

                share_perms = new_perms2['share']
                global_access = new_perms2['global_access']

                # msg = 'RBAC: BSL perms new %s, cur %s' % (new_perms2, cur_perms2)
                # self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

                # change in global access?
                if cur_perms2['global_access'] != global_access:
                    if global_access:
                        self._object_db.set_shared(obj_type, obj_id, rwx = global_access)
                    else:
                        self._object_db.del_shared(obj_type, obj_id)

                # change in shared list? Construct temporary sets to compare
                cur_shared_list = set(item['tenant']+':'+str(item['tenant_access']) for item in cur_perms2['share'])
                new_shared_list = set(item['tenant']+':'+str(item['tenant_access']) for item in new_perms2['share'])
                if cur_shared_list == new_shared_list:
                    return (ok, result)

                # delete sharing if no longer in shared list
                for share_info in cur_shared_list - new_shared_list:
                    # sharing information => [share-type, uuid, rwx bits]
                    (share_type, share_id, share_perms)  = shareinfo_from_perms2(share_info)
                    self._object_db.del_shared(obj_type, obj_id,
                        share_id=share_id, share_type=share_type)

                # share this object with specified tenants
                for share_info in new_shared_list - cur_shared_list:
                    # sharing information => [share-type, uuid, rwx bits]
                    (share_type, share_id, share_perms)  = shareinfo_from_perms2(share_info)
                    self._object_db.set_shared(obj_type, obj_id,
                        share_id = share_id, share_type = share_type, rwx = int(share_perms))

                return (ok, result)
            return wrapper2
        return wrapper1

    @dbe_trace('create')
    @build_shared_index('create')
    def dbe_create(self, obj_type, obj_uuid, obj_dict):
        (ok, result) = self._object_db.object_create(obj_type, obj_uuid, obj_dict)

        if ok:
            # publish to msgbus
            self._msgbus.dbe_publish('CREATE', obj_type, obj_uuid,
                                     obj_dict['fq_name'], obj_dict)

        return (ok, result)
    # end dbe_create

    # input id is uuid
    def dbe_read(self, obj_type, obj_id, obj_fields=None,
                 ret_readonly=False):
        try:
            (ok, cassandra_result) = self._object_db.object_read(
                obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), let caller decide if this can be handled gracefully
            # by re-raising
            if e._unknown_id == obj_id:
                raise

            return (False, str(e))

        return (ok, cassandra_result[0])
    # end dbe_read

    def dbe_count_children(self, obj_type, obj_id, child_type):
        try:
            (ok, cassandra_result) = self._object_db.object_count_children(
                obj_type, obj_id, child_type)
        except NoIdError as e:
            return (False, str(e))

        return (ok, cassandra_result)
    # end dbe_count_children

    def dbe_get_relaxed_refs(self, obj_id):
        return self._object_db.get_relaxed_refs(obj_id)
    # end dbe_get_relaxed_refs

    def dbe_is_latest(self, obj_id, tstamp):
        try:
            is_latest = self._object_db.is_latest(obj_id, tstamp)
            return (True, is_latest)
        except Exception as e:
            return (False, str(e))
    # end dbe_is_latest

    @dbe_trace('update')
    @build_shared_index('update')
    def dbe_update(self, obj_type, obj_uuid, new_obj_dict):
        (ok, cassandra_result) = self._object_db.object_update(
            obj_type, obj_uuid, new_obj_dict)

        # publish to message bus (rabbitmq)
        fq_name = self.uuid_to_fq_name(obj_uuid)
        self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name)

        return (ok, cassandra_result)
    # end dbe_update

    def _owner_id(self):
        env = get_request().headers.environ
        tenant_uuid = env.get('HTTP_X_PROJECT_ID')
        domain = env.get('HTTP_X_DOMAIN_ID')
        if domain is None:
            domain = env.get('HTTP_X_USER_DOMAIN_ID')
            try:
                domain = str(uuid.UUID(domain))
            except ValueError:
                if domain == 'default':
                    domain = 'default-domain'
                domain = self._db_conn.fq_name_to_uuid('domain', [domain])
        if domain:
            domain = domain.replace('-', '')
        return domain, tenant_uuid

    def dbe_list_rdbms(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, is_count=False, filters=None,
                 paginate_start=None, paginate_count=None, is_detail=False,
                 field_names=None, include_shared=False):
        domain = None
        tenant_id = None
        if include_shared:
            domain, tenant_id = self._owner_id()

        return self._object_db.object_list(
                 obj_type, parent_uuids=parent_uuids,
                 back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                 count=is_count, filters=filters, is_detail=is_detail,
                 field_names=field_names, tenant_id=tenant_id, domain=domain)

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, is_count=False, filters=None,
                 paginate_start=None, paginate_count=None, is_detail=False,
                 field_names=None, include_shared=False):
        if self._db_engine == 'rdbms':
            return self.dbe_list_rdbms(obj_type, parent_uuids, back_ref_uuids,
                 obj_uuids, is_count, filters,
                 paginate_start, paginate_count, is_detail,
                 field_names, include_shared)
        (ok, result) = self._object_db.object_list(
                 obj_type, parent_uuids=parent_uuids,
                 back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                 count=is_count, filters=filters)

        if not ok or is_count:
            return (ok, result)

        # include objects shared with tenant
        if include_shared:
            domain, tenant_uuid = self._owner_id()
            shares = self.get_shared_objects(obj_type, tenant_uuid, domain)
            owned_objs = set([obj_uuid for (fq_name, obj_uuid) in result])
            for (obj_uuid, obj_perm) in shares:
                # skip owned objects already included in results
                if obj_uuid in owned_objs:
                    continue
                try:
                    fq_name = self.uuid_to_fq_name(obj_uuid)
                    result.append((fq_name, obj_uuid))
                except NoIdError:
                    # uuid no longer valid. Delete?
                    pass
        # end shared

        if is_detail:
            cls = cfgm_common.utils.obj_type_to_vnc_class(obj_type, __name__)
            obj_fields = list(cls.prop_fields) + list(cls.ref_fields)
        else:
            obj_fields = []

        if field_names:
            obj_fields.extend(field_names)

        if not obj_fields:
            return (True, [{'uuid': obj_uuid, 'fq_name': fq_name}
                           for fq_name, obj_uuid in result])
        obj_ids_list = [obj_uuid for _, obj_uuid in result]
        try:
            return self._object_db.object_read(
                obj_type, obj_ids_list, obj_fields, ret_readonly=True)
        except NoIdError as e:
            return (False, str(e))
    # end dbe_list

    @dbe_trace('delete')
    def dbe_delete(self, obj_type, obj_uuid, obj_dict):
        (ok, cassandra_result) = self._object_db.object_delete(
            obj_type, obj_uuid)

        # publish to message bus (rabbitmq)
        self._msgbus.dbe_publish('DELETE', obj_type, obj_uuid,
                                 obj_dict['fq_name'], obj_dict)

        # finally remove mapping in zk
        self.dbe_release(obj_type, obj_dict['fq_name'])

        return ok, cassandra_result
    # end dbe_delete

    def dbe_release(self, obj_type, obj_fq_name):
        self._zk_db.delete_fq_name_to_uuid_mapping(obj_type, obj_fq_name)
    # end dbe_release

    def dbe_oper_publish_pending(self):
        return self._msgbus.num_pending_messages()
    # end dbe_oper_publish_pending

    def useragent_kv_store(self, key, value):
        self._object_db.useragent_kv_store(key, value)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        return self._object_db.useragent_kv_retrieve(key)
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        return self._object_db.useragent_kv_delete(key)
    # end useragent_kv_delete

    def subnet_is_addr_allocated(self, subnet, addr):
        return self._zk_db.subnet_is_addr_allocated(subnet, addr)
    # end subnet_is_addr_allocated

    def subnet_set_in_use(self, subnet, addr):
        return self._zk_db.subnet_set_in_use(subnet, addr)
    # end subnet_set_in_use

    def subnet_reset_in_use(self, subnet, addr):
        return self._zk_db.subnet_reset_in_use(subnet, addr)
    #end subnet_reset_in_use

    def subnet_alloc_count(self, subnet):
        return self._zk_db.subnet_alloc_count(subnet)
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None):
        return self._zk_db.subnet_alloc_req(subnet, value)
    # end subnet_alloc_req

    def subnet_reserve_req(self, subnet, addr=None, value=None):
        return self._zk_db.subnet_reserve_req(subnet, addr, value)
    # end subnet_reserve_req

    def subnet_free_req(self, subnet, addr):
        return self._zk_db.subnet_free_req(subnet, addr)
    # end subnet_free_req

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        return self._zk_db.create_subnet_allocator(subnet,
                               subnet_alloc_list, addr_from_start,
                               should_persist, start_subnet, size, alloc_unit)
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet):
        return self._zk_db.delete_subnet_allocator(subnet)
    # end subnet_delete_allocator

    def uuid_vnlist(self):
        return self._object_db.uuid_vnlist()
    # end uuid_vnlist

    def fq_name_to_uuid(self, obj_type, fq_name):
        obj_uuid = self._object_db.fq_name_to_uuid(obj_type, fq_name)
        return obj_uuid
    # end fq_name_to_uuid

    def uuid_to_fq_name(self, obj_uuid):
        return self._object_db.uuid_to_fq_name(obj_uuid)
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, obj_uuid):
        return self._object_db.uuid_to_obj_type(obj_uuid)
    # end uuid_to_obj_type

    def uuid_to_obj_dict(self, obj_uuid):
        return self._object_db.uuid_to_obj_dict(obj_uuid)
    # end uuid_to_obj_dict

    def uuid_to_obj_perms(self, obj_uuid):
        return self._object_db.uuid_to_obj_perms(obj_uuid)
    # end uuid_to_obj_perms

    def prop_collection_get(self, obj_type, obj_uuid, obj_fields, position):
        (ok, cassandra_result) = self._object_db.prop_collection_read(
            obj_type, obj_uuid, obj_fields, position)
        return ok, cassandra_result
    # end prop_collection_get

    def prop_collection_update(self, obj_type, obj_uuid, updates):
        if not updates:
            return

        self._object_db.prop_collection_update(obj_type, obj_uuid, updates)
        fq_name = self.uuid_to_fq_name(obj_uuid)
        self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name)
        return True, ''
    # end prop_collection_update

    def ref_update(self, obj_type, obj_uuid, ref_obj_type, ref_uuid, ref_data,
                   operation):
        self._object_db.ref_update(obj_type, obj_uuid, ref_obj_type,
                                   ref_uuid, ref_data, operation)
        fq_name = self.uuid_to_fq_name(obj_uuid)
        self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name)
    # ref_update

    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        self._object_db.ref_relax_for_delete(obj_uuid, ref_uuid)
    # end ref_relax_for_delete

    def uuid_to_obj_perms2(self, obj_uuid):
        return self._object_db.uuid_to_obj_perms2(obj_uuid)
    # end uuid_to_obj_perms2


    def get_resource_class(self, type):
        return self._api_svr_mgr.get_resource_class(type)
    # end get_resource_class

    def get_default_perms2(self):
        return self._api_svr_mgr._get_default_perms2()

    # Helper routines for REST
    def generate_url(self, resource_type, obj_uuid):
        return self._api_svr_mgr.generate_url(resource_type, obj_uuid)
    # end generate_url

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        self._api_svr_mgr.config_object_error(
            id, fq_name_str, obj_type, operation, err_str)
    # end config_object_error

    def config_log(self, msg, level):
        self._api_svr_mgr.config_log(msg, level)
    # end config_log

    def get_server_port(self):
        return self._api_svr_mgr.get_server_port()
    # end get_server_port

    # return all objects shared with us (tenant)
    # useful for collections
    def get_shared_objects(self, obj_type, tenant_uuid, domain_uuid):
        shared = []
        # specifically shared with us
        if tenant_uuid:
            l1 = self._object_db.get_shared(obj_type, share_id = tenant_uuid, share_type = 'tenant')
            if l1:
                shared.extend(l1)

        # shared at domain level
        if domain_uuid:
            l1 = self._object_db.get_shared(obj_type, share_id = domain_uuid, share_type = 'domain')
            if l1:
                shared.extend(l1)

        # globally shared
        l2 = self._object_db.get_shared(obj_type)
        if l2:
            shared.extend(l2)

        return shared
    # end get_shared_objects

    def reset(self):
        self._msgbus.reset()
    # end reset

    def get_worker_id(self):
        return self._api_svr_mgr.get_worker_id()
    # end get_worker_id

    def get_autonomous_system(self):
        config_uuid = self.fq_name_to_uuid('global_system_config', ['default-global-system-config'])
        ok, config = self._object_db.object_read('global_system_config', [config_uuid])
        global_asn = config[0]['autonomous_system']
        return global_asn


# end class VncDbClient
