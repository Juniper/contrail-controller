#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys

import logging
import tempfile
from pprint import pformat
import coverage
import fixtures
import testtools
from testtools import content, content_type
from flexmock import flexmock, Mock
from webtest import TestApp

from vnc_api.vnc_api import *
import cfgm_common.ifmap.client as ifmap_client
import cfgm_common.ifmap.response as ifmap_response
import kombu
import discoveryclient.client as disc_client
import cfgm_common.zkclient
from cfgm_common.uve.vnc_api.ttypes import VncApiConfigLog, VncApiError
from cfgm_common import imid

from test_utils import *
import bottle
bottle.catchall=False

import inspect
import novaclient
import novaclient.client

import gevent.wsgi

def lineno():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno
# end lineno


# import from package for non-api server test or directly from file
sys.path.insert(0, '../../../../build/debug/api-lib/vnc_api')
sys.path.insert(0, '../../../../distro/openstack/')
sys.path.append('../../../../build/debug/config/api-server/vnc_cfg_api_server')
import vnc_cfg_api_server
if not hasattr(vnc_cfg_api_server, 'main'):
    from vnc_cfg_api_server import vnc_cfg_api_server


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

def launch_api_server(listen_ip, listen_port, http_server_port, admin_port,
                      conf_sections):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--admin_port %s " % (admin_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_file api_server_sandesh.log "

    import cgitb
    cgitb.enable(format='text')

    with tempfile.NamedTemporaryFile() as conf, tempfile.NamedTemporaryFile() as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str = args_str + "--conf_file %s " %(conf.name)
        args_str = args_str + "--logging_conf %s " %(logconf.name)
        vnc_cfg_api_server.main(args_str)
#end launch_api_server

def launch_svc_monitor(api_server_ip, api_server_port):
    import svc_monitor
    if not hasattr(svc_monitor, 'main'):
        from svc_monitor import svc_monitor

    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--ifmap_username api-server "
    args_str = args_str + "--ifmap_password api-server "
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_file svc_monitor.log "

    svc_monitor.main(args_str)
# end launch_svc_monitor

def launch_schema_transformer(api_server_ip, api_server_port):
    try:
        import to_bgp
    except ImportError:
        from schema_transformer import to_bgp
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_file schema_transformer.log "
    to_bgp.main(args_str)
# end launch_schema_transformer

def setup_extra_flexmock(mocks):
    for (cls, method_name, val) in mocks:
        kwargs = {method_name: val}
        flexmock(cls, **kwargs)
# end setup_extra_flexmock

def setup_common_flexmock():
    flexmock(novaclient.client, Client=FakeNovaClient.initialize)
    flexmock(ifmap_client.client, __init__=FakeIfmapClient.initialize,
             call=FakeIfmapClient.call,
             call_async_result=FakeIfmapClient.call_async_result)

    flexmock(pycassa.system_manager.Connection, __init__=stub)
    flexmock(pycassa.system_manager.SystemManager, create_keyspace=stub,
             create_column_family=stub)
    flexmock(pycassa.ConnectionPool, __init__=stub)
    flexmock(pycassa.ColumnFamily, __new__=FakeCF)
    flexmock(pycassa.util, convert_uuid_to_time=Fake_uuid_to_time)

    flexmock(disc_client.DiscoveryClient, __init__=stub)
    flexmock(disc_client.DiscoveryClient, publish_obj=stub)
    flexmock(kazoo.client.KazooClient, __new__=FakeKazooClient)
    flexmock(kazoo.handlers.gevent.SequentialGeventHandler, __init__=stub)

    flexmock(kombu.Connection, __new__=FakeKombu.Connection)
    flexmock(kombu.Exchange, __new__=FakeKombu.Exchange)
    flexmock(kombu.Queue, __new__=FakeKombu.Queue)

    flexmock(VncApiConfigLog, __new__=FakeApiConfigLog)
#end setup_common_flexmock

cov_handle = None
class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    _HTTP_HEADERS =  {
        'Content-type': 'application/json; charset="UTF-8"',
    }
    def __init__(self, *args, **kwargs):
        self._logger = logging.getLogger(__name__)
        self._assert_till_max_tries = 30
        self._config_knobs = [
            ('DEFAULTS', '', ''),
            ]
        super(TestCase, self).__init__(*args, **kwargs)

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
        response = self._api_server_session.get(url, headers = headers,
                                                params = query_params)
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

    def ifmap_has_ident(self, obj=None, id=None):
        if obj:
            _type = obj.get_type()
            _fq_name = obj.get_fq_name()
        if id:
            _type = self._vnc_lib.id_to_fq_name_type(id)
            _fq_name = self._vnc_lib.id_to_fq_name(id)

        ifmap_id = imid.get_ifmap_id_from_fq_name(_type, _fq_name)
        if ifmap_id in FakeIfmapClient._graph:
            return True

        return False

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
            gevent.sleep(2)


    def setUp(self):
        super(TestCase, self).setUp()
        global cov_handle
        if not cov_handle:
            cov_handle = coverage.coverage(source=['./'], omit=['.venv/*'])
        cov_handle.start()

        cfgm_common.zkclient.LOG_DIR = './'
        gevent.wsgi.WSGIServer.handler_class = FakeWSGIHandler
        setup_common_flexmock()

        self._api_server_ip = socket.gethostbyname(socket.gethostname())
        self._api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_admin_port = get_free_port()
        self._api_svr_greenlet = gevent.spawn(launch_api_server,
                                     self._api_server_ip, self._api_server_port,
                                     http_server_port, self._api_admin_port,
                                     self._config_knobs)
        block_till_port_listened(self._api_server_ip, self._api_server_port)
        extra_env = {'HTTP_HOST':'%s%s' %(self._api_server_ip,
                                          self._api_server_port)}
        self._api_svr_app = TestApp(bottle.app(), extra_environ=extra_env)
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

        FakeNovaClient.vnc_lib = self._vnc_lib
        self._api_server_session = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._api_server_session.mount("http://", adapter)
        self._api_server_session.mount("https://", adapter)
        self._api_server = vnc_cfg_api_server.server
        self._api_server._sandesh.set_logging_params(level="SYS_WARN")
    # end setUp

    def tearDown(self):
        self._api_svr_greenlet.kill()
        self._api_server._db_conn._msgbus._dbe_publish_greenlet.kill()
        self._api_server._db_conn._msgbus._dbe_oper_subscribe_greenlet.kill()
        FakeIfmapClient.reset()
        cov_handle.stop()
        cov_handle.report(file=open('covreport.txt', 'w'))
        super(TestCase, self).tearDown()
    # end tearDown

    def get_obj_imid(self, obj):
        return 'contrail:%s:%s' %(obj._type, obj.get_fq_name_str())
    # end get_obj_imid

# end TestCase
