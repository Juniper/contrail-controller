#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Layer that transforms VNC config objects to database representation
"""
from __future__ import absolute_import
from __future__ import division
from future import standard_library
standard_library.install_aliases()
from builtins import zip
from builtins import str
from builtins import object
from past.utils import old_div
from cfgm_common.zkclient import ZookeeperClient, IndexAllocator
from gevent import monkey
monkey.patch_all()
import gevent
import gevent.event

import time
from pprint import pformat

import socket
from netaddr import IPNetwork, IPAddress
from .context import get_request

from cfgm_common.uve.vnc_api.ttypes import *
from cfgm_common import ignore_exceptions
from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.exceptions import ResourceOutOfRangeError
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.utils import cgitb_hook
from cfgm_common.utils import shareinfo_from_perms2
from cfgm_common import vnc_greenlets
from cfgm_common import SGID_MIN_ALLOC
from cfgm_common import VNID_MIN_ALLOC
from . import utils

import copy
from cfgm_common import jsonutils as json
import uuid
import datetime

import os

from .provision_defaults import *
from cfgm_common.exceptions import *
from .vnc_quota import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns import constants
from .sandesh.traces.ttypes import DBRequestTrace, MessageBusNotifyTrace
import functools

import sys

def get_trace_id():
    try:
        req_id = get_request().headers.get(
            'X-Request-Id', gevent.getcurrent().trace_request_id)
    except AttributeError:
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
    _USERAGENT_KEYSPACE_NAME = constants.USERAGENT_KEYSPACE_NAME
    _USERAGENT_KV_CF_NAME = 'useragent_keyval_table'

    @classmethod
    def get_db_info(cls):
        db_info = VncCassandraClient.get_db_info() + \
                  [(cls._USERAGENT_KEYSPACE_NAME, [cls._USERAGENT_KV_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, db_client_mgr, cass_srv_list, reset_config, db_prefix,
                 cassandra_credential, walk, obj_cache_entries,
                 obj_cache_exclude_types, debug_obj_cache_types,
                 log_response_time=None, ssl_enabled=False, ca_certs=None,
                 pool_size=20):
        self._db_client_mgr = db_client_mgr
        keyspaces = self._UUID_KEYSPACE.copy()
        keyspaces[self._USERAGENT_KEYSPACE_NAME] = {
            self._USERAGENT_KV_CF_NAME: {}}
        super(VncServerCassandraClient, self).__init__(
            cass_srv_list, db_prefix, keyspaces, None, self.config_log,
            generate_url=db_client_mgr.generate_url, reset_config=reset_config,
            credential=cassandra_credential, walk=walk,
            obj_cache_entries=obj_cache_entries,
            obj_cache_exclude_types=obj_cache_exclude_types,
            debug_obj_cache_types=debug_obj_cache_types,
            log_response_time=log_response_time, ssl_enabled=ssl_enabled,
            ca_certs=ca_certs)
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
                   ref_data, operation, id_perms, relax_ref_for_delete=False):
        bch = self._obj_uuid_cf.batch()
        if operation == 'ADD':
            self._create_ref(bch, obj_type, obj_uuid, ref_obj_type, ref_uuid,
                             ref_data)
            if relax_ref_for_delete:
                self._relax_ref_for_delete(bch, obj_uuid, ref_uuid)
        elif operation == 'DELETE':
            self._delete_ref(bch, obj_type, obj_uuid, ref_obj_type, ref_uuid)
        else:
            pass
        self.update_last_modified(bch, obj_type, obj_uuid, id_perms)
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
        self._cassandra_driver.insert(ref_uuid, {'relaxbackref:%s' % (obj_uuid):
                                      json.dumps(None)},
                                      batch=bch)
        if send:
            bch.send()
    # end _relax_ref_for_delete

    def get_relaxed_refs(self, obj_uuid):
        relaxed_cols = self._cassandra_driver.get(
            self._OBJ_UUID_CF_NAME, obj_uuid,
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

    def uuid_to_obj_dict(self, id):
        obj_cols = self._cassandra_driver.get(self._OBJ_UUID_CF_NAME, id)
        if not obj_cols:
            raise NoIdError(id)
        return obj_cols
    # end uuid_to_obj_dict

    def uuid_to_obj_perms(self, id):
        return self._cassandra_driver.get_one_col(self._OBJ_UUID_CF_NAME,
                                                  id,
                                                  'prop:id_perms')
    # end uuid_to_obj_perms

    # fetch perms2 for an object
    def uuid_to_obj_perms2(self, id):
        return self._cassandra_driver.get_one_col(self._OBJ_UUID_CF_NAME,
                                                  id,
                                                  'prop:perms2')
    # end uuid_to_obj_perms2

    def useragent_kv_store(self, key, value):
        columns = {'value': value}
        self.add(self._USERAGENT_KV_CF_NAME, key, columns)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        if key:
            if isinstance(key, list):
                rows = self._cassandra_driver.multiget(self._USERAGENT_KV_CF_NAME, key)
                return [rows[row].get('value') for row in rows]
            else:
                row = self._cassandra_driver.get(self._USERAGENT_KV_CF_NAME, key)
                if not row:
                    raise NoUserAgentKey
                return row.get('value')
        else:  # no key specified, return entire contents
            kv_list = []
            for ua_key, ua_cols in self._cassandra_driver.get_range(
                    self._USERAGENT_KV_CF_NAME):
                kv_list.append({'key': ua_key, 'value': ua_cols.get('value')})
            return kv_list
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        if not self.delete(self._USERAGENT_KV_CF_NAME, key):
            raise NoUserAgentKey
    # end useragent_kv_delete

# end class VncServerCassandraClient


class VncServerKombuClient(VncKombuClient):
    def __init__(self, db_client_mgr, rabbit_ip, rabbit_port,
                 rabbit_user, rabbit_password, rabbit_vhost, rabbit_ha_mode,
                 host_ip, rabbit_health_check_interval, **kwargs):
        self._db_client_mgr = db_client_mgr
        self._sandesh = db_client_mgr._sandesh
        listen_port = db_client_mgr.get_server_port()
        q_name = 'vnc_config.%s-%s' % (socket.getfqdn(host_ip),
            listen_port)
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
            else:
                return

            trace_msg([trace], 'MessageBusNotifyTraceBuf', self._sandesh)
        except Exception:
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            errmsg = string_buf.getvalue()
            self.config_log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)
            trace_msg([trace], name='MessageBusNotifyTraceBuf',
                      sandesh=self._sandesh, error_msg=errmsg)
    # end _dbe_subscribe_callback

    def dbe_publish(self, oper, obj_type, obj_id, fq_name, obj_dict=None,
                    extra_dict=None):
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
        if extra_dict is not None:
            oper_info['extra_dict'] = extra_dict
        self.publish(oper_info)

    def _dbe_create_notification(self, obj_info):
        obj_type = obj_info['type']
        obj_uuid = obj_info['uuid']

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_type)
            ok, result = r_class.dbe_create_notification(
                self._db_client_mgr,
                obj_uuid,
                obj_info.get('obj_dict'),
            )
            if not ok:
                if result[0] == 404 and obj_uuid in result[1]:
                    raise NoIdError(obj_uuid)
                else:
                    raise VncError(result)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), ignore notification
            if e._unknown_id == obj_uuid:
                msg = ("Create notification ignored as resource %s '%s' does "
                       "not exists anymore" % (obj_type, obj_uuid))
                self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                return
        except Exception as e:
            err_msg = ("Failed in dbe_create_notification: " + str(e))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
            raise
    # end _dbe_create_notification

    def _dbe_update_notification(self, obj_info):
        obj_type = obj_info['type']
        obj_uuid = obj_info['uuid']
        extra_dict = obj_info.get('extra_dict')

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_type)
            ok, result = r_class.dbe_update_notification(obj_uuid, extra_dict)
            if not ok:
                if result[0] == 404 and obj_uuid in result[1]:
                    raise NoIdError(obj_uuid)
                else:
                    raise VncError(result)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), ignore notification
            if e._unknown_id == obj_uuid:
                msg = ("Update notification ignored as resource %s '%s' does "
                       "not exists anymore" % (obj_type, obj_uuid))
                self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                return
        except Exception as e:
            msg = "Failure in dbe_update_notification: " + str(e)
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
            ok, result = r_class.dbe_delete_notification(obj_uuid, obj_dict)
            if not ok:
                if result[0] == 404 and obj_uuid in result[1]:
                    raise NoIdError(obj_uuid)
                else:
                    raise VncError(result)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), ignore notification
            if e._unknown_id == obj_uuid:
                msg = ("Delete notification ignored as resource %s '%s' does "
                       "not exists anymore" % (obj_type, obj_uuid))
                self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                return
        except Exception as e:
            msg = "Failure in dbe_delete_notification: " + str(e)
            self.config_log(msg, level=SandeshLevel.SYS_ERR)
            raise
    # end _dbe_delete_notification
# end class VncServerKombuClient


class VncZkClient(object):
    _SUBNET_PATH = "/api-server/subnets"
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid"
    _MAX_SUBNET_ADDR_ALLOC = 65535

    _VN_ID_ALLOC_PATH = "/id/virtual-networks/"
    _VN_MAX_ID = 1 << 24

    _VPG_ID_ALLOC_PATH = "/id/virtual-port-group/"
    _VPG_MIN_ID = 0
    _VPG_MAX_ID = (1 << 16) - 1

    _SG_ID_ALLOC_PATH = "/id/security-groups/id/"
    _SG_MAX_ID = 1 << 32

    _TAG_ID_ALLOC_ROOT_PATH = "/id/tags"
    _TAG_TYPE_ID_ALLOC_PATH = "%s/types/" % _TAG_ID_ALLOC_ROOT_PATH
    _TAG_VALUE_ID_ALLOC_PATH = "%s/values/%%s/" % _TAG_ID_ALLOC_ROOT_PATH
    _TAG_TYPE_MAX_ID = (1 << 16) - 1
    _TAG_TYPE_RESERVED_SIZE = 255
    _TAG_VALUE_MAX_ID = (1 << 16) - 1

    _AE_ID_ALLOC_PATH = "/id/aggregated-ethernet/%s/"
    _AE_MAX_ID = (1 << 7) - 1

    _SUB_CLUSTER_ID_ALLOC_PATH = "/id/sub-clusters/id/"
    _SUB_CLUSTER_MAX_ID_2_BYTES = (1 << 16) - 1
    _SUB_CLUSTER_MAX_ID_4_BYTES = (1 << 32) - 1

    def __init__(self, instance_id, zk_server_ip, host_ip, reset_config, db_prefix,
                 sandesh_hdl, log_response_time=None):
        self._db_prefix = db_prefix
        if db_prefix:
            client_pfx = db_prefix + '-'
            zk_path_pfx = db_prefix
        else:
            client_pfx = ''
            zk_path_pfx = ''

        client_name = '%sapi-%s' %(client_pfx, instance_id)
        self._subnet_path = zk_path_pfx + self._SUBNET_PATH
        self._fq_name_to_uuid_path = zk_path_pfx + self._FQ_NAME_TO_UUID_PATH
        _vn_id_alloc_path = zk_path_pfx + self._VN_ID_ALLOC_PATH
        _sg_id_alloc_path = zk_path_pfx + self._SG_ID_ALLOC_PATH
        _tag_type_id_alloc_path = zk_path_pfx + self._TAG_TYPE_ID_ALLOC_PATH
        _vpg_id_alloc_path = zk_path_pfx + self._VPG_ID_ALLOC_PATH
        self._tag_value_id_alloc_path = zk_path_pfx + self._TAG_VALUE_ID_ALLOC_PATH
        self._ae_id_alloc_path = zk_path_pfx + self._AE_ID_ALLOC_PATH
        self._zk_path_pfx = zk_path_pfx

        self._sandesh = sandesh_hdl
        self._reconnect_zk_greenlet = None
        while True:
            try:
                self._zk_client = ZookeeperClient(client_name, zk_server_ip,
                                           host_ip, self._sandesh,
                                           log_response_time=log_response_time)
                # set the lost callback to always reconnect
                self._zk_client.set_lost_cb(self.reconnect_zk)
                break
            except gevent.event.Timeout as e:
                pass

        if reset_config:
            self._zk_client.delete_node(self._subnet_path, True)
            self._zk_client.delete_node(self._fq_name_to_uuid_path, True)
            self._zk_client.delete_node(_vn_id_alloc_path, True)
            self._zk_client.delete_node(_vpg_id_alloc_path, True)
            self._zk_client.delete_node(_sg_id_alloc_path, True)
            self._zk_client.delete_node(
                zk_path_pfx + self._TAG_ID_ALLOC_ROOT_PATH, True)

        self._subnet_allocators = {}
        self._ae_id_allocator = {}

        # Initialize the Aggregated Ethernet allocator
        self._vpg_id_allocator = IndexAllocator(self._zk_client,
                                               _vpg_id_alloc_path,
                                               self._VPG_MAX_ID)

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

        # Initialize tag type ID allocator
        self._tag_type_id_allocator = IndexAllocator(
            self._zk_client,
            _tag_type_id_alloc_path,
            size=self._TAG_TYPE_MAX_ID,
            start_idx=self._TAG_TYPE_RESERVED_SIZE,
        )

        # Initialize the tag value ID allocator for pref-defined tag-type.
        # One allocator per tag type
        self._tag_value_id_allocator = {
            type_name: IndexAllocator(
                self._zk_client,
                self._tag_value_id_alloc_path % type_name,
                self._TAG_VALUE_MAX_ID,
            ) for type_name in list(constants.TagTypeNameToId.keys())}

        # Initialize the sub-cluster ID allocator
        self._sub_cluster_id_allocator = IndexAllocator(
            self._zk_client,
            zk_path_pfx + self._SUB_CLUSTER_ID_ALLOC_PATH,
            start_idx=1,
            size=self._SUB_CLUSTER_MAX_ID_4_BYTES)

    def master_election(self, path, func, *args):
        self._zk_client.master_election(
            self._zk_path_pfx + path, os.getpid(),
            func, *args)
    # end master_election

    def quota_counter(self, path, max_count=sys.maxsize, default=0):
        return self._zk_client.quota_counter(self._zk_path_pfx + path,
                                             max_count, default)

    def quota_counter_exists(self, path):
        return self._zk_client.exists(path)

    def delete_quota_counter(self, path):
        self._zk_client.delete_node(path, recursive=True)

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

    def change_subnet_allocator(self, subnet,
                                subnet_alloc_list, alloc_unit):
        allocator = self._subnet_allocators[subnet]
        allocator.reallocate(
            new_alloc_list=[{'start': old_div(x['start'],alloc_unit),
                         'end':old_div(x['end'],alloc_unit)}
                        for x in subnet_alloc_list])
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
                size=old_div(size,alloc_unit), start_idx=old_div(start_subnet,alloc_unit),
                reverse=not addr_from_start,
                alloc_list=[{'start': old_div(x['start'],alloc_unit), 'end':old_div(x['end'],alloc_unit)}
                            for x in subnet_alloc_list],
                max_alloc=old_div(self._MAX_SUBNET_ADDR_ALLOC,alloc_unit))
    # end create_subnet_allocator

    def delete_subnet_allocator(self, subnet, notify=True):
        if subnet in self._subnet_allocators:
            self._subnet_allocators.pop(subnet, None)
        if not notify:
            # ZK store subnet lock under 2 step depth folder
            # <vn fq_name string>:<subnet prefix>/<subnet prefix len>
            # As we prevent subnet overlaping on a same network, the first
            # folder can contains only one prefix len folder. So we can safely
            # remove first folder recursively.
            prefix, _, _ = subnet.rpartition('/')
            prefix_path = "%s/%s/" % (self._subnet_path, prefix)
            IndexAllocator.delete_all(self._zk_client, prefix_path)

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

    def subnet_alloc_req(self, subnet, value=None, alloc_pools=None,
                         alloc_unit=1):
        allocator = self._get_subnet_allocator(subnet)
        if alloc_pools:
            alloc_list=[{'start': old_div(x['start'],alloc_unit), 'end':old_div(x['end'],alloc_unit)}
                         for x in alloc_pools]
        else:
            alloc_list = []

        try:
            return allocator.alloc(value=value, pools=alloc_list)
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

    def alloc_vn_id(self, fq_name_str, id=None):
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if self.get_vn_from_id(id) is not None:
                self._vn_id_allocator.set_in_use(id - VNID_MIN_ALLOC)
                return id
        elif fq_name_str is not None:
            return self._vn_id_allocator.alloc(fq_name_str) + VNID_MIN_ALLOC

    def free_vn_id(self, id, fq_name_str, notify=False):
        if id is not None and id - VNID_MIN_ALLOC < self._VN_MAX_ID:
            # If fq_name associated to the allocated ID does not correpond to
            # freed resource fq_name, keep zookeeper lock
            allocated_fq_name_str = self.get_vn_from_id(id)
            if (allocated_fq_name_str is not None and
                    allocated_fq_name_str != fq_name_str):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                self._vn_id_allocator.reset_in_use(id - VNID_MIN_ALLOC)
            else:
                self._vn_id_allocator.delete(id - VNID_MIN_ALLOC)

    def alloc_vxlan_id(self, fq_name_str, id, notify=False):
        if notify:
            if self.get_vn_from_id(id) is not None:
                self._vn_id_allocator.set_in_use(id - VNID_MIN_ALLOC)
                return id
        elif fq_name_str is not None:
            allocated_fq_name_str = self.get_vn_from_id(id)
            if (allocated_fq_name_str is not None and
                     allocated_fq_name_str == fq_name_str):
                return id
            return self._vn_id_allocator.reserve(id - VNID_MIN_ALLOC, fq_name_str)

    def free_vxlan_id(self, id, fq_name_str, notify=False):
        if id is not None and id - VNID_MIN_ALLOC < self._VN_MAX_ID:
            # If fq_name associated to the allocated ID does not correpond to
            # freed resource fq_name, keep zookeeper lock
            allocated_fq_name_str = self.get_vn_from_id(id)
            if (allocated_fq_name_str is not None and
                    allocated_fq_name_str != fq_name_str):
                return

            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                self._vn_id_allocator.reset_in_use(id - VNID_MIN_ALLOC)
            else:
                self._vn_id_allocator.delete(id - VNID_MIN_ALLOC)

    def get_vn_from_id(self, id):
        if id is not None and id - VNID_MIN_ALLOC < self._VN_MAX_ID:
            return self._vn_id_allocator.read(id - VNID_MIN_ALLOC)

    def alloc_vpg_id(self, fq_name_str, id=None):
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if self.get_vpg_from_id(id) is not None:
                self._vpg_id_allocator.set_in_use(id - self._VPG_MIN_ID)
                return id
        elif fq_name_str is not None:
            return self._vpg_id_allocator.alloc(fq_name_str) + self._VPG_MIN_ID

    def free_vpg_id(self, id, fq_name_str, notify=False):
        if id is not None and id - self._VPG_MIN_ID < self._VPG_MAX_ID:
            # If fq_name associated to the allocated ID does not correspond to
            # freed resource fq_name, keep zookeeper lock
            allocated_fq_name_str = self.get_vpg_from_id(id)
            if (allocated_fq_name_str is not None and
                    allocated_fq_name_str != fq_name_str):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                self._vpg_id_allocator.reset_in_use(id - self._VPG_MIN_ID)
            else:
                self._vpg_id_allocator.delete(id - self._VPG_MIN_ID)

    def get_vpg_from_id(self, id):
        if id is not None and id - self._VPG_MIN_ID < self._VPG_MAX_ID:
            return self._vpg_id_allocator.read(id - self._VPG_MIN_ID)

    def alloc_sg_id(self, fq_name_str, id=None):
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if self.get_sg_from_id(id) is not None:
                self._vn_id_allocator.set_in_use(id - SGID_MIN_ALLOC)
                return id
            elif fq_name_str is not None:
                try:
                    return self._sg_id_allocator.reserve(
                        id - SGID_MIN_ALLOC, fq_name_str) + SGID_MIN_ALLOC
                except ResourceExistsError:
                    return self._sg_id_allocator.alloc(
                        fq_name_str) + SGID_MIN_ALLOC
        elif fq_name_str is not None:
            return self._sg_id_allocator.alloc(fq_name_str) + SGID_MIN_ALLOC

    def free_sg_id(self, id, fq_name_str, notify=False):
        if id is not None and id > SGID_MIN_ALLOC and id < self._SG_MAX_ID:
            # If fq_name associated to the allocated ID does not correspond to
            # freed resource fq_name, keep zookeeper lock
            allocated_fq_name_str = self.get_sg_from_id(id)
            if (allocated_fq_name_str is not None and
                    allocated_fq_name_str != fq_name_str):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                self._sg_id_allocator.reset_in_use(id - SGID_MIN_ALLOC)
            else:
                self._sg_id_allocator.delete(id - SGID_MIN_ALLOC)

    def get_sg_from_id(self, id):
        if id is not None and id > SGID_MIN_ALLOC and id < self._SG_MAX_ID:
            return self._sg_id_allocator.read(id - SGID_MIN_ALLOC)

    def alloc_tag_type_id(self, type_str, id=None):
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if self.get_tag_type_from_id(id) is not None:
                self._tag_type_id_allocator.set_in_use(id)
                return id
        elif type_str is not None:
            return self._tag_type_id_allocator.alloc(type_str)

    def free_tag_type_id(self, id, type_str, notify=False):
        if id is not None and id < self._TAG_TYPE_MAX_ID:
            # If tag type name associated to the allocated ID does not
            # correpond to freed tag type name, keep zookeeper lock
            allocated_type_str = self.get_tag_type_from_id(id)
            if (allocated_type_str is not None and
                    allocated_type_str != type_str):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                self._tag_type_id_allocator.reset_in_use(id)
            else:
                IndexAllocator.delete_all(
                    self._zk_client, self._tag_value_id_alloc_path % type_str)
                self._tag_type_id_allocator.delete(id)
            self._tag_value_id_allocator.pop(type_str, None)

    def get_tag_type_from_id(self, id):
        if id is not None and id < self._TAG_TYPE_MAX_ID:
            return self._tag_type_id_allocator.read(id)

    def alloc_tag_value_id(self, type_str, fq_name_str, id=None):
        tag_value_id_allocator = self._tag_value_id_allocator.setdefault(
            type_str,
            IndexAllocator(
                self._zk_client,
                self._tag_value_id_alloc_path % type_str,
                self._TAG_VALUE_MAX_ID,
            ),
        )
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if tag_value_id_allocator.read(id) is not None:
                tag_value_id_allocator.set_in_use(id)
                return id
        elif fq_name_str is not None:
            return tag_value_id_allocator.alloc(fq_name_str)

    def free_tag_value_id(self, type_str, id, fq_name_str, notify=False):
        tag_value_id_allocator = self._tag_value_id_allocator.setdefault(
            type_str,
            IndexAllocator(
                self._zk_client,
                self._tag_value_id_alloc_path % type_str,
                self._TAG_VALUE_MAX_ID,
            ),
        )
        if id is not None and id < self._TAG_VALUE_MAX_ID:
            # If tag value associated to the allocated ID does not correpond to
            # freed tag value, keep zookeeper lock
            if fq_name_str != tag_value_id_allocator.read(id):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                tag_value_id_allocator.reset_in_use(id)
            else:
                tag_value_id_allocator.delete(id)

    def get_tag_value_from_id(self, type_str, id):
        if id is not None and id < self._TAG_VALUE_MAX_ID:
            return self._tag_value_id_allocator.setdefault(
                type_str,
                IndexAllocator(
                    self._zk_client,
                    self._tag_value_id_alloc_path % type_str,
                    self._TAG_VALUE_MAX_ID,
                ),
            ).read(id)

    def alloc_ae_id(self, phy_rtr_name, fq_name_str, id=None):
        ae_id_allocator = self._ae_id_allocator.setdefault(
            phy_rtr_name,
            IndexAllocator(
                self._zk_client,
                self._ae_id_alloc_path % phy_rtr_name,
                self._AE_MAX_ID,
            ),
        )
        # If ID provided, it's a notify allocation, just lock allocated ID in
        # memory
        if id is not None:
            if ae_id_allocator.read(id) is not None:
                ae_id_allocator.set_in_use(id)
                return id
        elif fq_name_str is not None:
            return ae_id_allocator.alloc(fq_name_str)

    def free_ae_id(self, phy_rtr_name, id, fq_name_str, notify=False):
        ae_id_allocator = self._ae_id_allocator.setdefault(
            phy_rtr_name,
            IndexAllocator(
                self._zk_client,
                self._ae_id_alloc_path % phy_rtr_name,
                self._AE_MAX_ID,
            ),
        )
        if id is not None and id < self._AE_MAX_ID:
            # If tag value associated to the allocated ID does not correpond to
            # freed tag value, keep zookeeper lock
            if fq_name_str != ae_id_allocator.read(id):
                return
            if notify:
                # If notify, the ZK allocation already removed, just remove
                # lock in memory
                ae_id_allocator.reset_in_use(id)
            else:
                ae_id_allocator.delete(id)

    def _get_sub_cluster_from_id(self, sub_cluster_id):
        return self._sub_cluster_id_allocator.read(sub_cluster_id)

    def get_last_sub_cluster_allocated_id(self):
        return self._sub_cluster_id_allocator.get_last_allocated_id()

    def alloc_sub_cluster_id(self, asn, fq_name_str, sub_cluster_id=None):
        if asn > 0xFFFF:
            pool = {'start': 1, 'end': self._SUB_CLUSTER_MAX_ID_2_BYTES}
        else:
            pool = {'start': 1, 'end': self._SUB_CLUSTER_MAX_ID_4_BYTES}

        if sub_cluster_id is None:
            return self._sub_cluster_id_allocator.alloc(fq_name_str, [pool])

        allocated_id = self._sub_cluster_id_allocator.reserve(
            sub_cluster_id, fq_name_str, [pool])
        # reserve returns none if requested ID is out of the allocation range
        if not allocated_id:
            raise ResourceOutOfRangeError(
                sub_cluster_id,
                self._sub_cluster_id_allocator._start_idx,
                self._sub_cluster_id_allocator._start_idx +\
                    self._sub_cluster_id_allocator._size - 1)
        return allocated_id

    def free_sub_cluster_id(self, sub_cluster_id, fq_name_str, notify=False):
        # If fq_name associated to the allocated ID does not correspond to
        # freed resource fq_name, keep zookeeper lock
        allocated_fq_name_str = self._get_sub_cluster_from_id(sub_cluster_id)
        if allocated_fq_name_str and allocated_fq_name_str != fq_name_str:
            return
        if notify:
            # If notify, the ZK allocation already removed, just remove
            # lock in memory
            self._sub_cluster_id_allocator.reset_in_use(sub_cluster_id)
        else:
            self._sub_cluster_id_allocator.delete(sub_cluster_id)

class VncDbClient(object):
    def __init__(self, api_svr_mgr, db_srv_list, rabbit_servers, rabbit_port,
                 rabbit_user, rabbit_password, rabbit_vhost, rabbit_ha_mode,
                 host_ip, reset_config=False, zk_server_ip=None,
                 db_prefix='', db_credential=None, obj_cache_entries=0,
                 obj_cache_exclude_types=None, debug_obj_cache_types=None,
                 db_engine='cassandra', cassandra_use_ssl=False,
                 cassandra_ca_certs=None, **kwargs):
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
            "analytics_snmp_node" : ("ObjectAnalyticsSNMPInfo", True),
            "analytics_alarm_node" : ("ObjectAnalyticsAlarmInfo", True),
            "database_node" : ("ObjectDatabaseInfo", True),
            "config_database_node" : ("ObjectConfigDatabaseInfo", True),
            "config_node" : ("ObjectConfigNode", True),
            "service_chain" : ("ServiceChain", False),
            "physical_router" : ("ObjectPRouter", True),
            "bgp_router": ("ObjectBgpRouter", True),
            "tag" : ("ObjectTagTable", False),
            "project" : ("ObjectProjectTable", False),
            "firewall_policy" : ("ObjectFirewallPolicyTable", False),
            "firewall_rule" : ("ObjectFirewallRuleTable", False),
            "address_group" : ("ObjectAddressGroupTable", False),
            "service_group" : ("ObjectServiceGroupTable", False),
            "application_policy_set" : ("ObjectApplicationPolicySetTable", False),
        }

        self._db_resync_done = gevent.event.Event()

        self.log_cassandra_response_time = functools.partial(self.log_db_response_time, "CASSANDRA")
        self.log_zk_response_time = functools.partial(self.log_db_response_time, "ZK")

        msg = "Connecting to zookeeper on %s" % (zk_server_ip)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        if db_engine == 'cassandra':
            self._zk_db = VncZkClient(api_svr_mgr.get_worker_id(), zk_server_ip, host_ip,
                                      reset_config, db_prefix, self.config_log,
                                      log_response_time=self.log_zk_response_time)
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
                    obj_cache_exclude_types, debug_obj_cache_types,
                    self.log_cassandra_response_time,
                    ssl_enabled=cassandra_use_ssl, ca_certs=cassandra_ca_certs)

            self._zk_db.master_election("/api-server-election", db_client_init)
        else:
            msg = ("Contrail API server does not support database backend "
                   "'%s'" % db_engine)
            raise NotImplementedError(msg)

        health_check_interval = api_svr_mgr.get_rabbit_health_check_interval()
        if api_svr_mgr.get_worker_id() > 0:
            health_check_interval = 0.0

        self._msgbus = VncServerKombuClient(self, rabbit_servers,
            rabbit_port, rabbit_user, rabbit_password,
            rabbit_vhost, rabbit_ha_mode, host_ip,
            health_check_interval, **kwargs)

    def log_db_response_time(self, db, response_time, oper, level=SandeshLevel.SYS_DEBUG):
        response_time_in_usec = ((response_time.days*24*60*60) +
                                 (response_time.seconds*1000000) +
                                 response_time.microseconds)

        # Create latency stats object
        try:
            req_id = get_trace_id()
        except Exception as e:
            req_id = "NO-REQUESTID"

        stats = VncApiLatencyStats(
            operation_type=oper,
            application=db,
            response_time_in_usec=response_time_in_usec,
            response_size=0,
            identifier=req_id,
        )
        stats_log = VncApiLatencyStatsLog(
            level=level,
            node_name="issu-vm6",
            api_latency_stats=stats,
            sandesh=self._sandesh)
        x=stats_log.send(sandesh=self._sandesh)


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

    def iip_check_subnet(self, iip_dict, ipam_subnet, sn_uuid):
        pfx = ipam_subnet['subnet']['ip_prefix']
        pfx_len = ipam_subnet['subnet']['ip_prefix_len']
        cidr = '%s/%s' % (pfx, pfx_len)
        if (IPAddress(iip_dict['instance_ip_address']) in
                IPNetwork(cidr)):
            iip_dict['subnet_uuid'] = sn_uuid
            self._object_db.object_update('instance_ip',
                                          iip_dict['uuid'], iip_dict)
            return True

        return False
    # end iip_check_subnet

    def iip_update_subnet_uuid(self, iip_dict):
        """ Set the subnet uuid as instance-ip attribute """
        for vn_ref in iip_dict.get('virtual_network_refs', []):
            (ok, results) = self._object_db.object_read(
                'virtual_network', [vn_ref['uuid']],
                field_names=['network_ipam_refs'])
            if not ok:
                return
            vn_dict = results[0]
            ipam_refs = vn_dict.get('network_ipam_refs', [])
            # if iip is from the subnet in ipam['attr'],
            # update valid subnet_uuid in iip object
            flat_ipam_uuid_list = []
            flat_sn_uuid_list = []
            for ipam in ipam_refs:
                ipam_subnets = ipam['attr']['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    sn_uuid = ipam_subnet['subnet_uuid']
                    if 'subnet' not in ipam_subnet or\
                            ipam_subnet['subnet'] is None:
                        # Ipam subnet info need not have ip/prefix info,
                        # instead they could hold the uuid of subnet info.
                        # collect flat ipam uuid and ref subnet_uuid
                        # which is on vn-->ipam link
                        flat_ipam_uuid = ipam['uuid']
                        flat_ipam_uuid_list.append(flat_ipam_uuid)
                        flat_sn_uuid_list.append(sn_uuid)
                        continue
                    if self.iip_check_subnet(iip_dict, ipam_subnet, sn_uuid):
                        return

            # resync subnet_uuid if iip is from flat subnet
            if len(flat_ipam_uuild_list) == 0:
                return

            # read ipam objects which are flat-ipam
            (ok, result) = self._object_db.object_read('network_ipam',
                                                      flat_ipam_uuid_list)
            if not ok:
                    return

            for ipam_dict, subnet_uuid in zip(result, flat_sn_uuid_list):
                ipam_subnets_dict = ipam_dict.get('ipam_subnets') or {}
                ipam_subnets = ipams_subnets_dict.get('subnets') or []
                for ipam_subnet in ipam_subnets:
                    if self.iip_check_subnet(iip_dict, ipam_subnet,
                                             subnet_uuid):
                        return

    def _sub_cluster_upgrade(self, obj_dict):
        if obj_dict.get('sub_cluster_id'):
            return
        sub_cluster_id = self._zk_db.alloc_sub_cluster_id(
            cls.server.global_autonomous_system,
            ':'.join(obj_dict['fq_name']))
        self._object_db.object_update(
            'sub_cluster',
            obj_dict['uuid'],
            {'sub_cluster_id': sub_cluster_id})

    def _dbe_resync(self, obj_type, obj_uuids):
        obj_class = cfgm_common.utils.obj_type_to_vnc_class(obj_type, __name__)
        obj_fields = list(obj_class.prop_fields) + list(obj_class.ref_fields)
        if obj_type == 'project':
            obj_fields.append('logical_routers')

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
                    do_update = False
                    if 'network_ipam_refs' in obj_dict:
                        ipam_refs = obj_dict['network_ipam_refs']
                        for ipam in ipam_refs:
                            vnsn = ipam['attr']
                            ipam_subnets = vnsn['ipam_subnets']
                            if (self.update_subnet_uuid(ipam_subnets)):
                                if not do_update:
                                    do_update = True
                    # set is_provider_network property as True
                    # for ip-fabric network
                    if obj_dict['fq_name'][-1] == 'ip-fabric' and \
                        not obj_dict.get('is_provider_network', False):
                        do_update = True
                        obj_dict['is_provider_network'] = True
                    if do_update:
                        self._object_db.object_update(
                            'virtual_network', obj_uuid, obj_dict)

                elif obj_type == 'virtual_machine_interface':
                    device_owner = obj_dict.get('virtual_machine_interface_device_owner')
                    li_back_refs = obj_dict.get('logical_interface_back_refs', [])
                    if not device_owner and li_back_refs:
                        obj_dict['virtual_machine_interface_device_owner'] = 'PhysicalRouter'
                        self._object_db.object_update('virtual_machine_interface',
                                                      obj_uuid, obj_dict)

                elif obj_type == 'logical_router':
                    # Update IVNs forwarding_mode from l2 to l2_l3
                    vn_refs = obj_dict.get('virtual_network_refs', [])
                    for vn_ref in vn_refs:
                        vn_type = vn_ref.get('attr', {}).get('logical_router_virtual_network_type')
                        if vn_type == 'InternalVirtualNetwork':
                            vn_uuid = vn_ref.get('uuid')
                            _, result_dict = self._object_db.object_read(
                                'virtual_network', [vn_uuid])
                            obj_dict = result_dict[0]
                            vn_props = obj_dict.get('virtual_network_properties', {})
                            vn_props.update({'forwarding_mode': 'l2_l3'})
                            self._object_db.object_update(
                                'virtual_network',
                                vn_uuid,
                                {'virtual_network_properties': vn_props})

                elif obj_type == 'physical_router':
                    # Encrypt PR pwd if not already done
                    if obj_dict.get('physical_router_user_credentials') and \
                            obj_dict.get('physical_router_user_credentials',
                                         {}).get('password'):
                        dict_password = obj_dict.get(
                            'physical_router_user_credentials',
                            {}).get('password')
                        encryption_type = obj_dict.get(
                            'physical_router_encryption_type', 'none')
                        if dict_password is not None and \
                                encryption_type == 'none':
                            encrypt_pwd = utils.encrypt_password(
                                obj_dict['uuid'], dict_password)
                            obj_dict[
                                'physical_router_user_credentials'][
                                'password'] = encrypt_pwd
                            obj_dict[
                                'physical_router_encryption_type'] = 'local'
                            self._object_db.object_update(
                                'physical_router',
                                obj_uuid, obj_dict)

                elif obj_type == 'fabric':
                    # No longer using fabric credentials, so remove
                    if obj_dict.get('fabric_credentials'):
                        obj_dict['fabric_credentials'] = {}
                        self._object_db.object_update('fabric',obj_uuid,
                                                      obj_dict)

                elif obj_type == 'access_control_list':
                    if not obj_dict.get('access_control_list_hash'):
                        rules = obj_dict.get('access_control_list_entries')
                        if rules:
                            rules_obj = AclEntriesType(params_dict=rules)
                            obj_dict['access_control_list_hash'] = hash(rules_obj)
                            self._object_db.object_update('access_control_list',
                                                          obj_uuid, obj_dict)
                elif obj_type == 'global_system_config':
                    if (obj_dict['fq_name'][0] == 'default-global-system-config' and
                         'enable_4byte_as' not in obj_dict):
                        obj_dict['enable_4byte_as'] = False
                        self._object_db.object_update('global_system_config',
                                                      obj_uuid, obj_dict)
                elif obj_type == 'project':
                    self._api_svr_mgr.create_singleton_entry(
                        ApplicationPolicySet(parent_obj=Project(**obj_dict),
                                             all_applications=True),
                    )

                    vxlan_routing = obj_dict.get('vxlan_routing', False)

                    logical_routers = obj_dict.get('logical_routers', [])
                    lr_uuid_list = [lr['uuid'] for lr in logical_routers]
                    if lr_uuid_list:
                        (ok, lr_list) = self._object_db.object_read(
                                            'logical_router',
                                            obj_uuids=lr_uuid_list,
                                            field_names=['logical_router_type',
                                                'virtual_network_refs'])
                        for lr in lr_list:
                            if 'logical_router_type' not in lr:
                                self._object_db.object_update(
                                'logical_router',
                                lr['uuid'],
                                {'logical_router_type': 'vxlan-routing'
                                 if vxlan_routing else 'snat-routing'})

                            int_vn_uuid = None
                            for vn_ref in lr['virtual_network_refs']:
                                if (vn_ref.get('attr', {}).get(
                                      'logical_router_virtual_network_type') ==
                                      'InternalVirtualNetwork'):
                                    int_vn_uuid = vn_ref.get('uuid')
                            if int_vn_uuid is not None:
                                int_vn_display_name = 'LR::%s' % lr['fq_name'][-1]
                                self._object_db.object_update(
                                'virtual_network',
                                int_vn_uuid,
                                {'display_name': int_vn_display_name})

                    if vxlan_routing:
                        obj_dict['vxlan_routing'] = False
                        self._object_db.object_update('project',
                                                      obj_uuid, obj_dict)
                elif obj_type == 'floating_ip':
                    if not obj_dict.get('project_refs'):
                        project_fq_name = obj_dict['fq_name'][:2]
                        ref = {'to': project_fq_name, 'attr': None}
                        obj_dict['project_refs'] = [ref]
                        self._object_db.object_update('floating_ip',
                                                      obj_uuid, obj_dict)

                # create new perms if upgrading
                perms2 = obj_dict.get('perms2')
                update_obj = False
                if perms2 is None:
                    perms2 = self.update_perms2(obj_uuid)
                    update_obj = True
                elif perms2['owner'] is None:
                    perms2['owner'] = 'cloud-admin'
                    update_obj = True
                if (obj_dict.get('is_shared') == True and
                        perms2.get('global_access', 0) == 0):
                    perms2['global_access'] = PERMS_RWX
                    update_obj = True
                if obj_type == 'domain' and len(perms2.get('share') or []) == 0:
                    update_obj = True
                    perms2 = self.enable_domain_sharing(obj_uuid, perms2)
                if update_obj:
                    obj_dict['perms2'] = perms2
                    self._object_db.object_update(obj_type, obj_uuid, obj_dict)

                if (obj_type == 'bgp_router' and
                        'bgp_router_parameters' in obj_dict and
                        'router_type' not in obj_dict['bgp_router_parameters']):
                    self.update_bgp_router_type(obj_dict)

                if obj_type == 'instance_ip' and 'subnet_uuid' not in obj_dict:
                    self.iip_update_subnet_uuid(obj_dict)

                if obj_type == 'sub_cluster':
                    self._sub_cluster_upgrade(obj_dict)
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
                obj_class = self.get_resource_class(type)
                fields = list(obj_class.prop_fields) + list(obj_class.ref_fields)
                (ok, obj_dict) = self.dbe_read(type, uuid, obj_fields=fields)
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
        def wrapper(func):
            @functools.wraps(func)
            def wrapped(self, *args, **kwargs):
                trace = self._generate_db_request_trace(oper, *args)
                try:
                    ret = func(self, *args, **kwargs)
                    trace_msg([trace], 'DBRequestTraceBuf', self._sandesh)
                    return ret
                except Exception as e:
                    trace_msg([trace], 'DBRequestTraceBuf',
                              self._sandesh, error_msg=str(e))
                    raise
            return wrapped
        return wrapper
    # dbe_trace

    # create/update indexes if object is shared
    def build_shared_index(oper):
        def wrapper1(func):
            @functools.wraps(func)
            def wrapper2(self, *args, **kwargs):
                obj_type, obj_id, obj_dict = args

                # fetch current share information to identify what might have changed
                try:
                    cur_perms2 = self.uuid_to_obj_perms2(obj_id)
                except Exception as e:
                    cur_perms2 = self.get_default_perms2()

                # don't build sharing indexes if operation (create/update) failed
                (ok, result) = func(self, *args, **kwargs)
                if not ok:
                    return (ok, result)

                # many updates don't touch perms2
                new_perms2 = obj_dict.get('perms2', None)
                if not new_perms2:
                    return (ok, result)

                share_perms = new_perms2.get('share',
                                             cur_perms2.get('share') or [])
                global_access = new_perms2.get('global_access',
                                               cur_perms2.get('global_access',
                                                              0))

                # msg = 'RBAC: BSL perms new %s, cur %s' % (new_perms2, cur_perms2)
                # self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

                # change in global access?
                if cur_perms2.get('global_access', 0) != global_access:
                    if global_access:
                        self._object_db.set_shared(obj_type, obj_id, rwx = global_access)
                    else:
                        self._object_db.del_shared(obj_type, obj_id)

                # change in shared list? Construct temporary sets to compare
                cur_shared_list = set(
                    item['tenant'] + ':' + str(item['tenant_access'])
                    for item in cur_perms2.get('share') or [])
                new_shared_list = set(
                    item['tenant'] + ':' + str(item['tenant_access'])
                    for item in share_perms or [])
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
        (ok, result) = self._object_db.object_create(obj_type, obj_uuid,
                                                     obj_dict)

        if ok:
            # publish to msgbus
            self._msgbus.dbe_publish('CREATE', obj_type, obj_uuid,
                                     obj_dict['fq_name'], obj_dict=obj_dict)
            self._dbe_publish_update_implicit(obj_type, result)

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

    def _dbe_publish_update_implicit(self, obj_type, uuid_list):
        for ref_uuid in uuid_list:
            try:
                ref_fq_name = self.uuid_to_fq_name(ref_uuid)
                self._msgbus.dbe_publish('UPDATE-IMPLICIT', obj_type,
                                         ref_uuid, ref_fq_name)
            except NoIdError:
                # ignore if the object disappeared
                pass
    # end _dbe_publish_update_implicit

    @dbe_trace('update')
    @build_shared_index('update')
    def dbe_update(self, obj_type, obj_uuid, new_obj_dict,
                   attr_to_publish=None):
        (ok, result) = self._object_db.object_update(obj_type, obj_uuid,
                                                     new_obj_dict)

        if ok:
            try:
                # publish to message bus (rabbitmq)
                fq_name = self.uuid_to_fq_name(obj_uuid)
                self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name,
                                         extra_dict=attr_to_publish)
                self._dbe_publish_update_implicit(obj_type, result)
            except NoIdError as e:
                # Object might have disappeared after the update. Return Success
                # to the user.
                return (ok, result)
        return (ok, result)
    # end dbe_update

    def _owner_id(self):
        env = get_request().headers.environ
        domain_id = env.get('HTTP_X_DOMAIN_ID')
        if domain_id:
            domain_id = str(uuid.UUID(domain_id))
        project_id = env.get('HTTP_X_PROJECT_ID')
        if project_id:
            project_id = str(uuid.UUID(project_id))
        return domain_id, project_id

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, is_count=False, filters=None,
                 paginate_start=None, paginate_count=None, is_detail=False,
                 field_names=None, include_shared=False):

        def collect_shared(owned_fq_name_uuids=None, start=None, count=None):
            shared_result = []
            # include objects shared with tenant
            domain_id, project_id = self._owner_id()
            shares = self.get_shared_objects(obj_type, project_id, domain_id)
            if start is not None:
                # pick only ones greater than marker
                shares = sorted(shares, key=lambda uuid_perm: uuid_perm[0])
                shares = [(_uuid, _perms) for _uuid, _perms in shares
                    if _uuid > start]

            owned_objs = set([obj_uuid for (fq_name, obj_uuid) in
                                       owned_fq_name_uuids or []])

            collected = 0
            marker = None
            for obj_uuid, obj_perm in shares:
                # skip owned objects already included in results
                if obj_uuid in owned_objs:
                    continue
                try:
                    fq_name = self.uuid_to_fq_name(obj_uuid)
                    shared_result.append((fq_name, obj_uuid))
                    collected += 1
                    if count is not None and collected >= count:
                        marker = obj_uuid
                        break
                except NoIdError:
                    # uuid no longer valid. Deleted?
                    pass

            return shared_result, marker
        # end collect_shared

        if paginate_start is None:
            (ok, result, ret_marker) = self._object_db.object_list(
                     obj_type, parent_uuids=parent_uuids,
                     back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                     count=is_count, filters=filters,
                     paginate_start=paginate_start,
                     paginate_count=paginate_count)

            if not ok or is_count:
                return (ok, result, None)

            if include_shared:
                result.extend(collect_shared(result)[0])
        elif not paginate_start.startswith('shared:'):
            # choose to finish non-shared items before shared ones
            # else, items can be missed since sorted order used across two
            # collections (normally available vs shared with tenant)
            (ok, result, ret_marker) = self._object_db.object_list(
                     obj_type, parent_uuids=parent_uuids,
                     back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                     count=is_count, filters=filters,
                     paginate_start=paginate_start,
                     paginate_count=paginate_count)

            if not ok or is_count:
                return (ok, result, None)

            if ret_marker is None and include_shared:
                # transition to collect shared objects
                return (True, [], 'shared:0')
        else: # paginated and non-shared already visited
            result, marker = collect_shared(
                                    start=paginate_start.split(':')[-1],
                                    count=paginate_count)
            if marker is None:
                ret_marker = None
            else:
                ret_marker = 'shared:%s' %(marker)

        if is_detail:
            cls = cfgm_common.utils.obj_type_to_vnc_class(obj_type, __name__)
            obj_fields = list(cls.prop_fields) + list(cls.ref_fields)
        else:
            obj_fields = []

        if field_names:
            obj_fields.extend(field_names)

        if not obj_fields:
            return (True, [{'uuid': obj_uuid, 'fq_name': fq_name}
                           for fq_name, obj_uuid in result], ret_marker)

        obj_ids_list = [obj_uuid for _, obj_uuid in result]
        try:
            ok, read_result = self._object_db.object_read(
                obj_type, obj_ids_list, obj_fields, ret_readonly=True)
        except NoIdError as e:
            ok = False
            read_result = str(e)

        if not ok:
            return ok, read_result, None

        return ok, read_result, ret_marker
    # end dbe_list

    @dbe_trace('delete')
    def dbe_delete(self, obj_type, obj_uuid, obj_dict):
        (ok, result) = self._object_db.object_delete(obj_type, obj_uuid)

        if ok:
            # publish to message bus (rabbitmq)
            self._msgbus.dbe_publish('DELETE', obj_type, obj_uuid,
                                     obj_dict['fq_name'], obj_dict=obj_dict)
            self._dbe_publish_update_implicit(obj_type, result)

            # finally remove mapping in zk
            self.dbe_release(obj_type, obj_dict['fq_name'])

        return ok, result
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
    # end subnet_reset_in_use

    def subnet_alloc_count(self, subnet):
        return self._zk_db.subnet_alloc_count(subnet)
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None, alloc_pools=None,
                         alloc_unit=1):
        return self._zk_db.subnet_alloc_req(subnet, value, alloc_pools,
                                            alloc_unit)
    # end subnet_alloc_req

    def subnet_reserve_req(self, subnet, addr=None, value=None):
        return self._zk_db.subnet_reserve_req(subnet, addr, value)
    # end subnet_reserve_req

    def subnet_free_req(self, subnet, addr):
        return self._zk_db.subnet_free_req(subnet, addr)
    # end subnet_free_req

    def subnet_change_allocator(self, subnet,
                                subnet_alloc_list, alloc_unit):
        return self._zk_db.change_subnet_allocator(subnet,
                                                   subnet_alloc_list,
                                                   alloc_unit)
    # end subnet_change_allocator

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        return self._zk_db.create_subnet_allocator(
            subnet, subnet_alloc_list, addr_from_start,
            should_persist, start_subnet, size, alloc_unit)
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet, notify=True):
        return self._zk_db.delete_subnet_allocator(subnet, notify)
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

    def prop_collection_update(self, obj_type, obj_uuid, updates,
                               attr_to_publish=None):
        if not updates:
            return

        self._object_db.prop_collection_update(obj_type, obj_uuid, updates)
        fq_name = self.uuid_to_fq_name(obj_uuid)
        self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name,
                                 extra_dict=attr_to_publish)
        return True, ''
    # end prop_collection_update

    def ref_update(self, obj_type, obj_uuid, ref_obj_type, ref_uuid, ref_data,
                   operation, id_perms, attr_to_publish=None,
                   relax_ref_for_delete=False):
        self._object_db.ref_update(obj_type, obj_uuid, ref_obj_type,
                                   ref_uuid, ref_data, operation, id_perms,
                                   relax_ref_for_delete)
        fq_name = self.uuid_to_fq_name(obj_uuid)
        self._msgbus.dbe_publish('UPDATE', obj_type, obj_uuid, fq_name,
                                 extra_dict=attr_to_publish)
        if obj_type == ref_obj_type:
            self._dbe_publish_update_implicit(obj_type, [ref_uuid])
        return True, ''
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
            l1 = self._object_db.get_shared(obj_type, share_id=tenant_uuid,
                                            share_type='tenant')
            if l1:
                shared.extend(l1)

        # shared at domain level
        if domain_uuid:
            l1 = self._object_db.get_shared(obj_type, share_id=domain_uuid,
                                            share_type='domain')
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

    # Insert new perms. Called on startup when walking DB
    def update_perms2(self, obj_uuid):
        perms2 = copy.deepcopy(Provision.defaults.perms2)
        perms2_json = json.dumps(perms2, default=lambda o: dict((k, v)
                               for k, v in o.__dict__.items()))
        perms2 = json.loads(perms2_json)
        return perms2

    def enable_domain_sharing(self, obj_uuid, perms2):
        share_item = {
            'tenant': 'domain:%s' % obj_uuid,
            'tenant_access': cfgm_common.DOMAIN_SHARING_PERMS
        }
        perms2.setdefault('share', []).append(share_item)
        return perms2

# end class VncDbClient
