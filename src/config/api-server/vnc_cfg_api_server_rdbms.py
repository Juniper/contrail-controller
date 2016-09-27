#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""
This is the main module in vnc_cfg_api_server package with sql backend. It manages interaction
between http/rest, address management, authentication and database interfaces.
"""

from gevent import monkey
monkey.patch_all()
from gevent import hub

# from neutron plugin to api server, the request URL could be large.
# fix the const
import gevent.pywsgi
gevent.pywsgi.MAX_REQUEST_LINE = 65535

import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import functools
import logging
import logging.config
import signal
import os
import re
import socket
from cfgm_common import jsonutils as json
import uuid
import copy
from pprint import pformat
from cStringIO import StringIO
from lxml import etree
# import GreenletProfiler

logger = logging.getLogger(__name__)

"""
Following is needed to silence warnings on every request when keystone
    auth_token middleware + Sandesh is used. Keystone or Sandesh alone
    do not produce these warnings.

Exception AttributeError: AttributeError(
    "'_DummyThread' object has no attribute '_Thread__block'",)
    in <module 'threading' from '/usr/lib64/python2.7/threading.pyc'> ignored

See http://stackoverflow.com/questions/13193278/understand-python-threading-bug
for more information.
"""
import threading
threading._DummyThread._Thread__stop = lambda x: 42

CONFIG_VERSION = '1.0'

import bottle
bottle.BaseRequest.MEMFILE_MAX = 1024000

import utils
import context
from context import get_request, get_context, set_context
from context import ApiContext
import vnc_cfg_types
import vnc_cfg_ifmap
from vnc_cfg_ifmap import VncDbClient

from cfgm_common import ignore_exceptions, imid
from cfgm_common.uve.vnc_api.ttypes import VncApiCommon, VncApiConfigLog,\
    VncApiError
from cfgm_common import illegal_xml_chars_RE
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType,\
    NodeTypeNames, INSTANCE_ID_DEFAULT, API_SERVER_DISCOVERY_SERVICE_NAME,\
    IFMAP_SERVER_DISCOVERY_SERVICE_NAME

from provision_defaults import Provision
from vnc_quota import *
from gen.resource_xsd import *
from gen.resource_common import *
from gen.vnc_api_client_gen import all_resource_type_tuples
import cfgm_common
from cfgm_common.utils import cgitb_hook
from cfgm_common.rest import LinkObject, hdr_server_tenant
from cfgm_common.exceptions import *
from cfgm_common.vnc_extensions import ExtensionManager
import gen.resource_xsd
import vnc_addr_mgmt
import vnc_auth
import vnc_auth_keystone
import vnc_perms
import vnc_rbac
import vnc_cfg_api_server
from cfgm_common import vnc_cpu_info
from cfgm_common.vnc_api_stats import log_api_stats
from cfgm_common.vnc_rdbms import VncRDBMSClient
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import discoveryclient.client as client
# from gen_py.vnc_api.ttypes import *
import netifaces
from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from sandesh.discovery_client_stats import ttypes as sandesh
from sandesh.traces.ttypes import RestApiTrace
from vnc_bottle import get_bottle_server

class VncServerRDBMSClient(VncRDBMSClient):
    def __init__(self, db_client_mgr, *args, **kwargs):
        self._db_client_mgr = db_client_mgr
        self._subnet_path = "/api-server/subnets"
        super(VncServerRDBMSClient, self).__init__(*args, **kwargs)

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)

    def walk(self, fn):
        walk_results = []
        type_to_sqa_objs_info = self.get_all_sqa_objs_info()
        for obj_type in type_to_sqa_objs_info:
            for sqa_obj_info in type_to_sqa_objs_info[obj_type]:
                self.cache_uuid_to_fq_name_add(
                    sqa_obj_info[0], #uuid
                    json.loads(sqa_obj_info[1]), #fqname
                    obj_type)

        for obj_type in type_to_sqa_objs_info:
            uuid_list = [o[0] for o in type_to_sqa_objs_info[obj_type]]
            try:
                self.config_log('Resync: obj_type %s len %s'
                                %(obj_type, len(uuid_list)),
                                level=SandeshLevel.SYS_INFO)
                result = fn(obj_type, uuid_list)
                if result:
                    walk_results.append(result)
            except Exception as e:
                self.config_log('Error in db walk invoke %s' %(str(e)),
                                level=SandeshLevel.SYS_ERR)
                continue

        return walk_results

class VncRDBMSDbClient(VncDbClient):
    def __init__(self, api_svr_mgr, db_srv_list,
                 reset_config=False, db_prefix='', db_credential=None,
                 connection=None,
                 **kwargs):

        self._api_svr_mgr = api_svr_mgr
        self._sandesh = api_svr_mgr._sandesh

        self._UVEMAP = {
            "virtual_network" : ("ObjectVNTable", False),
            "service_instance" : ("ObjectSITable", False),
            "virtual_router" : ("ObjectVRouter", True),
            "analytics_node" : ("ObjectCollectorInfo", True),
            "database_node" : ("ObjectDatabaseInfo", True),
            "config_node" : ("ObjectConfigNode", True),
            "service_chain" : ("ServiceChain", False),
            "physical_router" : ("ObjectPRouter", True),
            "bgp_router": ("ObjectBgpRouter", True),
        }

        # certificate auth
        ssl_options = None
        if api_svr_mgr._args.use_certs:
            ssl_options = {
                'keyfile': api_svr_mgr._args.keyfile,
                'certfile': api_svr_mgr._args.certfile,
                'ca_certs': api_svr_mgr._args.ca_certs,
                'cert_reqs': ssl.CERT_REQUIRED,
                'ciphers': 'ALL'
            }

        def db_client_init():
            msg = "Connecting to database on %s" % (db_srv_list)
            self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        self._object_db = VncServerRDBMSClient(self,
            server_list=db_srv_list, reset_config=reset_config,
            generate_url=self.generate_url,
            connection=connection,
            db_prefix=db_prefix, credential=db_credential)
        self._zk_db = self._object_db

    # end __init__

    def dbe_trace(oper):
        def wrapper1(func):
            def wrapper2(self, obj_type, obj_ids, obj_dict):
                trace = self._generate_db_request_trace(oper, obj_type,
                                                        obj_ids, obj_dict)
                try:
                    ret = func(self, obj_type, obj_ids, obj_dict)
                    vnc_cfg_ifmap.trace_msg(trace, 'DBRequestTraceBuf',
                              self._sandesh)
                    return ret
                except Exception as e:
                    vnc_cfg_ifmap.trace_msg(trace, 'DBRequestTraceBuf',
                              self._sandesh, error_msg=str(e))
                    raise

            return wrapper2
        return wrapper1
    # dbe_trace

    def _update_default_quota(self):
        """ Read the default quotas from the configuration
        and update it in the project object if not already
        updated.
        """
        default_quota = QuotaHelper.default_quota

        proj_id = self.fq_name_to_uuid('project',
                                       ['default-domain', 'default-project'])
        try:
            (ok, result) = self.dbe_read('project', {'uuid':proj_id})
        except NoIdError as e:
            ok = False
            result = 'Project Not Found: %s' %(proj_id)
        if not ok:
            self.config_log("Updating default quota failed: %s." %(result),
                level=SandeshLevel.SYS_ERR)
            return

        proj_dict = result
        quota = QuotaType()

        proj_dict['quota'] = default_quota
        self.dbe_update('project', {'uuid':proj_id}, proj_dict)
    # end _update_default_quota

    def db_resync(self):
        pass
    # end db_resync

    def wait_for_resync_done(self):
        pass
    # end wait_for_resync_done

    def db_check(self):
        # Read contents from RDBMs and report any read exceptions
        check_results = self._object_db.walk(self._dbe_check)

        return check_results
    # end db_check

    def db_read(self):
        # Read contents from RDBMS
        read_results = self._object_db.walk(self._dbe_read)
        return read_results
    # end db_check

    def set_uuid(self, obj_type, obj_dict, id, do_lock=True):
        # set uuid in the perms meta
        mslong, lslong = self._uuid_to_longs(id)
        obj_dict['id_perms']['uuid'] = {}
        obj_dict['id_perms']['uuid']['uuid_mslong'] = mslong
        obj_dict['id_perms']['uuid']['uuid_lslong'] = lslong
        obj_dict['uuid'] = str(id)

        return True
    # end set_uuid

    def _alloc_set_uuid(self, obj_type, obj_dict):
        id = uuid.uuid4()
        ok = self.set_uuid(obj_type, obj_dict, id)

        return (ok, obj_dict['uuid'])
    # end _alloc_set_uuid

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

    # Public Methods

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

        obj_ids = {
            'uuid': obj_dict['uuid'],
            'imid': "", 'parent_imid': ""}

        return (True, obj_ids)
    # end dbe_alloc

    @dbe_trace('create')
    def dbe_create(self, obj_type, obj_ids, obj_dict):
        return self._object_db.object_create(
            obj_type, obj_ids['uuid'], obj_dict)

        return (ok, result)
    # end dbe_create

    # input id is uuid
    def dbe_read(self, obj_type, obj_ids, obj_fields=None, ret_readonly=True):
        try:
            (ok, db_result) = self._object_db.object_read(
                obj_type, [obj_ids['uuid']], obj_fields)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), let caller decide if this can be handled gracefully
            # by re-raising
            if e._unknown_id == obj_ids['uuid']:
                raise

            return (False, str(e))
        if len(db_result) == 0:
            raise NoIdError(obj_ids['uuid'])
        return (ok, db_result[0])
    # end dbe_read

    def dbe_count_children(self, obj_type, obj_id, child_type):
        try:
            (ok, db_result) = self._object_db.object_count_children(
                obj_type, obj_id, child_type)
        except NoIdError as e:
            return (False, str(e))

        return (ok, db_result)
    # end dbe_count_children

    def dbe_read_multi(self, obj_type, obj_ids_list, obj_fields=None):
        if not obj_ids_list:
            return (True, [])

        try:
            (ok, db_result) = self._object_db.object_read(
                obj_type, [obj_id['uuid'] for obj_id in obj_ids_list],
                obj_fields)
        except NoIdError as e:
            return (False, str(e))

        return (ok, db_result)
    # end dbe_read_multi

    def dbe_get_relaxed_refs(self, obj_id):
        return self._object_db.get_relaxed_refs(obj_id)
    # end dbe_get_relaxed_refs

    def dbe_is_latest(self, obj_ids, tstamp):
        #TODO(nati) support etag
        return (True, False)
    # end dbe_is_latest

    @dbe_trace('update')
    def dbe_update(self, obj_type, obj_ids, new_obj_dict):
        return self._object_db.object_update(
            obj_type, obj_ids['uuid'], new_obj_dict)
    # end dbe_update

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, count=False, filters=None,
                 paginate_start=None, paginate_count=None, is_detail=False,
                 field_names=None, owner_id=None):
        return self._object_db.object_list(
                 obj_type, parent_uuids=parent_uuids,
                 back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                 count=count, filters=filters, is_detail=is_detail, field_names=field_names, owner_id=owner_id)
    # end dbe_list

    @dbe_trace('delete')
    def dbe_delete(self, obj_type, obj_ids, obj_dict):
        return self._object_db.object_delete(
            obj_type, obj_ids['uuid'])
    # end dbe_delete

    def dbe_release(self, obj_type, obj_fq_name):
        pass
    # end dbe_release

    def dbe_oper_publish_pending(self):
        pass
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

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        #TODO
        pass
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet):
        #TODO
        pass
    # end subnet_delete_allocator

    def uuid_to_ifmap_id(self, res_type, id):
        pass

    def fq_name_to_uuid(self, obj_type, fq_name):
        return self._object_db.fq_name_to_uuid(obj_type, fq_name)
    # end fq_name_to_uuid

    def uuid_to_fq_name(self, obj_uuid):
        return self._object_db.uuid_to_fq_name(obj_uuid)
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, obj_uuid):
        return self._object_db.uuid_to_obj_type(obj_uuid)
    # end uuid_to_obj_type

    def get_autonomous_system(self):
        config_uuid = self.fq_name_to_uuid('global_system_config', ['default-global-system-config'])
        ok, config = self._object_db.object_read('global_system_config', [config_uuid])
        global_asn = config[0]['autonomous_system']
        return global_asn

    def uuid_to_obj_perms(self, obj_uuid):
        return self._object_db.uuid_to_obj_perms(obj_uuid)
    # end uuid_to_obj_perms

    def prop_collection_get(self, obj_type, obj_uuid, obj_fields, position):
        return self._object_db.prop_collection_read(
            obj_type, obj_uuid, obj_fields, position)
    # end prop_collection_get

    def prop_collection_update(self, obj_type, obj_uuid, updates):
        if not updates:
            return
        self._object_db.prop_collection_update(obj_type, obj_uuid, updates)
        return True, ''
    # end prop_collection_update

    def ref_update(self, obj_type, obj_uuid, ref_obj_type, ref_uuid, ref_data,
                   operation):
        self._object_db.ref_update(obj_type, obj_uuid, ref_obj_type,
                                      ref_uuid, ref_data, operation)
    # ref_update

    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        self._object_db.ref_relax_for_delete(obj_uuid, ref_uuid)
    # end ref_relax_for_delete

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        self._api_svr_mgr.config_object_error(
            id, fq_name_str, obj_type, operation, err_str)
    # end config_object_error

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size, alloc_unit):
        return self._object_db.create_subnet_allocator(subnet,
                               subnet_alloc_list, addr_from_start,
                               should_persist, start_subnet, size, alloc_unit)
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet):
        return self._object_db.delete_subnet_allocator(subnet)
    # end subnet_delete_allocator

    def reset(self):
        pass
    # end reset
# end class VncRDBMSDbClient

class VncRDBMSApiServer(vnc_cfg_api_server.VncApiServer):
    """
    This is the manager class co-ordinating all classes present in the package
    """
    _INVALID_NAME_CHARS = set(':')
    _GENERATE_DEFAULT_INSTANCE = [
        'namespace',
        'project',
        'virtual_network', 'virtual-network',
        'network_ipam', 'network-ipam',
    ]
    #def __new__(cls, *args, **kwargs):
    #    return super(VncRDBMSApiServer, cls).__new__(cls, *args, **kwargs)
    # end __new__

    def undo(self, result, obj_type, id=None, fq_name=None):
        pass

    def _list_collection(self, obj_type, parent_uuids=None,
                         back_ref_uuids=None, obj_uuids=None,
                         is_count=False, is_detail=False, filters=None,
                         req_fields=None, include_shared=False,
                         exclude_hrefs=False):
        r_class = self.get_resource_class(obj_type)
        resource_type = r_class.resource_type
        ok, owner_id = self._permissions.owner_id(get_request())
        if not ok:
            raise cfgm_common.exceptions.HttpError(403, "Permission Denied")
        (ok, result) = self._db_conn.dbe_list(obj_type,
                             parent_uuids, back_ref_uuids, obj_uuids, is_count,
                             filters, is_detail=is_detail, field_names=req_fields, owner_id=owner_id)
        if not ok:
            self.config_object_error(None, None, '%ss' %(obj_type),
                                     'dbe_list', result)
            raise cfgm_common.exceptions.HttpError(404, result)

        if is_count:
            return {'%ss' %(resource_type): {'count': result}}

        if self.is_admin_request():
            obj_dicts = []
            for obj_result in result:
                if not exclude_hrefs:
                    obj_result['href'] = self.generate_url(
                        resource_type, obj_result['uuid'])
                obj_dicts.append({resource_type: obj_result})
        else:
            obj_dicts = []
            for obj_result in result:
                # TODO(nati) we should do this using sql query
                if  obj_result.get('id_perms', {}).get('user_visible', True):
                    # skip items not authorized
                    (ok, status) = self._permissions.check_perms_read(
                            get_request(), obj_result['uuid'],
                            obj_result['id_perms'])
                    if not ok and status[0] == 403:
                        continue
                    obj_dict = obj_result
                    if not is_detail:
                        del obj_result['id_perms']
                    if not exclude_hrefs:
                        obj_dict['href'] = self.generate_url(
                            resource_type, obj_result['uuid'])
                    obj_dicts.append({resource_type: obj_dict})

        if not is_detail:
            obj_dicts = [obj[resource_type] for obj in obj_dicts]

        return {'%ss' %(resource_type): obj_dicts}
    # end _list_collection

    def alloc_vn_id(self, name):
        return self._db_conn._object_db.alloc_vn_id(name)

    def __init__(self, args_str=None):
        super(VncRDBMSApiServer, self).__init__(args_str)

    def _db_connect(self, reset_config):
        rdbms_server_list = self._args.rdbms_server_list
        rdbms_user = self._args.rdbms_user
        rdbms_password = self._args.rdbms_password
        rdbms_connection = self._args.rdbms_connection
        db_engine = self._args.db_engine
        cred = None

        db_server_list = rdbms_server_list
        if rdbms_user is not None and rdbms_password is not None:
            cred = {'username': rdbms_user,'password': rdbms_password}

        self._db_conn = VncRDBMSDbClient(
            self, db_server_list, connection=rdbms_connection,
            db_credential=cred)
        #TODO refacter db connection management.
        self._addr_mgmt._get_db_conn()
    # end _db_connect

def main(args_str=None, server=None):
    vnc_api_server = server

    pipe_start_app = vnc_api_server.get_pipe_start_app()
    server_ip = vnc_api_server.get_listen_ip()
    server_port = vnc_api_server.get_server_port()

    # Advertise services
    if (vnc_api_server._args.disc_server_ip and
            vnc_api_server._args.disc_server_port):
        vnc_api_server.publish_self_to_discovery()

    """ @sigchld
    Disable handling of SIG_CHLD for now as every keystone request to validate
    token sends SIG_CHLD signal to API server.
    """
    #hub.signal(signal.SIGCHLD, vnc_api_server.sigchld_handler)
    hub.signal(signal.SIGTERM, vnc_api_server.sigterm_handler)
    if pipe_start_app is None:
        pipe_start_app = vnc_api_server.api_bottle
    try:
        bottle.run(app=pipe_start_app, host=server_ip, port=server_port,
                   server=get_bottle_server(server._args.max_requests))
    except KeyboardInterrupt:
        # quietly handle Ctrl-C
        pass
    except:
        # dump stack on all other exceptions
        raise
    finally:
        # always cleanup gracefully
        vnc_api_server.reset()

# end main

def server_main(args_str=None):
    import cgitb
    cgitb.enable(format='text')

    main(args_str, VncRDBMSApiServer(args_str))
#server_main

if __name__ == "__main__":
    server_main()
