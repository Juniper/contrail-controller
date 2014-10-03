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

import sys
import re
import logging
import logging.config
import signal
import os
import socket
import json
import uuid
import copy
import argparse
import ConfigParser
from pprint import pformat
import cgitb
from cStringIO import StringIO
#import GreenletProfiler

import logging
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
from bottle import request

import vnc_cfg_types
from vnc_cfg_ifmap import VncDbClient

from cfgm_common import ignore_exceptions
from cfgm_common.uve.vnc_api.ttypes import VncApiCommon, VncApiReadLog,\
    VncApiConfigLog, VncApiError
from cfgm_common.uve.virtual_network.ttypes import UveVirtualNetworkConfig,\
    UveVirtualNetworkConfigTrace
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT
from provision_defaults import Provision
from vnc_quota import *
from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *
from gen.vnc_api_server_gen import VncApiServerGen
import cfgm_common
from cfgm_common.rest import LinkObject
from cfgm_common.exceptions import *
from cfgm_common.vnc_extensions import ExtensionManager, ApiHookManager
import gen.resource_xsd
import vnc_addr_mgmt
import vnc_auth
import vnc_auth_keystone
import vnc_perms
from cfgm_common import vnc_cpu_info

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import discoveryclient.client as client
#from gen_py.vnc_api.ttypes import *
import netifaces
from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from sandesh.traces.ttypes import RestApiTrace

_WEB_HOST = '0.0.0.0'
_WEB_PORT = 8082
_ADMIN_PORT = 8095

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


# Masking of password from openstack/common/log.py
_SANITIZE_KEYS = ['adminPass', 'admin_pass', 'password', 'admin_password']

# NOTE(ldbragst): Let's build a list of regex objects using the list of
# _SANITIZE_KEYS we already have. This way, we only have to add the new key
# to the list of _SANITIZE_KEYS and we can generate regular expressions
# for XML and JSON automatically.
_SANITIZE_PATTERNS = []
_FORMAT_PATTERNS = [r'(%(key)s\s*[=]\s*[\"\']).*?([\"\'])',
                    r'(<%(key)s>).*?(</%(key)s>)',
                    r'([\"\']%(key)s[\"\']\s*:\s*[\"\']).*?([\"\'])',
                    r'([\'"].*?%(key)s[\'"]\s*:\s*u?[\'"]).*?([\'"])']

for key in _SANITIZE_KEYS:
    for pattern in _FORMAT_PATTERNS:
        reg_ex = re.compile(pattern % {'key': key}, re.DOTALL)
        _SANITIZE_PATTERNS.append(reg_ex)

def mask_password(message, secret="***"):
    """Replace password with 'secret' in message.
    :param message: The string which includes security information.
    :param secret: value with which to replace passwords.
    :returns: The unicode value of message with the password fields masked.

    For example:

    >>> mask_password("'adminPass' : 'aaaaa'")
    "'adminPass' : '***'"
    >>> mask_password("'admin_pass' : 'aaaaa'")
    "'admin_pass' : '***'"
    >>> mask_password('"password" : "aaaaa"')
    '"password" : "***"'
    >>> mask_password("'original_password' : 'aaaaa'")
    "'original_password' : '***'"
    >>> mask_password("u'original_password' :   u'aaaaa'")
    "u'original_password' :   u'***'"
    """
    if not any(key in message for key in _SANITIZE_KEYS):
        return message

    secret = r'\g<1>' + secret + r'\g<2>'
    for pattern in _SANITIZE_PATTERNS:
        message = re.sub(pattern, secret, message)
    return message


class VncApiServer(VncApiServerGen):

    """
    This is the manager class co-ordinating all classes present in the package
    """
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
        self._post_common = self._http_post_common

        # Type overrides from generated code
        self._resource_classes['global-system-config'] = \
            vnc_cfg_types.GlobalSystemConfigServer
        self._resource_classes['floating-ip'] = vnc_cfg_types.FloatingIpServer
        self._resource_classes['instance-ip'] = vnc_cfg_types.InstanceIpServer
        self._resource_classes['logical-router'] = vnc_cfg_types.LogicalRouterServer
        self._resource_classes['security-group'] = vnc_cfg_types.SecurityGroupServer
        self._resource_classes['virtual-machine-interface'] = \
            vnc_cfg_types.VirtualMachineInterfaceServer
        self._resource_classes['virtual-network'] = \
            vnc_cfg_types.VirtualNetworkServer
        self._resource_classes['network-policy'] = \
            vnc_cfg_types.NetworkPolicyServer
        self._resource_classes['network-ipam'] = \
            vnc_cfg_types.NetworkIpamServer
        self._resource_classes['virtual-DNS'] = vnc_cfg_types.VirtualDnsServer
        self._resource_classes['virtual-DNS-record'] = \
            vnc_cfg_types.VirtualDnsRecordServer

        # TODO default-generation-setting can be from ini file
        self._resource_classes['bgp-router'].generate_default_instance = False
        self._resource_classes[
            'virtual-router'].generate_default_instance = False
        self._resource_classes[
            'access-control-list'].generate_default_instance = False
        self._resource_classes[
            'floating-ip-pool'].generate_default_instance = False
        self._resource_classes['instance-ip'].generate_default_instance = False
        self._resource_classes['logical-router'].generate_default_instance = False
        self._resource_classes['security-group'].generate_default_instance = False
        self._resource_classes[
            'virtual-machine'].generate_default_instance = False
        self._resource_classes[
            'virtual-machine-interface'].generate_default_instance = False
        self._resource_classes[
            'service-template'].generate_default_instance = False
        self._resource_classes[
            'service-instance'].generate_default_instance = False
        self._resource_classes[
            'virtual-DNS-record'].generate_default_instance = False
        self._resource_classes['virtual-DNS'].generate_default_instance = False
        self._resource_classes[
            'global-vrouter-config'].generate_default_instance = False
        self._resource_classes[
            'loadbalancer-pool'].generate_default_instance = False
        self._resource_classes[
            'loadbalancer-member'].generate_default_instance = False
        self._resource_classes[
            'loadbalancer-healthmonitor'].generate_default_instance = False
        self._resource_classes[
            'virtual-ip'].generate_default_instance = False

        for act_res in _ACTION_RESOURCES:
            link = LinkObject('action', self._base_url, act_res['uri'],
                              act_res['link_name'])
            self._homepage_links.append(link)

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
            trace = self._generate_rest_api_request_trace()
            try:
                response = handler(*args, **kwargs)
                self._generate_rest_api_response_trace(trace, response)
                return response
            except Exception as e:
                if trace:
                    trace.trace_msg(name='RestApiTraceBuf',
                        sandesh=self._sandesh)
                # don't log details of bottle.abort i.e handled error cases
                if not isinstance(e, bottle.HTTPError):
                    string_buf = StringIO()
                    cgitb.Hook(
                        file=string_buf,
                        format="text",
                        ).handle(sys.exc_info())
                    err_msg = mask_password(string_buf.getvalue())
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
        return bottle.static_file(
            filename,
            root='/usr/share/doc/python-vnc_cfg_api_server/build/html')
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

        # type-specific hook
        r_class = self._resource_classes.get(obj_type)
        if r_class:
            try:
                fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
            except NoIdError:
                bottle.abort(404, 'UUID ' + obj_uuid + ' not found')
            (read_ok, read_result) = self._db_conn.dbe_read(obj_type, request.json)
            if not read_ok:
                (code, msg) = read_result
                self.config_object_error(obj_uuid, None, obj_type, 'ref_update', msg)
                bottle.abort(code, msg)

            obj_dict = read_result
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
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type.replace('-', '_')
        fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        apiConfig.identifier_name=':'.join(fq_name)
        apiConfig.identifier_uuid = obj_uuid
        apiConfig.operation = 'ref-update'
        apiConfig.body = str(request.json)

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
        if resource_type in self._resource_classes:
            return self._resource_classes[resource_type]

        return None
    # end get_resource_class

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

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'reset_config': False,
            'wipe_config': False,
            'listen_ip_addr': _WEB_HOST,
            'listen_port': _WEB_PORT,
            'admin_port': _ADMIN_PORT,
            'ifmap_server_ip': '127.0.0.1',
            'ifmap_server_port': "8443",
            'collectors': None,
            'http_server_port': '8084',
            'log_local': True,
            'log_level': SandeshLevel.SYS_NOTICE,
            'log_category': '',
            'log_file': Sandesh._DEFAULT_LOG_FILE,
            'use_syslog': False,
            'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
            'logging_level': 'WARN',
            'logging_conf': '',
            'multi_tenancy': True,
            'disc_server_ip': None,
            'disc_server_port': '5998',
            'zk_server_ip': '127.0.0.1:2181',
            'worker_id': '0',
            'rabbit_server': 'localhost',
            'rabbit_port': '5672',
            'rabbit_user': 'guest',
            'rabbit_password': 'guest',
            'rabbit_vhost': None,
            'rabbit_max_pending_updates': '4096',
            'cluster_id': '',
        }
        # ssl options
        secopts = {
            'use_certs': False,
            'keyfile': '',
            'certfile': '',
            'ca_certs': '',
            'ifmap_certauth_port': "8444",
        }
        # keystone options
        ksopts = {
            'auth_host': '127.0.0.1',
            'auth_port': '35357',
            'auth_protocol': 'http',
            'admin_user': '',
            'admin_password': '',
            'admin_tenant_name': '',
            'insecure': True
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser({'admin_token': None})
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            if 'multi_tenancy' in config.options('DEFAULTS'):
                defaults['multi_tenancy'] = config.getboolean(
                    'DEFAULTS', 'multi_tenancy')
            if 'SECURITY' in config.sections() and\
                    'use_certs' in config.options('SECURITY'):
                if config.getboolean('SECURITY', 'use_certs'):
                    secopts.update(dict(config.items("SECURITY")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))
            if 'QUOTA' in config.sections():
                for (k, v) in config.items("QUOTA"):
                    try:
                        if str(k) != 'admin_token':
                            QuotaHelper.default_quota[str(k)] = int(v)
                    except ValueError:
                        pass
            if 'default_encoding' in config.options('DEFAULTS'):
                default_encoding = config.get('DEFAULTS', 'default_encoding')
                gen.resource_xsd.ExternalEncoding = default_encoding

        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        defaults.update(secopts)
        defaults.update(ksopts)
        parser.set_defaults(**defaults)

        parser.add_argument(
            "--ifmap_server_ip", help="IP address of ifmap server")
        parser.add_argument(
            "--ifmap_server_port", help="Port of ifmap server")

        # TODO should be from certificate
        parser.add_argument(
            "--ifmap_username",
            help="Username known to ifmap server")
        parser.add_argument(
            "--ifmap_password",
            help="Password known to ifmap server")
        parser.add_argument(
            "--cassandra_server_list",
            help="List of cassandra servers in IP Address:Port format",
            nargs='+')
        parser.add_argument(
            "--redis_server_ip",
            help="IP address of redis server")
        parser.add_argument(
            "--redis_server_port",
            help="Port of redis server")
        parser.add_argument(
            "--auth", choices=['keystone'],
            help="Type of authentication for user-requests")
        parser.add_argument(
            "--reset_config", action="store_true",
            help="Warning! Destroy previous configuration and start clean")
        parser.add_argument(
            "--wipe_config", action="store_true",
            help="Warning! Destroy previous configuration")
        parser.add_argument(
            "--listen_ip_addr",
            help="IP address to provide service on, default %s" % (_WEB_HOST))
        parser.add_argument(
            "--listen_port",
            help="Port to provide service on, default %s" % (_WEB_PORT))
        parser.add_argument(
            "--admin_port",
            help="Port with local auth for admin access, default %s"
                  % (_ADMIN_PORT))
        parser.add_argument(
            "--collectors",
            help="List of VNC collectors in ip:port format",
            nargs="+")
        parser.add_argument(
            "--http_server_port",
            help="Port of local HTTP server")
        parser.add_argument(
            "--ifmap_server_loc",
            help="Location of IFMAP server")
        parser.add_argument(
            "--log_local", action="store_true",
            help="Enable local logging of sandesh messages")
        parser.add_argument(
            "--log_level",
            help="Severity level for local logging of sandesh messages")
        parser.add_argument(
            "--logging_level",
            help=("Log level for python logging: DEBUG, INFO, WARN, ERROR default: %s"
                  % defaults['logging_level']))
        parser.add_argument(
            "--logging_conf",
            help=("Optional logging configuration file, default: None"))
        parser.add_argument(
            "--log_category",
            help="Category filter for local logging of sandesh messages")
        parser.add_argument(
            "--log_file",
            help="Filename for the logs to be written to")
        parser.add_argument("--use_syslog",
            action="store_true",
            help="Use syslog for logging")
        parser.add_argument("--syslog_facility",
            help="Syslog facility to receive log lines")
        parser.add_argument(
            "--multi_tenancy", action="store_true",
            help="Validate resource permissions (implies token validation)")
        parser.add_argument(
            "--worker_id",
            help="Worker Id")
        parser.add_argument(
            "--zk_server_ip",
            help="Ip address:port of zookeeper server")
        parser.add_argument(
            "--rabbit_server",
            help="Rabbitmq server address")
        parser.add_argument(
            "--rabbit_port",
            help="Rabbitmq server port")
        parser.add_argument(
            "--rabbit_user",
            help="Username for rabbit")
        parser.add_argument(
            "--rabbit_vhost",
            help="vhost for rabbit")
        parser.add_argument(
            "--rabbit_password",
            help="password for rabbit")
        parser.add_argument(
            "--rabbit_max_pending_updates",
            help="Max updates before stateful changes disallowed")
        parser.add_argument(
            "--cluster_id",
            help="Used for database keyspace separation")
        self._args = parser.parse_args(remaining_argv)
        self._args.config_sections = config
        if type(self._args.cassandra_server_list) is str:
            self._args.cassandra_server_list =\
                self._args.cassandra_server_list.split()
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
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
                conf_sections=conf_sections)
            self._extension_mgrs['resourceApi'] = ExtensionManager(
                'vnc_cfg_api.resourceApi',
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections)
            self._extension_mgrs['neutronApi'] = ExtensionManager(
                'vnc_cfg_api.neutronApi',
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections)
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
        ifmap_loc = self._args.ifmap_server_loc
        zk_server = self._args.zk_server_ip
        rabbit_server = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost


        db_conn = VncDbClient(self, ifmap_ip, ifmap_port, user, passwd,
                              cass_server_list, rabbit_server, rabbit_port,
                              rabbit_user, rabbit_password, rabbit_vhost,
                              reset_config, ifmap_loc, zk_server, self._args.cluster_id)
        self._db_conn = db_conn
    # end _db_connect

    def _ensure_id_perms_present(self, obj_type, obj_dict):
        """
        Called at resource creation to ensure that id_perms is present in obj
        """
        new_id_perms = self._get_default_id_perms(obj_type)

        if (('id_perms' not in obj_dict) or
                (obj_dict['id_perms'] is None)):
            obj_dict['id_perms'] = new_id_perms
            return

        # Start from default and update from obj_dict
        req_id_perms = obj_dict['id_perms']
        for key in ('enable', 'description', 'user_visible'):
            if key in req_id_perms:
                new_id_perms[key] = req_id_perms[key]
        # TODO handle perms present in req_id_perms

        obj_dict['id_perms'] = new_id_perms
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
        apiConfig.url = request.url
        apiConfig.remote_ip = request.headers.get('Host')
        useragent = request.headers.get('X-Contrail-Useragent')
        if not useragent:
            useragent = request.headers.get('User-Agent')
        apiConfig.useragent = useragent
        apiConfig.user = request.headers.get('X-User-Name')
        apiConfig.project = request.headers.get('X-Project-Name')
        apiConfig.domain = request.headers.get('X-Domain-Name')
        if int(request.headers.get('Content-Length', 0)) > 0:
            apiConfig.body = str(request.json)
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

        # Ensure object has atleast default permissions set
        self._ensure_id_perms_present(obj_type, obj_dict)

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

    def publish(self):
        # publish API server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.listen_port,
        }
        self.api_server_task = self._disc.publish(
            ModuleNames[Module.API_SERVER], data)

        # publish ifmap server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.ifmap_server_port,
        }
        self.ifmap_task = self._disc.publish(
            ModuleNames[Module.IFMAP_SERVER], data)
    # end

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
    if vnc_api_server._args.disc_server_ip and\
            vnc_api_server._args.disc_server_port:
        vnc_api_server.publish()

    """ @sigchld
    Disable handling of SIG_CHLD for now as every keystone request to validate
    token sends SIG_CHLD signal to API server.
    """
    #hub.signal(signal.SIGCHLD, vnc_api_server.sigchld_handler)
    hub.signal(signal.SIGTERM, vnc_api_server.sigterm_handler)

    try:
        bottle.run(app=pipe_start_app, host=server_ip, port=server_port,
                   server='gevent')
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
