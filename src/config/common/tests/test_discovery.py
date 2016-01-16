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
from testtools import content, content_type
from flexmock import flexmock, Mock
from webtest import TestApp
import contextlib
import ConfigParser

from test_utils import *
import bottle
bottle.catchall=False

import inspect
import requests

import gevent.wsgi
import uuid

def lineno():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno
# end lineno


# import from package for non-api server test or directly from file
sys.path.insert(0, '../../../../build/production/discovery/discovery')

try:
    from discovery import disc_server
except ImportError:
    disc_server = 'discovery server could not be imported'

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
    cfg_parser.add_section('handler_disc_server_file')
    cfg_parser.set('handlers', 'keys', 'console, disc_server_file')
    cfg_parser.set('handler_console', 'class', 'StreamHandler')
    cfg_parser.set('handler_console', 'level', 'DEBUG')
    cfg_parser.set('handler_console', 'args', '[]')
    cfg_parser.set('handler_console', 'formatter', 'simple')
    cfg_parser.set('handler_disc_server_file', 'class', 'FileHandler')
    cfg_parser.set('handler_disc_server_file', 'level', 'INFO')
    cfg_parser.set('handler_disc_server_file', 'formatter', 'simple')
    cfg_parser.set('handler_disc_server_file', 'args', "('disc_server.log',)")

    cfg_parser.add_section('loggers')
    cfg_parser.add_section('logger_root')
    cfg_parser.add_section('logger_FakeWSGIHandler')
    cfg_parser.set('loggers', 'keys', 'root,FakeWSGIHandler')
    cfg_parser.set('logger_root', 'level', 'DEBUG')
    cfg_parser.set('logger_root', 'handlers', 'console')
    cfg_parser.set('logger_FakeWSGIHandler', 'level', 'INFO')
    cfg_parser.set('logger_FakeWSGIHandler', 'qualname', 'FakeWSGIHandler')
    cfg_parser.set('logger_FakeWSGIHandler', 'handlers', 'disc_server_file')

    return cfg_parser
# end generate_logconf_file_contents

def launch_disc_server(listen_ip, listen_port, http_server_port, conf_sections):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--ttl_min 30 "
    args_str = args_str + "--ttl_max 60 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file discovery_server_sandesh.log "

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
        disc_server.main(args_str)
#end launch_disc_server

def setup_extra_flexmock(mocks):
    for (cls, method_name, val) in mocks:
        kwargs = {method_name: val}
        flexmock(cls, **kwargs)
# end setup_extra_flexmock

def setup_common_flexmock():
    flexmock(pycassa.system_manager.Connection, __init__=stub)
    flexmock(pycassa.system_manager.SystemManager, __new__=FakeSystemManager)
    flexmock(pycassa.ConnectionPool, __init__=stub)
    flexmock(pycassa.ColumnFamily, __new__=FakeCF)
    flexmock(pycassa.util, convert_uuid_to_time=Fake_uuid_to_time)
#end setup_common_flexmock

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

cov_handle = None
class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    def __init__(self, *args, **kwargs):
        self._logger = logging.getLogger(__name__)
        self._assert_till_max_tries = 30
        self._content_type = 'application/json; charset="UTF-8"'
        self._config_knobs = [
            ('DEFAULTS', '', ''),
            ]
        super(TestCase, self).__init__(*args, **kwargs)
        self.addOnException(self._add_detailed_traceback)

        self._http_headers =  {
            'Content-type': 'application/json; charset="UTF-8"',
        }

    def _add_detailed_traceback(self, exc_info):
        import cgitb
        cgitb.enable(format='text')
        from cStringIO  import StringIO

        tmp_file = StringIO()
        cgitb.Hook(format="text", file=tmp_file).handle(exc_info)
        tb_str = tmp_file.getvalue()
        tmp_file.close()
        self.addDetail('detailed-traceback', content.text_content(tb_str))

    def _add_detail(self, detail_str):
        frame = inspect.stack()[1]
        self.addDetail('%s:%s ' %(frame[1],frame[2]), content.text_content(detail_str))

    def _set_content_type(self, ctype):
        self._content_type = ctype
        self._http_headers['Content-type'] = self._content_type

    def _add_request_detail(self, op, url, headers=None, query_params=None,
                         body=None):
        request_str = ' URL: ' + pformat(url) + \
                      ' OPER: ' + pformat(op) + \
                      ' Headers: ' + pformat(headers) + \
                      ' Query Params: ' + pformat(query_params) + \
                      ' Body: ' + pformat(body)
        self._add_detail('Requesting: ' + request_str)

    def _http_get(self, uri, query_params=None):
        url = "http://%s:%s%s" % (self._disc_server_ip, self._disc_server_port, uri)
        self._add_request_detail('GET', url, headers=self._http_headers,
                                 query_params=query_params)
        response = self._disc_server_session.get(url, headers=self._http_headers,
                                                params=query_params)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_get

    def _http_post(self, uri, body):
        url = "http://%s:%s%s" % (self._disc_server_ip, self._disc_server_port, uri)
        self._add_request_detail('POST', url, headers=self._http_headers, body=body)
        response = self._disc_server_session.post(url, data=body,
                                                 headers=self._http_headers)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_post

    def _http_delete(self, uri, body):
        url = "http://%s:%s%s" % (self._disc_server_ip, self._disc_server_port, uri)
        self._add_request_detail('DELETE', url, headers=self._http_headers, body=body)
        response = self._disc_server_session.delete(url, data=body,
                                                   headers=self._http_headers)
        self._add_detail('Received Response: ' +
                         pformat(response.status_code) +
                         pformat(response.text))
        return (response.status_code, response.text)
    #end _http_delete

    def _http_put(self, uri, body):
        url = "http://%s:%s%s" % (self._disc_server_ip, self._disc_server_port, uri)
        self._add_request_detail('PUT', url, headers=self._http_headers, body=body)
        response = self._disc_server_session.put(url, data=body,
                                                headers=self._http_headers)
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


    def setUp(self, extra_config_knobs = None):
        super(TestCase, self).setUp()
        global cov_handle
        if not cov_handle:
            cov_handle = coverage.coverage(source=['./'], omit=['.venv/*'])

        if extra_config_knobs:
            self._config_knobs.extend(extra_config_knobs)

        gevent.wsgi.WSGIServer.handler_class = FakeWSGIHandler
        setup_common_flexmock()

        """
        extra_mocks = [(pycassa.system_manager.SystemManager,
                            alter_column_family, stub)]
        """

        self._disc_server_ip = socket.gethostbyname(socket.gethostname())
        self._disc_server_port = get_free_port()
        http_server_port = get_free_port()
        self._disc_server_greenlet = gevent.spawn(launch_disc_server,
                                     self._disc_server_ip, self._disc_server_port,
                                     http_server_port, self._config_knobs)
        block_till_port_listened(self._disc_server_ip, self._disc_server_port)

        self._disc_server_session = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._disc_server_session.mount("http://", adapter)
        self._disc_server_session.mount("https://", adapter)
        self._disc_server = disc_server.server
        self._disc_server._sandesh.set_logging_params(level="SYS_DEBUG")
        self.addCleanup(self.cleanUp)
    # end setUp

    def cleanUp(self):
        self._disc_server_greenlet.kill()
        CassandraCFs.reset()
    # end cleanUp

# end TestCase
