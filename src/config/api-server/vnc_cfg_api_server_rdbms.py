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
#from vnc_cfg_ifmap import dbe_trace

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


def owner_id(request):
    app = request.environ['bottle.app']
    if app.config.local_auth or self._server_mgr.is_auth_disabled():
        return (True, None)

    if self._rbac:
        user, roles = self.get_user_roles(request)
        is_admin = self.cloud_admin_role in [x.lower() for x in roles]
        if is_admin:
            return (True, None)

        env = request.headers.environ
        tenant = env.get('HTTP_X_PROJECT_ID', None)
        if tenant:
            return (True, tenant)
        else:
            return (False, None)

    return (True, None)

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

class Noop(object):
    def nop(*args, **kw):
        pass

    def __getattr__(self, _):
        return self.nop

class NoopQueue(object):
    def qsize(self):
        return 0

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
        self._msgbus = Noop()
        setattr(self._msgbus, '_urls', [])
        self._ifmap_db = Noop()
        setattr(self._ifmap_db, '_queue', NoopQueue())

        self._db_resync_done = gevent.event.Event()

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

    def dbe_is_latest(self, obj_ids, tstamp):
        #TODO(nati) support etag
        return (True, False)
    # end dbe_is_latest

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, count=False, filters=None,
                 paginate_start=None, paginate_count=None, is_detail=False,
                 field_names=None, include_shared=False):
        owner_id = None
        if include_shared:
            ok, owner_id = self._permissions.owner_id(get_request())
            if not ok:
                raise cfgm_common.exceptions.HttpError(403, "Permission Denied")
        return self._object_db.object_list(
                 obj_type, parent_uuids=parent_uuids,
                 back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                 count=count, filters=filters, is_detail=is_detail, field_names=field_names, owner_id=owner_id)
    # end dbe_list

    def get_autonomous_system(self):
        config_uuid = self.fq_name_to_uuid('global_system_config', ['default-global-system-config'])
        ok, config = self._object_db.object_read('global_system_config', [config_uuid])
        global_asn = config[0]['autonomous_system']
        return global_asn


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

    def alloc_vn_id(self, name):
        return self._db_conn._object_db.alloc_vn_id(name)

    def __init__(self, args_str=None):
        super(VncRDBMSApiServer, self).__init__(args_str)

    def obj_view(self, resource_type, obj):
        #TODO(nati) implement this
        return obj

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
