#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent.monkey
gevent.monkey.patch_all()

import logging
import tempfile
from pprint import pformat
import coverage
import fixtures
import testtools
from testtools import content
from flexmock import flexmock
from webtest import TestApp
import contextlib
from lxml import etree

from vnc_api.vnc_api import *
import kombu
import discoveryclient.client as disc_client
import cfgm_common.zkclient
from cfgm_common.uve.vnc_api.ttypes import VncApiConfigLog
from cfgm_common import imid
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook

from test_utils import *
import bottle
bottle.catchall=False

import inspect
import novaclient
import novaclient.client

import gevent.wsgi
import uuid

def lineno():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno
# end lineno


# import from package for non-api server test or directly from file
sys.path.insert(0, '../../../../build/production/api-lib/vnc_api')
sys.path.insert(0, '../../../../distro/openstack/')
sys.path.append('../../../../build/production/config/api-server/vnc_cfg_api_server')
sys.path.append("../config/api-server/vnc_cfg_api_server")
sys.path.insert(0, '../../../../build/production/discovery/discovery')

try:
    import vnc_cfg_api_server
    if not hasattr(vnc_cfg_api_server, 'main'):
        from vnc_cfg_api_server import vnc_cfg_api_server
except ImportError:
    vnc_cfg_api_server = 'vnc_cfg_api_server could not be imported'

try:
    import to_bgp
except ImportError:
    try:
        from schema_transformer import to_bgp
    except ImportError:
        to_bgp = 'to_bgp could not be imported'

try:
    import svc_monitor
    if not hasattr(svc_monitor, 'main'):
        from svc_monitor import svc_monitor
except ImportError:
    svc_monitor = 'svc_monitor could not be imported'

try:
    import device_manager
    if not hasattr(device_manager, 'main'):
        from device_manager import device_manager
except ImportError:
    device_manager = 'device_manager could not be imported'

try:
    from discovery import disc_server
    if not hasattr(disc_server, 'main'):
        from disc_server import disc_server
except ImportError:
    disc_server = 'disc_server could not be imported'

def generate_conf_file_contents(conf_sections):
    cfg_parser = ConfigParser.RawConfigParser()
    for (section, var, val) in conf_sections:
        try:
            cfg_parser.add_section(section)
        except ConfigParser.DuplicateSectionError:
            pass
        if not var:
            continue
        if val == '':
            cfg_parser.set(section, var, 'empty')
        else:
            cfg_parser.set(section, var, val)

    return cfg_parser
# end generate_conf_file_contents

def generate_logconf_file_contents():
    cfg_parser = ConfigParser.RawConfigParser()

    cfg_parser.add_section('formatters')
    cfg_parser.add_section('formatter_simple')
    cfg_parser.set('formatters', 'keys', 'simple')
    cfg_parser.set('formatter_simple', 'format', '%(name)s:%(levelname)s: %(message)s')

    cfg_parser.add_section('handlers')
    cfg_parser.add_section('handler_console')
    cfg_parser.add_section('handler_api_server_file')
    cfg_parser.set('handlers', 'keys', 'console,api_server_file')
    cfg_parser.set('handler_console', 'class', 'StreamHandler')
    cfg_parser.set('handler_console', 'level', 'WARN')
    cfg_parser.set('handler_console', 'args', '[]')
    cfg_parser.set('handler_console', 'formatter', 'simple')
    cfg_parser.set('handler_api_server_file', 'class', 'FileHandler')
    cfg_parser.set('handler_api_server_file', 'level', 'INFO')
    cfg_parser.set('handler_api_server_file', 'formatter', 'simple')
    cfg_parser.set('handler_api_server_file', 'args', "('api_server.log',)")

    cfg_parser.add_section('loggers')
    cfg_parser.add_section('logger_root')
    cfg_parser.add_section('logger_FakeWSGIHandler')
    cfg_parser.set('loggers', 'keys', 'root,FakeWSGIHandler')
    cfg_parser.set('logger_root', 'level', 'WARN')
    cfg_parser.set('logger_root', 'handlers', 'console')
    cfg_parser.set('logger_FakeWSGIHandler', 'level', 'INFO')
    cfg_parser.set('logger_FakeWSGIHandler', 'qualname', 'FakeWSGIHandler')
    cfg_parser.set('logger_FakeWSGIHandler', 'handlers', 'api_server_file')

    return cfg_parser
# end generate_logconf_file_contents

def launch_disc_server(test_id, listen_ip, listen_port, http_server_port, conf_sections):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--ttl_min 30 "
    args_str = args_str + "--ttl_max 60 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file discovery_server_%s.log " % test_id

    vnc_cgitb.enable(format='text')

    with tempfile.NamedTemporaryFile() as conf, tempfile.NamedTemporaryFile() as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str = args_str + "--conf_file %s " %(conf.name)
        disc_server.main(args_str)
#end launch_disc_server

class VncTestApp(TestApp):
    def post_json(self, *args, **kwargs):
        resp = super(VncTestApp, self).post_json(*args, **kwargs)
        resp.charset = 'UTF-8'
        return resp
#end class VncTestApp

def create_api_server_instance(test_id, config_knobs):
    ret_server_info = {}
    allocated_sockets = []
    ret_server_info['ip'] = socket.gethostbyname(socket.gethostname())
    ret_server_info['service_port'] = get_free_port(allocated_sockets)
    ret_server_info['introspect_port'] = get_free_port(allocated_sockets)
    ret_server_info['admin_port'] = get_free_port(allocated_sockets)
    ret_server_info['ifmap_port'] = get_free_port(allocated_sockets)
    ret_server_info['allocated_sockets'] = allocated_sockets
    with_irond = [conf for conf in config_knobs
                  if (conf[0] == 'DEFAULTS' and conf[1] == 'ifmap_server_ip')]
    if with_irond:
        ret_server_info['ifmap_server_ip'] = with_irond[0][2]
    ret_server_info['greenlet'] = gevent.spawn(launch_api_server,
        test_id, ret_server_info['ip'], ret_server_info['service_port'],
        ret_server_info['introspect_port'], ret_server_info['admin_port'],
        ret_server_info['ifmap_port'], config_knobs,
        ret_server_info.get('ifmap_server_ip'))
    block_till_port_listened(ret_server_info['ip'],
        ret_server_info['service_port'])
    extra_env = {'HTTP_HOST': ret_server_info['ip'],
                 'SERVER_PORT': str(ret_server_info['service_port'])}
    api_server_obj = ret_server_info['greenlet'].api_server
    ret_server_info['app'] = VncTestApp(api_server_obj.api_bottle,
                                        extra_environ=extra_env)
    ret_server_info['api_conn'] = VncApi('u', 'p',
        api_server_host=ret_server_info['ip'],
        api_server_port=ret_server_info['service_port'])

    if FakeNovaClient.vnc_lib is None:
        FakeNovaClient.vnc_lib = ret_server_info['api_conn']
    ret_server_info['api_session'] = requests.Session()
    adapter = requests.adapters.HTTPAdapter()
    ret_server_info['api_session'].mount("http://", adapter)
    ret_server_info['api_session'].mount("https://", adapter)
    ret_server_info['api_server'] = api_server_obj
    ret_server_info['api_server']._sandesh.set_logging_level(level="SYS_DEBUG")

    return ret_server_info
# end create_api_server_instance

def destroy_api_server_instance(server_info):
    server_info['greenlet'].kill()
    server_info['api_server']._db_conn._msgbus.shutdown()
    vhost_url = server_info['api_server']._db_conn._msgbus._urls
    FakeKombu.reset(vhost_url)
    FakeNovaClient.reset()
    if server_info.get('ifmap_server_ip') is not None:
        FakeIfmapClient.reset(server_info['ifmap_port'])
    else:
        vnc_cfg_api_server.VncIfmapServer.reset_graph()
    CassandraCFs.reset()
    FakeKazooClient.reset()
    FakeExtensionManager.reset()
    for sock in server_info['allocated_sockets']:
        sock.close()
# end destroy_api_server_instance

def destroy_api_server_instance_issu(server_info):
    server_info['greenlet'].kill()
    server_info['api_server']._db_conn._msgbus.shutdown()
    vhost_url = server_info['api_server']._db_conn._msgbus._urls
    for sock in server_info['allocated_sockets']:
        sock.close()
# end destroy_api_server_instance

def launch_api_server(test_id, listen_ip, listen_port, http_server_port,
                      admin_port, ifmap_port, conf_sections,
                      ifmap_server_ip=None):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--admin_port %s " % (admin_port)
    if ifmap_server_ip is not None:
        args_str = args_str + "--ifmap_server_ip %s " % ifmap_server_ip
        args_str = args_str + "--ifmap_server_port %s " % ifmap_port
    else:
        args_str = args_str + "--ifmap_listen_ip %s " % listen_ip
        args_str = args_str + "--ifmap_listen_port %s " % ifmap_port
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file api_server_%s.log " %(test_id)

    vnc_cgitb.enable(format='text')

    with tempfile.NamedTemporaryFile() as conf, \
         tempfile.NamedTemporaryFile() as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str = args_str + "--conf_file %s " %(conf.name)
        args_str = args_str + "--logging_conf %s " %(logconf.name)
        server = vnc_cfg_api_server.VncApiServer(args_str)
        gevent.getcurrent().api_server = server
        vnc_cfg_api_server.main(args_str, server)
#end launch_api_server

def launch_svc_monitor(test_id, api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port())
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file svc_monitor_%s.log " %(test_id)
    args_str = args_str + "--check_service_interval 2 "

    svc_monitor.main(args_str)
# end launch_svc_monitor

def kill_svc_monitor(glet):
    glet.kill()
    svc_monitor.SvcMonitor.reset()

def kill_schema_transformer(glet):
    glet.kill()
    to_bgp.transformer.reset()

def kill_disc_server(glet):
    glet.kill()

def launch_schema_transformer(test_id, api_server_ip, api_server_port, extra_args=None):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port())
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file schema_transformer_%s.log " %(test_id)
    args_str = args_str + "--trace_file schema_transformer_%s.err " %(test_id)
    if extra_args:
        args_str = args_str + (extra_args)
    to_bgp.main(args_str)
# end launch_schema_transformer

def launch_device_manager(test_id, api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port())
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file device_manager_%s.log " %(test_id)
    device_manager.main(args_str)
# end launch_device_manager

@contextlib.contextmanager
def flexmocks(mocks):
    orig_values = {}
    try:
        for cls, method_name, val in mocks:
            kwargs = {method_name: val}
            # save orig cls.method_name
            orig_values[(cls, method_name)] = getattr(cls, method_name)
            flexmock(cls, **kwargs)
        yield
    finally:
        for (cls, method_name), method in orig_values.items():
            setattr(cls, method_name, method)
# end flexmocks

def setup_extra_flexmock(mocks):
    for (cls, method_name, val) in mocks:
        kwargs = {method_name: val}
        flexmock(cls, **kwargs)
# end setup_extra_flexmock

def setup_mocks(mod_attr_val_list):
    # use setattr instead of flexmock because flexmocks are torndown
    # after every test in stopTest whereas these mocks are needed across
    # all tests in class
    orig_mod_attr_val_list = []
    for mod, attr, val in mod_attr_val_list:
        orig_mod_attr_val_list.append(
            (mod, attr, getattr(mod, attr)))
        setattr(mod, attr, val)

    return orig_mod_attr_val_list
#end setup_mocks

def teardown_mocks(mod_attr_val_list):
    for mod, attr, val in mod_attr_val_list:
        setattr(mod, attr, val)
# end teardown_mocks

@contextlib.contextmanager
def patch(target_obj, target_method_name, patched):
    orig_method = getattr(target_obj, target_method_name)
    def patched_wrapper(*args, **kwargs):
        return patched(orig_method, *args, **kwargs)

    setattr(target_obj, target_method_name, patched_wrapper)
    try:
        yield
    finally:
        setattr(target_obj, target_method_name, orig_method)
#end patch

@contextlib.contextmanager
def patch_imports(imports):
    # save original, patch and restore
    orig_modules = {}
    mocked_modules = []
    try:
        for import_str, fake in imports:
           cur_module = None
           for mod_part in import_str.split('.'):
               if not cur_module:
                   cur_module = mod_part
               else:
                   cur_module += "." + mod_part
               if cur_module in sys.modules:
                   orig_modules[cur_module] = sys.modules[cur_module]
               else:
                   mocked_modules.append(cur_module)
               sys.modules[cur_module] = fake
        yield
    finally:
        for mod_name, mod in orig_modules.items():
            sys.modules[mod_name] = mod
        for mod_name in mocked_modules:
            del sys.modules[mod_name]

#end patch_import

cov_handle = None
class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    _HTTP_HEADERS =  {
        'Content-type': 'application/json; charset="UTF-8"',
    }
    _config_knobs = [
        ('DEFAULTS', '', ''),
        ]

    mocks = [
        (novaclient.client, 'Client', FakeNovaClient.initialize),

        (pycassa.system_manager.Connection, '__init__',stub),
        (pycassa.system_manager.SystemManager, '__new__',FakeSystemManager),
        (pycassa.ConnectionPool, '__new__',FakeConnectionPool),
        (pycassa.ColumnFamily, '__new__',FakeCF),
        (pycassa.util, 'convert_uuid_to_time',Fake_uuid_to_time),

        (disc_client.DiscoveryClient, '__init__',stub),
        (disc_client.DiscoveryClient, 'publish_obj',stub),
        (disc_client.DiscoveryClient, 'publish',stub),
        (disc_client.DiscoveryClient, 'subscribe',stub),
        (disc_client.DiscoveryClient, 'syslog',stub),

        (kazoo.client.KazooClient, '__new__',FakeKazooClient),
        (kazoo.handlers.gevent.SequentialGeventHandler, '__init__',stub),

        (kombu.Connection, '__new__',FakeKombu.Connection),
        (kombu.Exchange, '__new__',FakeKombu.Exchange),
        (kombu.Queue, '__new__',FakeKombu.Queue),
        (kombu.Consumer, '__new__',FakeKombu.Consumer),
        (kombu.Producer, '__new__',FakeKombu.Producer),

        (VncApiConfigLog, '__new__',FakeApiConfigLog),
    ]

    def __init__(self, *args, **kwargs):
        self._logger = logging.getLogger(__name__)
        self._assert_till_max_tries = 600
        super(TestCase, self).__init__(*args, **kwargs)
        self.addOnException(self._add_detailed_traceback)

    def _add_detailed_traceback(self, exc_info):
        vnc_cgitb.enable(format='text')
        from cStringIO  import StringIO

        tmp_file = StringIO()
        cgitb_hook(format="text", file=tmp_file, info=exc_info)
        tb_str = tmp_file.getvalue()
        tmp_file.close()
        self.addDetail('detailed-traceback', content.text_content(tb_str))

    def _add_detail(self, detail_str):
        frame = inspect.stack()[1]
        self.addDetail('%s:%s ' %(frame[1],frame[2]), content.text_content(detail_str))

    def _add_request_detail(self, op, url, headers=None, query_params=None,
                         body=None):
        request_str = ' URL: ' + pformat(url) + \
                      ' OPER: ' + pformat(op) + \
                      ' Headers: ' + pformat(headers) + \
                      ' Query Params: ' + pformat(query_params) + \
                      ' Body: ' + pformat(body)
        self._add_detail('Requesting: ' + request_str)

    def _http_get(self, uri, query_params=None):
        url = "http://%s:%s%s" % (self._api_server_ip, self._api_server_port, uri)
        self._add_request_detail('GET', url, headers=self._HTTP_HEADERS,
                                 query_params=query_params)
        response = self._api_server_session.get(url, headers=self._HTTP_HEADERS,
                                                params=query_params)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_get

    def _http_post(self, uri, body):
        url = "http://%s:%s%s" % (self._api_server_ip, self._api_server_port, uri)
        self._add_request_detail('POST', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.post(url, data=body,
                                                 headers=self._HTTP_HEADERS)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_post

    def _http_delete(self, uri, body):
        url = "http://%s:%s%s" % (self._api_server_ip, self._api_server_port, uri)
        self._add_request_detail('DELETE', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.delete(url, data=body,
                                                   headers=self._HTTP_HEADERS)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_delete

    def _http_put(self, uri, body):
        url = "http://%s:%s%s" % (self._api_server_ip, self._api_server_port, uri)
        self._add_request_detail('PUT', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.put(url, data=body,
                                                headers=self._HTTP_HEADERS)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_put

    def _create_test_objects(self, count=1):
        ret_objs = []
        for i in range(count):
            obj_name = self.id() + '-vn-' + str(i)
            obj = VirtualNetwork(obj_name)
            self._add_detail('creating-object ' + obj_name)
            self._vnc_lib.virtual_network_create(obj)
            ret_objs.append(obj)

        return ret_objs

    def _create_test_object(self):
        return self._create_test_objects()[0]

    def _delete_test_object(self, obj):
        self._vnc_lib.virtual_network_delete(id=obj.uuid)

    def ifmap_has_ident(self, obj=None, id=None, type_fq_name=None):
        if obj:
            _type = obj.get_type()
            _fq_name = obj.get_fq_name()
        if id:
            _type = self._vnc_lib.id_to_fq_name_type(id)
            _fq_name = self._vnc_lib.id_to_fq_name(id)
        if type_fq_name:
            _type = type_fq_name[0]
            _fq_name = type_fq_name[1]

        ifmap_id = imid.get_ifmap_id_from_fq_name(_type, _fq_name)
        if self._ifmap_server_ip is not None:
            port = str(self._api_ifmap_port)
            if ifmap_id in FakeIfmapClient._graph[port]:
                # Old ifmap fake client store identity and link in lxml object
                # in memory, we need to convert them in string
                return self._xml_to_string(
                    FakeIfmapClient._graph[port][ifmap_id])
        else:
            if ifmap_id in vnc_cfg_api_server.VncIfmapServer._graph:
                return vnc_cfg_api_server.VncIfmapServer._graph[ifmap_id]

        return None

    @staticmethod
    def _xml_to_string(xml_ifmap_dict):
        string_ifmap_dict = {}
        if 'ident' in xml_ifmap_dict:
            string_ifmap_dict['ident'] = etree.tostring(xml_ifmap_dict['ident'])
        if 'links' in xml_ifmap_dict:
            string_ifmap_dict['links'] = dict()
            for meta_name, meta in xml_ifmap_dict['links'].items():
                string_ifmap_dict['links'][meta_name] = dict()
                if 'meta' in meta:
                    string_ifmap_dict['links'][meta_name]['meta'] = etree.tostring(meta['meta'])
                if 'other' in meta:
                    string_ifmap_dict['links'][meta_name]['other'] = etree.tostring(meta['other'])
        return string_ifmap_dict

    def ifmap_ident_has_link(self, obj=None, id=None, type_fq_name=None,
                             link_name=None):
        ifmap_dict = self.ifmap_has_ident(obj=obj, id=id,
                                          type_fq_name=type_fq_name)

        if ifmap_dict is None:
            return None

        match = [s for s in ifmap_dict['links'] if link_name in s]
        if len(match) == 1:
            return ifmap_dict['links'][match[0]]

    def ifmap_doesnt_have_ident(self, obj=None, id=None, type_fq_name=None):
        return not self.ifmap_has_ident(obj=obj, id=id,
                                        type_fq_name=type_fq_name)

    def ifmap_ident_doesnt_have_link(self, obj=None, id=None,
                                     type_fq_name=None, link_name=None):
        return not self.ifmap_ident_has_link(obj=obj, id=id,
                                             type_fq_name=type_fq_name,
                                             link_name=link_name)

    def assertTill(self, expr_or_cb, *cb_args, **cb_kwargs):
        tries = 0
        while True:
            if callable(expr_or_cb):
                ret = expr_or_cb(*cb_args, **cb_kwargs)
            else:
                ret = eval(expr_or_cb)

            if ret:
                break

            tries = tries + 1
            if tries >= self._assert_till_max_tries:
                raise Exception('Max retries')

            self._logger.warn('Retrying at ' + str(inspect.stack()[1]))
            gevent.sleep(0.1)


    @classmethod
    def setUpClass(cls, extra_mocks=None, extra_config_knobs=None):
        super(TestCase, cls).setUpClass()
        global cov_handle
        if not cov_handle:
            cov_handle = coverage.coverage(source=['./'], omit=['.venv/*'])
        #cov_handle.start()

        cfgm_common.zkclient.LOG_DIR = './'
        gevent.wsgi.WSGIServer.handler_class = FakeWSGIHandler

        cls.orig_mocked_values = setup_mocks(cls.mocks + (extra_mocks or []))

        cls._server_info = create_api_server_instance(
            cls.__name__, cls._config_knobs + (extra_config_knobs or []))
        try:
            cls._api_server_ip = cls._server_info['ip']
            cls._api_server_port = cls._server_info['service_port']
            cls._api_admin_port = cls._server_info['admin_port']
            cls._api_ifmap_port = cls._server_info['ifmap_port']
            cls._api_svr_greenlet = cls._server_info['greenlet']
            cls._api_svr_app = cls._server_info['app']
            cls._vnc_lib = cls._server_info['api_conn']
            cls._api_server_session = cls._server_info['api_session']
            cls._api_server = cls._server_info['api_server']
            cls._ifmap_server_ip = cls._server_info.get('ifmap_server_ip')
        except Exception as e:
            cls.tearDownClass()
            raise
    # end setUpClass

    @classmethod
    def tearDownClass(cls):
        destroy_api_server_instance(cls._server_info)
        teardown_mocks(cls.orig_mocked_values)
    # end tearDownClass

    def setUp(self, extra_mocks=None, extra_config_knobs=None):
        self._logger.info("Running %s" %(self.id()))
        super(TestCase, self).setUp()
    # end setUp

    def tearDown(self):
        self._logger.info("Finished %s" %(self.id()))
        self.wait_till_api_server_idle()
        super(TestCase, self).tearDown()
    # end tearDown

    def wait_till_api_server_idle(self):
        # wait for in-flight messages to be processed
        while self._api_server._db_conn._msgbus.num_pending_messages() > 0:
            gevent.sleep(0.001)
        vhost_url = self._api_server._db_conn._msgbus._urls
        while not FakeKombu.is_empty(vhost_url, 'vnc_config'):
            gevent.sleep(0.001)
        if self._ifmap_server_ip is not None:
            while self._api_server._db_conn._ifmap_db._queue.qsize() > 0:
                gevent.sleep(0.001)
    # wait_till_api_server_idle

    def get_obj_imid(self, obj):
        return imid.get_ifmap_id_from_fq_name(obj._type, obj.get_fq_name_str())
    # end get_obj_imid

    def create_virtual_network(self, vn_name, vn_subnet):
        vn_obj = VirtualNetwork(name=vn_name)
        ipam_fq_name = [
            'default-domain', 'default-project', 'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnets = [vn_subnet] if isinstance(vn_subnet, basestring) else vn_subnet
        subnet_infos = []
        for subnet in subnets:
            cidr = subnet.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            subnet_infos.append(IpamSubnetType(subnet=SubnetType(pfx, pfx_len)))
        subnet_data = VnSubnetsType(subnet_infos)
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj.clear_pending_updates()
        return vn_obj
    # end create_virtual_network

    def _create_service(self, vn_list, si_name, auto_policy, **kwargs):
        sa_set = None
        if kwargs.get('service_virtualization_type') == 'physical-device':
            pr = PhysicalRouter(si_name)
            self._vnc_lib.physical_router_create(pr)
            sa_set = ServiceApplianceSet('sa_set-'+si_name)
            self._vnc_lib.service_appliance_set_create(sa_set)
            sa = ServiceAppliance('sa-'+si_name, parent_obj=sa_set)
            for if_type, _ in vn_list:
               attr = ServiceApplianceInterfaceType(interface_type=if_type)
               pi = PhysicalInterface('pi-'+si_name+if_type, parent_obj=pr)
               self._vnc_lib.physical_interface_create(pi)
               sa.add_physical_interface(pi, attr)
            self._vnc_lib.service_appliance_create(sa)
        sti = [ServiceTemplateInterfaceType(k) for k, _ in vn_list]
        st_prop = ServiceTemplateType(
            flavor='medium',
            image_name='junk',
            ordered_interfaces=True,
            interface_type=sti, **kwargs)
        service_template = ServiceTemplate(
            name=si_name + 'template',
            service_template_properties=st_prop)
        if sa_set:
            service_template.add_service_appliance_set(sa_set)
        self._vnc_lib.service_template_create(service_template)
        scale_out = ServiceScaleOutType()
        if kwargs.get('service_mode') in ['in-network', 'in-network-nat']:
            if_list = [ServiceInstanceInterfaceType(virtual_network=vn)
                       for _, vn in vn_list]
            si_props = ServiceInstanceType(auto_policy=auto_policy,
                                           interface_list=if_list,
                                           scale_out=scale_out)
        else:
            if_list = [ServiceInstanceInterfaceType(),
                       ServiceInstanceInterfaceType()]
            si_props = ServiceInstanceType(interface_list=if_list,
                                           scale_out=scale_out)
        service_instance = ServiceInstance(
            name=si_name, service_instance_properties=si_props)
        service_instance.add_service_template(service_template)
        self._vnc_lib.service_instance_create(service_instance)

        if kwargs.get('version') == 2:
            proj = Project()
            pt = PortTuple('pt-'+si_name, parent_obj=service_instance)
            self._vnc_lib.port_tuple_create(pt)
            for if_type, vn_name in vn_list:
                port = VirtualMachineInterface(si_name+if_type, parent_obj=proj)
                vmi_props = VirtualMachineInterfacePropertiesType(
                    service_interface_type=if_type)
                vn_obj = self._vnc_lib.virtual_network_read(fq_name_str=vn_name)
                port.set_virtual_machine_interface_properties(vmi_props)
                port.add_virtual_network(vn_obj)
                port.add_port_tuple(pt)
                self._vnc_lib.virtual_machine_interface_create(port)
                # Let a chance to the API to create iip for the vmi of the pt
                # before creating the si and the schema allocates an iip
                # address to the service chain
                gevent.sleep(0.1)

        return service_instance.get_fq_name_str()

    def create_network_policy(self, vn1, vn2, service_list=None, mirror_service=None,
                              auto_policy=False, **kwargs):
        vn1_name = vn1 if isinstance(vn1, basestring) else vn1.get_fq_name_str()
        vn2_name = vn2 if isinstance(vn2, basestring) else vn2.get_fq_name_str()

        addr1 = AddressType(virtual_network=vn1_name, subnet=kwargs.get('subnet_1'))
        addr2 = AddressType(virtual_network=vn2_name, subnet=kwargs.get('subnet_2'))

        port = PortType(-1, 0)
        service_name_list = []
        si_list = service_list or []
        if service_list:
            for service in si_list:
                service_name_list.append(self._create_service(
                    [('left', vn1_name), ('right', vn2_name)], service,
                     auto_policy, **kwargs))
        if mirror_service:
            mirror_si = self._create_service(
                [('left', vn1_name), ('right', vn2_name)], mirror_service, False,
                service_mode='transparent', service_type='analyzer')
        action_list = ActionListType()
        if mirror_service:
            mirror = MirrorActionType(analyzer_name=mirror_si)
            action_list.mirror_to=mirror
        if service_name_list:
            action_list.apply_service=service_name_list
        else:
            action_list.simple_action='pass'

        prule = PolicyRuleType(direction="<>", protocol="any",
                               src_addresses=[addr1], dst_addresses=[addr2],
                               src_ports=[port], dst_ports=[port],
                               action_list=action_list)
        pentry = PolicyEntriesType([prule])
        np = NetworkPolicy(str(uuid.uuid4()), network_policy_entries=pentry)
        if auto_policy:
            return np
        self._vnc_lib.network_policy_create(np)
        return np
    # end create_network_policy

# end TestCase
