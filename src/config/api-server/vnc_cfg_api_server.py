#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""
This is the main module in vnc_cfg_api_server package. It manages interaction
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
import re
import logging
import logging.config
import signal
import os
import socket
from cfgm_common import jsonutils as json
import uuid
import copy
from pprint import pformat
from cStringIO import StringIO
# import GreenletProfiler

logger = logging.getLogger(__name__)

"""
Following is needed to silence warnings on every request when keystone\
    auth_token middleware + Sandesh is used. Keystone or Sandesh alone\
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
import vnc_cfg_types
from vnc_cfg_ifmap import VncDbClient

from cfgm_common import ignore_exceptions
from cfgm_common.uve.vnc_api.ttypes import VncApiCommon, VncApiReadLog,\
    VncApiConfigLog, VncApiError
from cfgm_common.uve.virtual_network.ttypes import UveVirtualNetworkConfig,\
    UveVirtualNetworkConfigTrace
from cfgm_common import illegal_xml_chars_RE
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType,\
    NodeTypeNames, INSTANCE_ID_DEFAULT, API_SERVER_DISCOVERY_SERVICE_NAME,\
    IFMAP_SERVER_DISCOVERY_SERVICE_NAME

from provision_defaults import Provision
from vnc_quota import *
from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *
from gen.vnc_api_server_gen import VncApiServerGen
import cfgm_common
from cfgm_common.rest import LinkObject
from cfgm_common.utils import cgitb_hook
from cfgm_common.exceptions import *
from cfgm_common.vnc_extensions import ExtensionManager
import gen.resource_xsd
import vnc_addr_mgmt
import vnc_auth
import vnc_auth_keystone
import vnc_perms
from cfgm_common import vnc_cpu_info

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import discoveryclient.client as client
# from gen_py.vnc_api.ttypes import *
import netifaces
from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from sandesh.traces.ttypes import RestApiTrace
from vnc_bottle import get_bottle_server

_ACTION_RESOURCES = [
    {'uri': '/ref-update', 'link_name': 'ref-update',
     'method_name': 'ref_update_http_post'},
    {'uri': '/fqname-to-id', 'link_name': 'name-to-id',
     'method_name': 'fq_name_to_id_http_post'},
    {'uri': '/id-to-fqname', 'link_name': 'id-to-name',
     'method_name': 'id_to_fq_name_http_post'},
    # ifmap-to-id only for ifmap subcribers using rest for publish
    {'uri': '/ifmap-to-id', 'link_name': 'ifmap-to-id',
     'method_name': 'ifmap_to_id_http_post'},
    {'uri': '/useragent-kv', 'link_name': 'useragent-keyvalue',
     'method_name': 'useragent_kv_http_post'},
    {'uri': '/db-check', 'link_name': 'database-check',
     'method_name': 'db_check'},
    {'uri': '/fetch-records', 'link_name': 'fetch-records',
     'method_name': 'fetch_records'},
    {'uri': '/start-profile', 'link_name': 'start-profile',
     'method_name': 'start_profile'},
    {'uri': '/stop-profile', 'link_name': 'stop-profile',
     'method_name': 'stop_profile'},
    {'uri': '/list-bulk-collection', 'link_name': 'list-bulk-collection',
     'method_name': 'list_bulk_collection_http_post'},
]


@bottle.error(400)
def error_400(err):
    return err.body
# end error_400


@bottle.error(403)
def error_403(err):
    return err.body
# end error_403


@bottle.error(404)
def error_404(err):
    return err.body
# end error_404


@bottle.error(409)
def error_409(err):
    return err.body
# end error_409


@bottle.error(500)
def error_500(err):
    return err.body
# end error_500


@bottle.error(503)
def error_503(err):
    return err.body
# end error_503


def str_to_class(class_name):
    try:
        return reduce(getattr, class_name.split("."), sys.modules[__name__])
    except Exception as e:
        logger = logging.getLogger(__name__)
        logger.warn("Exception: %s", str(e))
        return None
#end str_to_class

class VncApiServer(VncApiServerGen):
    """
    This is the manager class co-ordinating all classes present in the package
    """
    _INVALID_NAME_CHARS = set(':')

    def __new__(cls, *args, **kwargs):
        obj = super(VncApiServer, cls).__new__(cls, *args, **kwargs)
        bottle.route('/', 'GET', obj.homepage_http_get)
        for act_res in _ACTION_RESOURCES:
            method = getattr(obj, act_res['method_name'])
            obj.route(act_res['uri'], 'POST', method)
        return obj
    # end __new__

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        # set python logging level from logging_level cmdline arg
        if not self._args.logging_conf:
            logging.basicConfig(level = getattr(logging, self._args.logging_level))
        else:
            logging.config.fileConfig(self._args.logging_conf)

        self._base_url = "http://%s:%s" % (self._args.listen_ip_addr,
                                           self._args.listen_port)
        super(VncApiServer, self).__init__()
        self._pipe_start_app = None

        #GreenletProfiler.set_clock_type('wall')
        self._profile_info = None

        # REST interface initialization
        self._get_common = self._http_get_common
        self._put_common = self._http_put_common
        self._delete_common = self._http_delete_common
        self._post_validate = self._http_post_validate
        self._post_common = self._http_post_common

        # Type overrides from generated code
        self.set_resource_class('global-system-config',
            vnc_cfg_types.GlobalSystemConfigServer)
        self.set_resource_class('floating-ip', vnc_cfg_types.FloatingIpServer)
        self.set_resource_class('instance-ip', vnc_cfg_types.InstanceIpServer)
        self.set_resource_class('logical-router',
            vnc_cfg_types.LogicalRouterServer)
        self.set_resource_class('security-group',
            vnc_cfg_types.SecurityGroupServer)
        self.set_resource_class('virtual-machine-interface',
            vnc_cfg_types.VirtualMachineInterfaceServer)
        self.set_resource_class('virtual-network',
            vnc_cfg_types.VirtualNetworkServer)
        self.set_resource_class('network-policy',
            vnc_cfg_types.NetworkPolicyServer)
        self.set_resource_class('network-ipam',
            vnc_cfg_types.NetworkIpamServer)
        self.set_resource_class('virtual-DNS', vnc_cfg_types.VirtualDnsServer)
        self.set_resource_class('virtual-DNS-record',
            vnc_cfg_types.VirtualDnsRecordServer)
        self.set_resource_class('logical-interface',
            vnc_cfg_types.LogicalInterfaceServer)
        self.set_resource_class('physical-interface',
            vnc_cfg_types.PhysicalInterfaceServer)

        self.set_resource_class('virtual-ip', vnc_cfg_types.VirtualIpServer)
        self.set_resource_class('loadbalancer-healthmonitor',
            vnc_cfg_types.LoadbalancerHealthmonitorServer)
        self.set_resource_class('loadbalancer-member',
            vnc_cfg_types.LoadbalancerMemberServer)
        self.set_resource_class('loadbalancer-pool',
            vnc_cfg_types.LoadbalancerPoolServer)

        # TODO default-generation-setting can be from ini file
        self.get_resource_class('bgp-router').generate_default_instance = False
        self.get_resource_class(
            'virtual-router').generate_default_instance = False
        self.get_resource_class(
            'access-control-list').generate_default_instance = False
        self.get_resource_class(
            'floating-ip-pool').generate_default_instance = False
        self.get_resource_class('instance-ip').generate_default_instance = False
        self.get_resource_class('logical-router').generate_default_instance = False
        self.get_resource_class('security-group').generate_default_instance = False
        self.get_resource_class(
            'virtual-machine').generate_default_instance = False
        self.get_resource_class(
            'virtual-machine-interface').generate_default_instance = False
        self.get_resource_class(
            'service-template').generate_default_instance = False
        self.get_resource_class(
            'service-instance').generate_default_instance = False
        self.get_resource_class(
            'virtual-DNS-record').generate_default_instance = False
        self.get_resource_class('virtual-DNS').generate_default_instance = False
        self.get_resource_class(
            'global-vrouter-config').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-pool').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-member').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-healthmonitor').generate_default_instance = False
        self.get_resource_class(
            'virtual-ip').generate_default_instance = False
        self.get_resource_class('config-node').generate_default_instance = False
        self.get_resource_class('analytics-node').generate_default_instance = False
        self.get_resource_class('database-node').generate_default_instance = False
        self.get_resource_class('physical-router').generate_default_instance = False
        self.get_resource_class('physical-interface').generate_default_instance = False
        self.get_resource_class('logical-interface').generate_default_instance = False

        for act_res in _ACTION_RESOURCES:
            link = LinkObject('action', self._base_url, act_res['uri'],
                              act_res['link_name'])
            self._homepage_links.append(link)

        # Register for VN delete request. Disallow delete of system default VN
        self.route('/virtual-network/<id>', 'DELETE', self.virtual_network_http_delete)

        bottle.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)
        self._homepage_links.insert(
            0, LinkObject('documentation', self._base_url,
                          '/documentation/index.html',
                          'documentation'))

        # APIs to reserve/free block of IP address from a VN/Subnet
        bottle.route('/virtual-network/<id>/ip-alloc',
                     'POST', self.vn_ip_alloc_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-alloc',
                       'virtual-network-ip-alloc'))

        bottle.route('/virtual-network/<id>/ip-free',
                     'POST', self.vn_ip_free_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-free',
                       'virtual-network-ip-free'))

        # APIs to find out number of ip instances from given VN subnet
        bottle.route('/virtual-network/<id>/subnet-ip-count',
                     'POST', self.vn_subnet_ip_count_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/subnet-ip-count',
                       'virtual-network-subnet-ip-count'))

        # Enable/Disable multi tenancy
        bottle.route('/multi-tenancy', 'GET', self.mt_http_get)
        bottle.route('/multi-tenancy', 'PUT', self.mt_http_put)

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(self._args.disc_server_ip,
                                                self._args.disc_server_port,
                                                ModuleNames[Module.API_SERVER])

        # sandesh init
        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit  value
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._args.sandesh_send_rate_limit)
        module = Module.API_SERVER
        module_name = ModuleNames[Module.API_SERVER]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        if self._args.worker_id:
            instance_id = self._args.worker_id
        else:
            instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.gethostname()
        self._sandesh.init_generator(module_name, hostname,
                                     node_type_name, instance_id,
                                     self._args.collectors,
                                     'vnc_api_server_context',
                                     int(self._args.http_server_port),
                                     ['cfgm_common'], self._disc)
        self._sandesh.trace_buffer_create(name="VncCfgTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="RestApiTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="DBRequestTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)
        self._sandesh.trace_buffer_create(name="IfmapTraceBuf", size=1000)

        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        ConnectionState.init(self._sandesh, hostname, module_name,
                instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus)

        # Load extensions
        self._extension_mgrs = {}
        self._load_extensions()

        # Address Management interface
        addr_mgmt = vnc_addr_mgmt.AddrMgmt(self)
        vnc_cfg_types.LogicalRouterServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.SecurityGroupServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.VirtualMachineInterfaceServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.FloatingIpServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.InstanceIpServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.VirtualNetworkServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.InstanceIpServer.manager = self
        self._addr_mgmt = addr_mgmt

        # Authn/z interface
        if self._args.auth == 'keystone':
            auth_svc = vnc_auth_keystone.AuthServiceKeystone(self, self._args)
        else:
            auth_svc = vnc_auth.AuthService(self, self._args)

        self._pipe_start_app = auth_svc.get_middleware_app()
        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()
            # When the multi tenancy is disable, add 'admin' role into the
            # header for all requests to see all resources
            @self._pipe_start_app.hook('before_request')
            @bottle.hook('before_request')
            def set_admin_role(*args, **kwargs):
                if bottle.request.app != self._pipe_start_app:
                    return
                bottle.request.environ['HTTP_X_ROLE'] = 'admin'

        self._auth_svc = auth_svc

        # API/Permissions check
        self._permissions = vnc_perms.VncPermissions(self, self._args)

        # DB interface initialization
        if self._args.wipe_config:
            self._db_connect(True)
        else:
            self._db_connect(self._args.reset_config)
            self._db_init_entries()

        # Cpuinfo interface
        sysinfo_req = True
        config_node_ip = self.get_server_ip()
        cpu_info = vnc_cpu_info.CpuInfo(
            self._sandesh.module(), self._sandesh.instance_id(), sysinfo_req,
            self._sandesh, 60, config_node_ip)
        self._cpu_info = cpu_info

    # end __init__

    def _extensions_transform_request(self, request):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'transform_request', request)
    # end _extensions_transform_request

    def _extensions_validate_request(self, request):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'validate_request', request)
    # end _extensions_validate_request

    def _extensions_transform_response(self, request, response):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'transform_response', request, response)
    # end _extensions_transform_response

    @ignore_exceptions
    def _generate_rest_api_request_trace(self):
        method = bottle.request.method.upper()
        if method == 'GET':
            return None

        req_id = bottle.request.headers.get('X-Request-Id',
                                            'req-%s' %(str(uuid.uuid4())))
        gevent.getcurrent().trace_request_id = req_id
        url = bottle.request.url
        if method == 'DELETE':
            req_data = ''
        else:
            try:
                req_data = json.dumps(bottle.request.json)
            except Exception as e:
                req_data = '%s: Invalid request body' %(e)
        rest_trace = RestApiTrace(request_id=req_id)
        rest_trace.url = url
        rest_trace.method = method
        rest_trace.request_data = req_data
        return rest_trace
    # end _generate_rest_api_request_trace

    @ignore_exceptions
    def _generate_rest_api_response_trace(self, rest_trace, response):
        if not rest_trace:
            return

        rest_trace.status = bottle.response.status
        rest_trace.response_body = json.dumps(response)
        rest_trace.trace_msg(name='RestApiTraceBuf', sandesh=self._sandesh)
    # end _generate_rest_api_response_trace

    # Public Methods
    def route(self, uri, method, handler):
        def handler_trap_exception(*args, **kwargs):
            trace = None
            try:
                self._extensions_transform_request(bottle.request)
                self._extensions_validate_request(bottle.request)

                trace = self._generate_rest_api_request_trace()
                response = handler(*args, **kwargs)
                self._generate_rest_api_response_trace(trace, response)

                self._extensions_transform_response(bottle.request, response)

                return response
            except Exception as e:
                if trace:
                    trace.trace_msg(name='RestApiTraceBuf',
                        sandesh=self._sandesh)
                # don't log details of bottle.abort i.e handled error cases
                if not isinstance(e, bottle.HTTPError):
                    string_buf = StringIO()
                    cgitb_hook(file=string_buf, format="text")
                    err_msg = string_buf.getvalue()
                    self.config_log(err_msg, level=SandeshLevel.SYS_ERR)

                raise

        bottle.route(uri, method, handler_trap_exception)
    # end route

    def get_args(self):
        return self._args
    # end get_args

    def get_server_ip(self):
        ip_list = []
        for i in netifaces.interfaces():
            try:
                if netifaces.AF_INET in netifaces.ifaddresses(i):
                    addr = netifaces.ifaddresses(i)[netifaces.AF_INET][0][
                        'addr']
                    if addr != '127.0.0.1' and addr not in ip_list:
                        ip_list.append(addr)
            except ValueError, e:
                self.config_log("Skipping interface %s" % i,
                                level=SandeshLevel.SYS_DEBUG)
        return ip_list
    # end get_server_ip

    def get_listen_ip(self):
        return self._args.listen_ip_addr
    # end get_listen_ip

    def get_server_port(self):
        return self._args.listen_port
    # end get_server_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

    # Check for the system created VN. Disallow such VN delete
    def virtual_network_http_delete(self, id):
        db_conn = self._db_conn
        # if obj doesn't exist return early
        try:
            obj_type = db_conn.uuid_to_obj_type(id)
            if obj_type != 'virtual_network':
                bottle.abort(404, 'No virtual-network object found for id %s' %(id))
            vn_name = db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'ID %s does not exist' %(id))
        if vn_name == cfgm_common.IP_FABRIC_VN_FQ_NAME or \
           vn_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME:
            bottle.abort(409, 'Can not delete system created default virtual-network %s' %(id))
        super(VncApiServer, self).virtual_network_http_delete(id)
   # end

    def homepage_http_get(self):
        json_body = {}
        json_links = []
        # strip trailing '/' in url
        url = bottle.request.url[:-1]
        for link in self._homepage_links:
            # strip trailing '/' in url
            json_links.append(
                {'link': link.to_dict(with_url=url)}
            )

        json_body = \
            {"href": url,
             "links": json_links
             }

        return json_body
    # end homepage_http_get

    def documentation_http_get(self, filename):
        # ubuntu packaged path
        doc_root = '/usr/share/doc/contrail-config/doc/contrail-config/html/'
        if not os.path.exists(doc_root):
            # centos packaged path
            doc_root='/usr/share/doc/python-vnc_cfg_api_server/contrial-config/html/'

        return bottle.static_file(
                filename,
                root=doc_root)
    # end documentation_http_get

    def ref_update_http_post(self):
        self._post_common(bottle.request, None, None)
        obj_type = bottle.request.json['type']
        obj_uuid = bottle.request.json['uuid']
        ref_type = bottle.request.json['ref-type'].replace('-', '_')
        operation = bottle.request.json['operation']
        ref_uuid = bottle.request.json.get('ref-uuid')
        ref_fq_name = bottle.request.json.get('ref-fq-name')
        attr = bottle.request.json.get('attr')

        if not ref_uuid and not ref_fq_name:
            bottle.abort(404, 'Either ref-uuid or ref-fq-name must be specified')

        if not ref_uuid:
            try:
                ref_uuid = self._db_conn.fq_name_to_uuid(ref_type, ref_fq_name)
            except NoIdError:
                bottle.abort(404, 'Name ' + pformat(ref_fq_name) + ' not found')

        # To invoke type specific hook and extension manager
        (read_ok, read_result) = self._db_conn.dbe_read(obj_type, bottle.request.json)
        if not read_ok:
            self.config_object_error(obj_uuid, None, obj_type, 'ref_update', read_result)
            bottle.abort(404, read_result)

        obj_dict = read_result

        # invoke the extension
        try:
            pre_func = 'pre_'+obj_type+'_update'
            self._extension_mgrs['resourceApi'].map_method(post_func, obj_uuid, obj_dict)
        except Exception as e:
            pass

        # type-specific hook
        r_class = self.get_resource_class(obj_type)
        if r_class:
            try:
                fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
            except NoIdError:
                bottle.abort(404, 'UUID ' + obj_uuid + ' not found')
            if operation == 'ADD':
                if ref_type+'_refs' not in obj_dict:
                    obj_dict[ref_type+'_refs'] = []
                obj_dict[ref_type+'_refs'].append({'to':ref_fq_name, 'uuid': ref_uuid, 'attr':attr})
            elif operation == 'DELETE':
                for old_ref in obj_dict.get(ref_type+'_refs', []):
                    if old_ref['to'] == ref_fq_name or old_ref['uuid'] == ref_uuid:
                        obj_dict[ref_type+'_refs'].remove(old_ref)
                        break
            else:
                msg = 'Unknown operation ' + operation
                self.config_object_error(obj_uuid, None, obj_type, 'ref_update', msg)
                bottle.abort(409, msg)

            (ok, put_result) = r_class.http_put(obj_uuid, fq_name, obj_dict, self._db_conn)
            if not ok:
                (code, msg) = put_result
                self.config_object_error(obj_uuid, None, obj_type, 'ref_update', msg)
                bottle.abort(code, msg)
        obj_type = obj_type.replace('-', '_')
        try:
            id = self._db_conn.ref_update(obj_type, obj_uuid, ref_type, ref_uuid, {'attr': attr}, operation)
        except NoIdError:
            bottle.abort(404, 'uuid ' + obj_uuid + ' not found')

        # invoke the extension
        try:
            post_func = 'post_'+obj_type+'_update'
            self._extension_mgrs['resourceApi'].map_method(post_func, obj_uuid, obj_dict)
        except Exception as e:
            pass

        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type.replace('-', '_')
        fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        apiConfig.identifier_name=':'.join(fq_name)
        apiConfig.identifier_uuid = obj_uuid
        apiConfig.operation = 'ref-update'
        apiConfig.body = str(bottle.request.json)

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return {'uuid': id}
    # end ref_update_id_http_post

    def fq_name_to_id_http_post(self):
        self._post_common(bottle.request, None, None)
        obj_type = bottle.request.json['type'].replace('-', '_')
        fq_name = bottle.request.json['fq_name']

        try:
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            bottle.abort(404, 'Name ' + pformat(fq_name) + ' not found')

        return {'uuid': id}
    # end fq_name_to_id_http_post

    def id_to_fq_name_http_post(self):
        self._post_common(bottle.request, None, None)
        try:
            obj_uuid = bottle.request.json['uuid']
            fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        except NoIdError:
            bottle.abort(404, 'UUID ' + obj_uuid + ' not found')

        obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        return {'fq_name': fq_name, 'type': obj_type}
    # end id_to_fq_name_http_post

    def ifmap_to_id_http_post(self):
        self._post_common(bottle.request, None, None)
        uuid = self._db_conn.ifmap_id_to_uuid(bottle.request.json['ifmap_id'])
        return {'uuid': uuid}
    # end ifmap_to_id_http_post

    # Enables a user-agent to store and retrieve key-val pair
    # TODO this should be done only for special/quantum plugin
    def useragent_kv_http_post(self):
        self._post_common(bottle.request, None, None)

        oper = bottle.request.json['operation']
        key = bottle.request.json['key']
        val = bottle.request.json.get('value', '')

        # TODO move values to common
        if oper == 'STORE':
            self._db_conn.useragent_kv_store(key, val)
        elif oper == 'RETRIEVE':
            try:
                result = self._db_conn.useragent_kv_retrieve(key)
                return {'value': result}
            except NoUserAgentKey:
                bottle.abort(404, "Unknown User-Agent key " + key)
        elif oper == 'DELETE':
            result = self._db_conn.useragent_kv_delete(key)
        else:
            bottle.abort(404, "Invalid Operation " + oper)

    # end useragent_kv_http_post

    def db_check(self):
        """ Check database for inconsistencies. No update to database """
        check_result = self._db_conn.db_check()

        return {'results': check_result}
    # end db_check

    def fetch_records(self):
        """ Retrieve and return all records """
        result = self._db_conn.db_read()
        return {'results': result}
    # end fetch_records

    def start_profile(self):
        #GreenletProfiler.start()
        pass
    # end start_profile

    def stop_profile(self):
        pass
        #GreenletProfiler.stop()
        #stats = GreenletProfiler.get_func_stats()
        #self._profile_info = stats.print_all()

        #return self._profile_info
    # end stop_profile

    def get_profile_info(self):
        return self._profile_info
    # end get_profile_info

    def get_resource_class(self, resource_type):
        if resource_type.replace('-', '_') in self._resource_classes:
            return self._resource_classes[resource_type.replace('-', '_')]

        cls_name = '%sServerGen' %(cfgm_common.utils.CamelCase(resource_type))
        return self.str_to_class(cls_name)
    # end get_resource_class

    def set_resource_class(self, resource_type, resource_class):
        obj_type = resource_type.replace('-', '_')
        self._resource_classes[obj_type]  = resource_class
    # end set_resource_class

    def list_bulk_collection_http_post(self):
        """ List collection when requested ids don't fit in query params."""

        obj_type = bottle.request.json.get('type') # e.g. virtual-network
        if not obj_type:
            bottle.abort(400, "Bad Request, no 'type' in POST body")

        obj_class = self.get_resource_class(obj_type)
        if not obj_class:
            bottle.abort(400,
                   "Bad Request, Unknown type %s in POST body" %(obj_type))

        try:
            parent_ids = bottle.request.json['parent_id'].split(',')
            parent_uuids = [str(uuid.UUID(p_uuid)) for p_uuid in parent_ids]
        except KeyError:
            parent_uuids = None

        try:
            back_ref_ids = bottle.request.json['back_ref_id'].split(',')
            back_ref_uuids = [str(uuid.UUID(b_uuid)) for b_uuid in back_ref_ids]
        except KeyError:
            back_ref_uuids = None

        try:
            obj_ids = bottle.request.json['obj_uuids'].split(',')
            obj_uuids = [str(uuid.UUID(b_uuid)) for b_uuid in obj_ids]
        except KeyError:
            obj_uuids = None

        is_count = bottle.request.json.get('count', False)
        is_detail = bottle.request.json.get('detail', False)

        filter_params = bottle.request.json.get('filters', {})
        if filter_params:
            try:
               ff_key_vals = filter_params.split(',')
               ff_names = [ff.split('==')[0] for ff in ff_key_vals]
               ff_values = [ff.split('==')[1] for ff in ff_key_vals]
               filters = {'field_names': ff_names, 'field_values': ff_values}
            except Exception as e:
               bottle.abort(400, 'Invalid filter ' + filter_params)
        else:
            filters = None

        req_fields = bottle.request.json.get('fields', [])
        if req_fields:
            req_fields = req_fields.split(',')

        return self._list_collection(obj_type, parent_uuids, back_ref_uuids,
                                     obj_uuids, is_count, is_detail, filters,
                                     req_fields)
    # end list_bulk_collection_http_post

    def str_to_class(self, class_name):
        return str_to_class(class_name)
    # end str_to_class

    # Private Methods
    def _parse_args(self, args_str):
        '''
        Eg. python vnc_cfg_api_server.py --ifmap_server_ip 192.168.1.17
                                         --ifmap_server_port 8443
                                         --ifmap_username test
                                         --ifmap_password test
                                         --cassandra_server_list\
                                             10.1.2.3:9160 10.1.2.4:9160
                                         --redis_server_ip 127.0.0.1
                                         --redis_server_port 6382
                                         --collectors 127.0.0.1:8086
                                         --http_server_port 8090
                                         --listen_ip_addr 127.0.0.1
                                         --listen_port 8082
                                         --admin_port 8095
                                         --log_local
                                         --log_level SYS_DEBUG
                                         --logging_level DEBUG
                                         --logging_conf <logger-conf-file>
                                         --log_category test
                                         --log_file <stdout>
                                         --trace_file /var/log/contrail/vnc_openstack.err
                                         --use_syslog
                                         --syslog_facility LOG_USER
                                         --disc_server_ip 127.0.0.1
                                         --disc_server_port 5998
                                         --worker_id 1
                                         --rabbit_max_pending_updates 4096
                                         --cluster_id <testbed-name>
                                         [--auth keystone]
                                         [--ifmap_server_loc
                                          /home/contrail/source/ifmap-server/]
                                         [--default_encoding ascii ]
        '''
        self._args, _ = utils.parse_args(args_str)
    # end _parse_args

    # sigchld handler is currently not engaged. See comment @sigchld
    def sigchld_handler(self):
        # DB interface initialization
        self._db_connect(reset_config=False)
        self._db_init_entries()
    # end sigchld_handler

    def sigterm_handler(self):
        self.cleanup()
        exit()

    def _load_extensions(self):
        try:
            conf_sections = self._args.config_sections
            self._extension_mgrs['resync'] = ExtensionManager(
                'vnc_cfg_api.resync', api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
            self._extension_mgrs['resourceApi'] = ExtensionManager(
                'vnc_cfg_api.resourceApi',
                propagate_map_exceptions=True,
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
            self._extension_mgrs['neutronApi'] = ExtensionManager(
                'vnc_cfg_api.neutronApi',
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
        except Exception as e:
            self.config_log("Exception in extension load: %s" %(str(e)),
                level=SandeshLevel.SYS_ERR)
    # end _load_extensions

    def _db_connect(self, reset_config):
        ifmap_ip = self._args.ifmap_server_ip
        ifmap_port = self._args.ifmap_server_port
        user = self._args.ifmap_username
        passwd = self._args.ifmap_password
        cass_server_list = self._args.cassandra_server_list
        redis_server_ip = self._args.redis_server_ip
        redis_server_port = self._args.redis_server_port
        zk_server = self._args.zk_server_ip
        rabbit_servers = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        db_conn = VncDbClient(self, ifmap_ip, ifmap_port, user, passwd,
                              cass_server_list, rabbit_servers, rabbit_port,
                              rabbit_user, rabbit_password, rabbit_vhost,
                              rabbit_ha_mode, reset_config,
                              zk_server, self._args.cluster_id)
        self._db_conn = db_conn
    # end _db_connect

    def _ensure_id_perms_present(self, obj_type, obj_uuid, obj_dict):
        """
        Called at resource creation to ensure that id_perms is present in obj
        """
        # retrieve object and permissions
        id_perms = self._get_default_id_perms(obj_type)

        if (('id_perms' not in obj_dict) or
                (obj_dict['id_perms'] is None)):
            # Resource creation
            if obj_uuid is None:
                obj_dict['id_perms'] = id_perms
                return

            return

        # retrieve the previous version of the id_perms
        # from the database and update the id_perms with
        # them.
        if obj_uuid is not None:
            try:
                old_id_perms = self._db_conn.uuid_to_obj_perms(obj_uuid)
                for field, value in old_id_perms.items():
                    if value is not None:
                        id_perms[field] = value
            except NoIdError:
                pass

        # not all fields can be updated
        if obj_uuid:
            field_list = ['enable', 'description']
        else:
            field_list = ['enable', 'description', 'user_visible', 'creator']

        # Start from default and update from obj_dict
        req_id_perms = obj_dict['id_perms']
        for key in field_list:
            if key in req_id_perms:
                id_perms[key] = req_id_perms[key]
        # TODO handle perms present in req_id_perms

        obj_dict['id_perms'] = id_perms
    # end _ensure_id_perms_present

    def _get_default_id_perms(self, obj_type):
        id_perms = copy.deepcopy(Provision.defaults.perms[obj_type])
        id_perms_json = json.dumps(id_perms, default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.iteritems()))
        id_perms_dict = json.loads(id_perms_json)
        return id_perms_dict
    # end _get_default_id_perms

    def _db_init_entries(self):
        # create singleton defaults if they don't exist already in db
        glb_sys_cfg = self._create_singleton_entry(
            GlobalSystemConfig(autonomous_system=64512,
                               config_version=CONFIG_VERSION))
        def_domain = self._create_singleton_entry(Domain())
        ip_fab_vn = self._create_singleton_entry(
            VirtualNetwork(cfgm_common.IP_FABRIC_VN_FQ_NAME[-1]))
        self._create_singleton_entry(RoutingInstance('__default__', ip_fab_vn))
        link_local_vn = self._create_singleton_entry(
            VirtualNetwork(cfgm_common.LINK_LOCAL_VN_FQ_NAME[-1]))
        self._create_singleton_entry(
            RoutingInstance('__link_local__', link_local_vn))

        self._db_conn.db_resync()
        try:
            self._extension_mgrs['resync'].map(self._resync_domains_projects)
        except Exception as e:
            pass
    # end _db_init_entries

    def _resync_domains_projects(self, ext):
        ext.obj.resync_domains_projects()
    # end _resync_domains_projects

    def _create_singleton_entry(self, singleton_obj):
        s_obj = singleton_obj
        obj_type = s_obj.get_type()
        method_name = obj_type.replace('-', '_')
        fq_name = s_obj.get_fq_name()

        # TODO remove backward compat create mapping in zk
        # for singleton START
        try:
            cass_uuid = self._db_conn._cassandra_db.fq_name_to_uuid(obj_type, fq_name)
            try:
                zk_uuid = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
            except NoIdError:
                # doesn't exist in zookeeper but does so in cassandra,
                # migrate this info to zookeeper
                self._db_conn._zk_db.create_fq_name_to_uuid_mapping(obj_type, fq_name, str(cass_uuid))
        except NoIdError:
            # doesn't exist in cassandra as well as zookeeper, proceed normal
            pass
        # TODO backward compat END


        # create if it doesn't exist yet
        try:
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            obj_dict = s_obj.serialize_to_json()
            obj_dict['id_perms'] = self._get_default_id_perms(obj_type)
            (ok, result) = self._db_conn.dbe_alloc(obj_type, obj_dict)
            obj_ids = result
            self._db_conn.dbe_create(obj_type, obj_ids, obj_dict)
            method = '_%s_create_default_children' % (method_name)
            def_children_method = getattr(self, method)
            def_children_method(s_obj)

        return s_obj
    # end _create_singleton_entry

    def _list_collection(self, obj_type, parent_uuids=None,
                         back_ref_uuids=None, obj_uuids=None,
                         is_count=False, is_detail=False, filters=None,
                         req_fields=None):
        method_name = obj_type.replace('-', '_') # e.g. virtual_network

        (ok, result) = self._db_conn.dbe_list(obj_type,
                             parent_uuids, back_ref_uuids, obj_uuids, is_count,
                             filters)
        if not ok:
            self.config_object_error(None, None, '%ss' %(method_name),
                                     'dbe_list', result)
            bottle.abort(404, result)

        # If only counting, return early
        if is_count:
            return {'%ss' %(obj_type): {'count': result}}

        fq_names_uuids = result
        obj_dicts = []
        if not is_detail:
            if not self.is_admin_request():
                obj_ids_list = [{'uuid': obj_uuid} 
                                for _, obj_uuid in fq_names_uuids]
                obj_fields = [u'id_perms']
                (ok, result) = self._db_conn.dbe_read_multi(
                                    obj_type, obj_ids_list, obj_fields)
                if not ok:
                    bottle.abort(404, result)
                for obj_result in result:
                    if obj_result['id_perms'].get('user_visible', True):
                        obj_dict = {}
                        obj_dict['uuid'] = obj_result['uuid']
                        obj_dict['href'] = self.generate_url(obj_type,
                                                         obj_result['uuid'])
                        obj_dict['fq_name'] = obj_result['fq_name']
                        obj_dicts.append(obj_dict)
            else: # admin
                for fq_name, obj_uuid in fq_names_uuids:
                    obj_dict = {}
                    obj_dict['uuid'] = obj_uuid
                    obj_dict['href'] = self.generate_url(obj_type, obj_uuid)
                    obj_dict['fq_name'] = fq_name
                    obj_dicts.append(obj_dict)
        else: #detail
            obj_ids_list = [{'uuid': obj_uuid}
                            for _, obj_uuid in fq_names_uuids]

            obj_class = self.get_resource_class(obj_type)
            obj_fields = list(obj_class.prop_fields) + \
                         list(obj_class.ref_fields)
            if req_fields:
                obj_fields.extend(req_fields)
            (ok, result) = self._db_conn.dbe_read_multi(
                                obj_type, obj_ids_list, obj_fields)

            if not ok:
                bottle.abort(404, result)

            for obj_result in result:
                obj_dict = {}
                obj_dict['name'] = obj_result['fq_name'][-1]
                obj_dict['href'] = self.generate_url(
                                        obj_type, obj_result['uuid'])
                obj_dict.update(obj_result)
                if 'id_perms' not in obj_dict:
                    # It is possible that the object was deleted, but received
                    # an update after that. We need to ignore it for now. In
                    # future, we should clean up such stale objects
                    continue
                if (obj_dict['id_perms'].get('user_visible', True) or
                    self.is_admin_request()):
                    obj_dicts.append({obj_type: obj_dict})

        return {'%ss' %(obj_type): obj_dicts}
    # end _list_collection

    def get_db_connection(self):
        return self._db_conn
    # end get_db_connection

    def generate_url(self, obj_type, obj_uuid):
        obj_uri_type = obj_type.replace('_', '-')
        try:
            url_parts = bottle.request.urlparts
            return '%s://%s/%s/%s'\
                % (url_parts.scheme, url_parts.netloc, obj_uri_type, obj_uuid)
        except Exception as e:
            return '%s/%s/%s' % (self._base_url, obj_uri_type, obj_uuid)
    # end generate_url

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        apiConfig = VncApiCommon()
        if obj_type is not None:
            apiConfig.object_type = obj_type.replace('-', '_')
        apiConfig.identifier_name = fq_name_str
        apiConfig.identifier_uuid = id
        apiConfig.operation = operation
        if err_str:
            apiConfig.error = "%s:%s" % (obj_type, err_str)
        self._set_api_audit_info(apiConfig)

        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)
    # end config_object_error

    def config_log(self, err_str, level=SandeshLevel.SYS_INFO):
        VncApiError(api_error_msg=err_str, level=level, sandesh=self._sandesh).send(
            sandesh=self._sandesh)
    # end config_log

    def add_virtual_network_refs(self, vn_log, obj_dict):
        # Reference to policies
        if not 'network_policy_refs' in obj_dict:
            return
        pols = obj_dict['network_policy_refs']
        vn_log.attached_policies = []
        for pol_ref in pols:
            pol_name = ":".join(pol_ref['to'])
            maj_num = pol_ref['attr']['sequence']['major']
            min_num = pol_ref['attr']['sequence']['minor']
            vn_policy = VnPolicy(vnp_major=maj_num, vnp_minor=min_num,
                                 vnp_name=pol_name)
            vn_log.attached_policies.append(vn_policy)
    # end add_virtual_network_refs

    def _set_api_audit_info(self, apiConfig):
        apiConfig.url = bottle.request.url
        apiConfig.remote_ip = bottle.request.headers.get('Host')
        useragent = bottle.request.headers.get('X-Contrail-Useragent')
        if not useragent:
            useragent = bottle.request.headers.get('User-Agent')
        apiConfig.useragent = useragent
        apiConfig.user = bottle.request.headers.get('X-User-Name')
        apiConfig.project = bottle.request.headers.get('X-Project-Name')
        apiConfig.domain = bottle.request.headers.get('X-Domain-Name', 'None')
        if apiConfig.domain.lower() == 'none':
            apiConfig.domain = 'default-domain'
        if int(bottle.request.headers.get('Content-Length', 0)) > 0:
            apiConfig.body = str(bottle.request.json)
    # end _set_api_audit_info

    # uuid is parent's for collections
    def _http_get_common(self, request, uuid=None):
        # TODO check api + resource perms etc.
        if self._args.multi_tenancy and uuid:
            if isinstance(uuid, list):
                for u_id in uuid:
                    ok, result = self._permissions.check_perms_read(request,
                                                                    u_id)
                    if not ok:
                        return ok, result
            else:
                return self._permissions.check_perms_read(request, uuid)

        return (True, '')
    # end _http_get_common

    def _http_put_common(self, request, obj_type, obj_uuid, obj_fq_name,
                         obj_dict):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        if obj_dict:
            fq_name_str = ":".join(obj_fq_name)

            # TODO keep _id_perms.uuid_xxlong immutable in future
            # dsetia - check with ajay regarding comment above
            # if 'id_perms' in obj_dict:
            #    del obj_dict['id_perms']
            if 'id_perms' in obj_dict and obj_dict['id_perms']['uuid']:
                if not self._db_conn.match_uuid(obj_dict, obj_uuid):
                    log_msg = 'UUID mismatch from %s:%s' \
                        % (request.environ['REMOTE_ADDR'],
                           request.environ['HTTP_USER_AGENT'])
                    self.config_object_error(
                        obj_uuid, fq_name_str, obj_type, 'put', log_msg)
                    self._db_conn.set_uuid(obj_type, obj_dict,
                                           uuid.UUID(obj_uuid),
                                           persist=False)

            # TODO remove this when the generator will be adapted to
            # be consistent with the post method
            obj_type = obj_type.replace('_', '-')

            # Ensure object has at least default permissions set
            self._ensure_id_perms_present(obj_type, obj_uuid, obj_dict)

            apiConfig = VncApiCommon()
            apiConfig.object_type = obj_type.replace('-', '_')
            apiConfig.identifier_name = fq_name_str
            apiConfig.identifier_uuid = obj_uuid
            apiConfig.operation = 'put'
            self._set_api_audit_info(apiConfig)
            log = VncApiConfigLog(api_log=apiConfig,
                    sandesh=self._sandesh)
            log.send(sandesh=self._sandesh)

        # TODO check api + resource perms etc.
        if self._args.multi_tenancy:
            return self._permissions.check_perms_write(request, obj_uuid)

        return (True, '')
    # end _http_put_common

    # parent_type needed for perms check. None for derived objects (eg.
    # routing-instance)
    def _http_delete_common(self, request, obj_type, uuid, parent_type):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        fq_name_str = ":".join(self._db_conn.uuid_to_fq_name(uuid))
        apiConfig = VncApiCommon()
        apiConfig.object_type=obj_type.replace('-', '_')
        apiConfig.identifier_name=fq_name_str
        apiConfig.identifier_uuid = uuid
        apiConfig.operation = 'delete'
        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        # TODO check api + resource perms etc.
        if not self._args.multi_tenancy or not parent_type:
            return (True, '')

        """
        Validate parent allows write access. Implicitly trust
        parent info in the object since coming from our DB.
        """
        obj_dict = self._db_conn.uuid_to_obj_dict(uuid)
        parent_fq_name = json.loads(obj_dict['fq_name'])[:-1]
        try:
            parent_uuid = self._db_conn.fq_name_to_uuid(
                parent_type, parent_fq_name)
        except NoIdError:
            # parent uuid could be null for derived resources such as
            # routing-instance
            return (True, '')
        return self._permissions.check_perms_write(request, parent_uuid)
    # end _http_delete_common

    def _http_post_validate(self, obj_type=None, obj_dict=None):
        if not obj_dict:
            return

        def _check_field_present(fname):
            fval = obj_dict.get(fname)
            if not fval:
                bottle.abort(400, "Bad Request, no %s in POST body" %(fname))
            return fval
        fq_name = _check_field_present('fq_name')

        # well-formed name checks
        if illegal_xml_chars_RE.search(fq_name[-1]):
            bottle.abort(400,
                "Bad Request, name has illegal xml characters")
        if obj_type[:].replace('-','_') == 'route_target':
            invalid_chars = self._INVALID_NAME_CHARS - set(':')
        else:
            invalid_chars = self._INVALID_NAME_CHARS
        if any((c in invalid_chars) for c in fq_name[-1]):
            bottle.abort(400,
                "Bad Request, name has one of invalid chars %s"
                %(invalid_chars))
    # end _http_post_validate

    def _http_post_common(self, request, obj_type, obj_dict):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))
        if not obj_dict:
            # TODO check api + resource perms etc.
            return (True, None)

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        # Fail if object exists already
        try:
            obj_uuid = self._db_conn.fq_name_to_uuid(
                obj_type, obj_dict['fq_name'])
            bottle.abort(
                409, '' + pformat(obj_dict['fq_name']) +
                ' already exists with uuid: ' + obj_uuid)
        except NoIdError:
            pass

        # Ensure object has at least default permissions set
        self._ensure_id_perms_present(obj_type, None, obj_dict)

        # TODO check api + resource perms etc.

        uuid_in_req = obj_dict.get('uuid', None)

        # Set the display name
        if (('display_name' not in obj_dict) or
            (obj_dict['display_name'] is None)):
            obj_dict['display_name'] = obj_dict['fq_name'][-1]

        fq_name_str = ":".join(obj_dict['fq_name'])
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type.replace('-', '_')
        apiConfig.identifier_name=fq_name_str
        apiConfig.identifier_uuid = uuid_in_req
        apiConfig.operation = 'post'
        apiConfig.body = str(request.json)
        if uuid_in_req:
            if uuid_in_req != str(uuid.UUID(uuid_in_req)):
                bottle.abort(400, 'Invalid UUID format: ' + uuid_in_req)
            try:
                fq_name = self._db_conn.uuid_to_fq_name(uuid_in_req)
                bottle.abort(
                    409, uuid_in_req + ' already exists with fq_name: ' +
                    pformat(fq_name))
            except NoIdError:
                pass
            apiConfig.identifier_uuid = uuid_in_req

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return (True, uuid_in_req)
    # end _http_post_common

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    # end cleanup

    # allocate block of IP addresses from VN. Subnet info expected in request
    # body
    def vn_ip_alloc_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet" : "2.1.1.0/24", "count" : 4}
        req_dict = bottle.request.json
        count = req_dict['count'] if 'count' in req_dict else 1
        subnet = req_dict['subnet'] if 'subnet' in req_dict else None
        try:
            result = vnc_cfg_types.VirtualNetworkServer.ip_alloc(
                vn_fq_name, subnet, count)
        except vnc_addr_mgmt.AddrMgmtSubnetUndefined as e:
            bottle.abort(404, str(e))
        except vnc_addr_mgmt.AddrMgmtSubnetExhausted as e:
            bottle.abort(409, str(e))

        return result
    # end vn_ip_alloc_http_post

    # free block of ip addresses to subnet
    def vn_ip_free_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        """
          {
            "subnet" : "2.1.1.0/24",
            "ip_addr": [ "2.1.1.239", "2.1.1.238", "2.1.1.237", "2.1.1.236" ]
          }
        """

        req_dict = bottle.request.json
        ip_list = req_dict['ip_addr'] if 'ip_addr' in req_dict else []
        subnet = req_dict['subnet'] if 'subnet' in req_dict else None
        result = vnc_cfg_types.VirtualNetworkServer.ip_free(
            vn_fq_name, subnet, ip_list)
        return result
    # end vn_ip_free_http_post

    # return no. of  IP addresses from VN/Subnet
    def vn_subnet_ip_count_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet" : ["2.1.1.0/24", "1.1.1.0/24"]
        req_dict = bottle.request.json
        (_, obj_dict) = self._db_conn.dbe_read('virtual-network', {'uuid': id})
        subnet_list = req_dict[
            'subnet_list'] if 'subnet_list' in req_dict else []
        result = vnc_cfg_types.VirtualNetworkServer.subnet_ip_count(
            obj_dict, subnet_list)
        return result
    # end vn_subnet_ip_count_http_post

    def mt_http_get(self):
        pipe_start_app = self.get_pipe_start_app()
        mt = False
        try:
            mt = pipe_start_app.get_mt()
        except AttributeError:
            pass
        return {'enabled': mt}
    # end

    def mt_http_put(self):
        multi_tenancy = bottle.request.json['enabled']
        user_token = bottle.request.get_header('X-Auth-Token')
        if user_token is None:
            bottle.abort(403, " Permission denied")

        data = self._auth_svc.verify_signed_token(user_token)
        if data is None:
            bottle.abort(403, " Permission denied")

        pipe_start_app = self.get_pipe_start_app()
        try:
            pipe_start_app.set_mt(multi_tenancy)
        except AttributeError:
            pass
        self._args.multi_tenancy = multi_tenancy
        return {}
    # end

    def publish_self_to_discovery(self):
        # publish API server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.listen_port,
        }
        if self._disc:
            self.api_server_task = self._disc.publish(
                API_SERVER_DISCOVERY_SERVICE_NAME, data)

    def publish_ifmap_to_discovery(self):
        # publish ifmap server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.ifmap_server_port,
        }
        if self._disc:
            self.ifmap_task = self._disc.publish(
                IFMAP_SERVER_DISCOVERY_SERVICE_NAME, data)
    # end publish_ifmap_to_discovery

    def un_publish_self_to_discovery(self):
        # un publish api server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.listen_port,
        }
        if self._disc:
            self._disc.un_publish(API_SERVER_DISCOVERY_SERVICE_NAME, data)

    def un_publish_ifmap_to_discovery(self):
        # un publish ifmap server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.ifmap_server_port,
        }
        if self._disc:
            self._disc.un_publish(IFMAP_SERVER_DISCOVERY_SERVICE_NAME, data)
    # end un_publish_ifmap_to_discovery

# end class VncApiServer

server = None
def main(args_str=None):
    vnc_api_server = VncApiServer(args_str)
    # set module var for uses with import e.g unit test
    global server
    server = vnc_api_server

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
        vnc_api_server.cleanup()

# end main

def server_main(args_str=None):
    import cgitb
    cgitb.enable(format='text')

    main()
#server_main

if __name__ == "__main__":
    server_main()
