from __future__ import print_function
from __future__ import absolute_import
from __future__ import unicode_literals
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import range
from past.builtins import basestring
from builtins import object
from future.utils import native_str
import sys
from six.moves.configparser import RawConfigParser
from six.moves.configparser import DuplicateSectionError
import gevent.monkey
gevent.monkey.patch_all()
import os
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__),
                "./mocked_libs")))
from pycassa import CassandraCFs

import logging
import tempfile
import mock
from pprint import pformat
import fixtures
import testtools
from testtools import content
from flexmock import flexmock
from webtest import TestApp
import contextlib
from netaddr import IPNetwork, IPAddress

from vnc_api.vnc_api import *
import kombu
import cfgm_common.zkclient
from cfgm_common.uve.vnc_api.ttypes import VncApiConfigLog
from cfgm_common import db_json_exim
from cfgm_common import vnc_cgitb
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.utils import cgitb_hook

from .test_utils import *
import bottle
bottle.catchall=False

import inspect
import novaclient
import novaclient.client

import gevent.pywsgi
import uuid
from pysandesh import sandesh_logger

def lineno():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno
# end lineno


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
    if hasattr(device_manager, 'DeviceManager'):
        import dm_server
    else:
        from device_manager import dm_server
        from device_manager import device_manager
except ImportError:
    device_manager = 'device_manager could not be imported'

try:
    from kube_manager import kube_manager
    if not hasattr(kube_manager, 'main'):
        from kube_manager import kube_manager
except ImportError:
    kube_manager = 'kube_manager could not be imported'

try:
    from mesos_manager import mesos_manager
    if not hasattr(mesos_manager, 'main'):
        from mesos_manager import mesos_manager
except ImportError:
    mesos_manager = 'mesos_manager could not be imported'

def generate_conf_file_contents(conf_sections):
    cfg_parser = RawConfigParser()
    for (section, var, val) in conf_sections:
        try:
            cfg_parser.add_section(section)
        except DuplicateSectionError:
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
    cfg_parser = RawConfigParser()

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

def launch_kube_manager(test_id, conf_sections, kube_api_skip, event_queue,
                        vnc_kubernetes_config_dict=None):
    args_str = ""
    vnc_cgitb.enable(format='text')

    wait_for_kube_manager_down()
    with tempfile.NamedTemporaryFile(mode='w+') as conf, tempfile.NamedTemporaryFile(mode='w+') as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str= ["-c", conf.name]
        kube_manager.main(args_str, kube_api_skip=kube_api_skip,
                          event_queue=event_queue,
                          vnc_kubernetes_config_dict=vnc_kubernetes_config_dict)
#end launch_kube_manager

def launch_mesos_manager(test_id, conf_sections, mesos_api_skip, event_queue):
    args_str = ""
    vnc_cgitb.enable(format='text')

    wait_for_mesos_manager_down()
    with tempfile.NamedTemporaryFile(mode='w+') as conf, tempfile.NamedTemporaryFile(mode='w+') as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str= ["-c", conf.name]
        mesos_manager.main(args_str, mesos_api_skip=mesos_api_skip, event_queue=event_queue)
#end launch_mesos_manager

def retry_exc_handler(tries_remaining, exception, delay):
    print("Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay), file=sys.stderr)
# end retry_exc_handler

def retries(max_tries, delay=1, backoff=2, exceptions=(Exception,), hook=None):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = list(range(max_tries))
            tries.reverse()
            for tries_remaining in tries:
                try:
                   return func(*args, **kwargs)
                except exceptions as e:
                    if tries_remaining > 0:
                        if hook is not None:
                            hook(tries_remaining, e, mydelay)
                        gevent.sleep(mydelay)
                        mydelay = mydelay * backoff
                    else:
                        raise
                else:
                    break
        return f2
    return dec
# end retries

class VncTestApp(TestApp):
    def post_json(self, *args, **kwargs):
        resp = super(VncTestApp, self).post_json(*args, **kwargs)
        resp.charset = 'UTF-8'
        return resp
#end class VncTestApp

def create_api_server_instance(test_id, config_knobs, db='cassandra'):
    ret_server_info = {}
    allocated_sockets = []
    ret_server_info['ip'] = socket.gethostbyname(socket.gethostname())
    ret_server_info['service_port'] = get_free_port(allocated_sockets)
    ret_server_info['introspect_port'] = get_free_port(allocated_sockets)
    ret_server_info['admin_port'] = get_free_port(allocated_sockets)
    ret_server_info['allocated_sockets'] = allocated_sockets
    if db == "cassandra":
        ret_server_info['greenlet'] = gevent.spawn(launch_api_server,
            test_id, ret_server_info['ip'], ret_server_info['service_port'],
            ret_server_info['introspect_port'], ret_server_info['admin_port'],
            config_knobs)
    else:
        msg = ("Contrail API server does not support database backend "
               "'%s'" % db)
        raise NotImplementedError(msg)
    if ret_server_info['greenlet'].exception:
        # If the server did not start well, we should quit early.
        raise ret_server_info['greenlet'].exception
    block_till_port_listened(ret_server_info['ip'],
        ret_server_info['service_port'])
    extra_env = {'HTTP_HOST': ret_server_info['ip'],
                 'SERVER_PORT': native_str(ret_server_info['service_port'])}
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
    if hasattr(server_info['api_server']._db_conn, '_msgbus'):
        server_info['api_server']._db_conn._msgbus.shutdown()
        vhost_url = server_info['api_server']._db_conn._msgbus._urls
        FakeKombu.reset(vhost_url)
    FakeNovaClient.reset()
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
                      admin_port, conf_sections):
    try:
        import api_server
    except ImportError:
        from vnc_cfg_api_server import api_server

    kombu_mock = mock.Mock()
    kombu_patch = mock.patch(
        'vnc_cfg_api_server.api_server.KombuAmqpClient')
    kombu_init_mock = kombu_patch.start()
    kombu_init_mock.side_effect = kombu_mock

    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--admin_port %s " % (admin_port)

    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file api_server_%s.log " %(test_id)
    args_str = args_str + "--cluster_id %s " %(test_id)

    vnc_cgitb.enable(format='text')

    with tempfile.NamedTemporaryFile(mode='w+') as conf, \
         tempfile.NamedTemporaryFile(mode='w+') as logconf:
        cfg_parser = generate_conf_file_contents(conf_sections)
        cfg_parser.write(conf)
        conf.flush()

        cfg_parser = generate_logconf_file_contents()
        cfg_parser.write(logconf)
        logconf.flush()

        args_str = args_str + "--conf_file %s " %(conf.name)
        args_str = args_str + "--logging_conf %s " %(logconf.name)
        server = api_server.VncApiServer(args_str)
        gevent.getcurrent().api_server = server
        api_server.main(args_str, server)
# end launch_api_server


def launch_svc_monitor(cluster_id, test_id, api_server_ip, api_server_port, **extra_args):
    allocated_sockets = []
    args_str = ""
    args_str += "--cluster_id %s " % (cluster_id)
    args_str += "--api_server_ip %s " % (api_server_ip)
    args_str += "--api_server_port %s " % (api_server_port)
    args_str += "--http_server_port %s " % (get_free_port(allocated_sockets))
    args_str += "--cassandra_server_list 0.0.0.0:9160 "
    args_str += "--log_local "
    args_str += "--log_file svc_monitor_%s.log " %(test_id)
    args_str += "--trace_file svc_monitor_%s.err " %(test_id)
    args_str += "--check_service_interval 2 "

    for name, value in list(extra_args.items()):
        args_str += "--{name} {value} ".format(name=name, value=value)

    svc_monitor.main(args_str)
# end launch_svc_monitor

def kill_svc_monitor(glet):
    glet.kill()
    svc_monitor.SvcMonitor.reset()

def kill_schema_transformer(glet):
    glet.kill()
    to_bgp.SchemaTransformer.destroy_instance()

def kill_device_manager(glet):
    glet.kill()
    dm_server.sigterm_handler()

def kill_kube_manager(glet):
    glet.kill()
    kube_manager.KubeNetworkManager.destroy_instance()

def kill_mesos_manager(glet):
    glet.kill()
    mesos_manager.MesosNetworkManager.destroy_instance()

def reinit_schema_transformer():
    for obj_cls in list(to_bgp.ResourceBaseST.get_obj_type_map().values()):
        obj_cls.reset()
    to_bgp.transformer.reinit()

def launch_schema_transformer(cluster_id, test_id, api_server_ip,
        api_server_port, extra_args=None):
    allocated_sockets = []
    wait_for_schema_transformer_down()
    args_str = ""
    args_str = args_str + "--cluster_id %s " % (cluster_id)
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port(allocated_sockets))
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file schema_transformer_%s.log " %(test_id)
    args_str = args_str + "--trace_file schema_transformer_%s.err " %(test_id)
    if extra_args:
        args_str = args_str + (extra_args)
    to_bgp.main(args_str)
# end launch_schema_transformer

def launch_device_manager(test_id, api_server_ip, api_server_port,
                          conf_sections=None):

    kombu_mock = mock.Mock()
    kombu_patch = mock.patch(
        'device_manager.dm_server.KombuAmqpClient')
    kombu_init_mock = kombu_patch.start()
    kombu_init_mock.side_effect = kombu_mock

    wait_for_device_manager_down()
    allocated_sockets = []
    args_str = ""
    args_str = args_str + "--cluster_id %s " % (test_id)
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port(allocated_sockets))
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file device_manager_%s.log " %(test_id)

    if conf_sections is not None:
        with tempfile.NamedTemporaryFile(mode='w+') as conf:
            cfg_parser = generate_conf_file_contents(conf_sections)
            cfg_parser.write(conf)
            conf.flush()
            args_str = args_str + "--conf_file %s " % conf.name
            dm_server.main(args_str)
    else:
        dm_server.main(args_str)

# end launch_device_manager

@retries(5, hook=retry_exc_handler)
def wait_for_schema_transformer_up():
    if not to_bgp.SchemaTransformer.get_instance():
        raise Exception("ST instance is not up")

@retries(5, hook=retry_exc_handler)
def wait_for_schema_transformer_down():
    if to_bgp.SchemaTransformer.get_instance():
        raise Exception("ST instance is up, no new instances allowed")

@retries(5, hook=retry_exc_handler)
def wait_for_device_manager_up():
    if not device_manager.DeviceManager.get_instance():
        raise Exception("DM instance is not up")

@retries(5, hook=retry_exc_handler)
def wait_for_device_manager_down():
    if device_manager.DeviceManager.get_instance():
        raise Exception("DM instance is up, no new instances allowed")

@retries(5, hook=retry_exc_handler)
def wait_for_kube_manager_up():
    if not kube_manager.KubeNetworkManager.get_instance():
        raise Exception("KM instance is not up")

@retries(5, hook=retry_exc_handler)
def wait_for_kube_manager_down():
    if kube_manager.KubeNetworkManager.get_instance():
        raise Exception("KM instance is up, no new instances allowed")

@retries(5, hook=retry_exc_handler)
def wait_for_mesos_manager_up():
    if not mesos_manager.MesosNetworkManager.get_instance():
        raise Exception("MM instance is not up")

@retries(5, hook=retry_exc_handler)
def wait_for_mesos_manager_down():
    if mesos_manager.MesosNetworkManager.get_instance():
        raise Exception("MM instance is up, no new instances allowed")

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
        for (cls, method_name), method in list(orig_values.items()):
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
        for mod_name, mod in list(orig_modules.items()):
            sys.modules[mod_name] = mod
        for mod_name in mocked_modules:
            del sys.modules[mod_name]

#end patch_import


def fake_wrapper(self, func, *args, **kwargs):
    def wrapper(*wargs, **wkwargs):
        return func(*wargs, **wkwargs)
    return wrapper


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

        (kazoo.client.KazooClient, '__new__',FakeKazooClient),
        (kazoo.recipe.counter.Counter, '__init__',fake_zk_counter_init),
        (kazoo.recipe.counter.Counter, '_change',fake_zk_counter_change),
        (kazoo.recipe.counter.Counter, 'value',fake_zk_counter_value),
        (kazoo.recipe.counter.Counter, '_ensure_node',
                                       fake_zk_counter_ensure_node),
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
        from six import StringIO

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

    def _create_test_objects(self, count=1, proj_obj=None):
        ret_objs = []
        for i in range(count):
            obj_name = self.id() + '-vn-' + str(i)
            obj = VirtualNetwork(obj_name, parent_obj=proj_obj)
            self._add_detail('creating-object ' + obj_name)
            self._vnc_lib.virtual_network_create(obj)
            ret_objs.append(obj)

        return ret_objs

    def _create_test_object(self):
        return self._create_test_objects()[0]

    def _delete_test_object(self, obj):
        self._vnc_lib.virtual_network_delete(id=obj.uuid)

    def get_cf(self, keyspace_name, cf_name):
        ks_name = '%s_%s' %(self._cluster_id, keyspace_name)
        return CassandraCFs.get_cf(ks_name, cf_name)
    # end get_cf

    def vnc_db_has_ident(self, obj=None, id=None, type_fq_name=None):
        if obj:
            _type = obj.get_type()
            _fq_name = obj.get_fq_name()
        if id:
            _type = self._vnc_lib.id_to_fq_name_type(id)
            _fq_name = self._vnc_lib.id_to_fq_name(id)
        if type_fq_name:
            _type = type_fq_name[0]
            _fq_name = type_fq_name[1]

        try:
            vnc_obj = self._vnc_lib._object_read(_type, _fq_name)
        except NoIdError:
            return None
        return vnc_obj

    def vnc_db_ident_has_prop(self, obj, prop_name, prop_value):
        vnc_obj = self.vnc_db_has_ident(obj=obj)

        if vnc_obj is None:
            return False

        return getattr(vnc_obj, prop_name) == prop_value

    def vnc_db_ident_has_ref(self, obj, ref_name, ref_fq_name):
        vnc_obj = self.vnc_db_has_ident(obj=obj)
        if vnc_obj is None:
            return False

        refs = getattr(vnc_obj, ref_name, [])
        for ref in refs:
            if ref['to'] == ref_fq_name:
                return True

        return False

    def vnc_db_doesnt_have_ident(self, obj=None, id=None, type_fq_name=None):
        return not self.vnc_db_has_ident(obj=obj, id=id,
                                         type_fq_name=type_fq_name)

    def vnc_db_ident_doesnt_have_ref(self, obj, ref_name, ref_fq_name=None):
        return not self.vnc_db_ident_has_ref(obj, ref_name, ref_fq_name)

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
    def setUpClass(cls, extra_mocks=None, extra_config_knobs=None,
                   db='cassandra', in_place_upgrade_path=None):
        cls._cluster_id = cls.__name__
        cls._in_place_upgrade_path = in_place_upgrade_path
        super(TestCase, cls).setUpClass()

        cfgm_common.zkclient.LOG_DIR = './'
        gevent.pywsgi.WSGIServer.handler_class = FakeWSGIHandler

        cls.orig_mocked_values = setup_mocks(cls.mocks + (extra_mocks or []))
        # For performance reasons, don't log cassandra requests
        VncCassandraClient._handle_exceptions = fake_wrapper

        # Load DB if JSON DBs dump file is provided
        cls._load_db_contents()

        cls._server_info = create_api_server_instance(
            cls._cluster_id, cls._config_knobs + (extra_config_knobs or []),
            db=db)
        try:
            cls._api_server_ip = cls._server_info['ip']
            cls._api_server_port = cls._server_info['service_port']
            cls._api_admin_port = cls._server_info['admin_port']
            cls._api_svr_greenlet = cls._server_info['greenlet']
            cls._api_svr_app = cls._server_info['app']
            cls._vnc_lib = cls._server_info['api_conn']
            cls._api_server_session = cls._server_info['api_session']
            cls._api_server = cls._server_info['api_server']
        except Exception as e:
            cls.tearDownClass()
            raise
    # end setUpClass

    @classmethod
    def tearDownClass(cls):
        # Dump DBs into a JSON file if a path was provided
        cls._dump_db_contents()

        destroy_api_server_instance(cls._server_info)
        teardown_mocks(cls.orig_mocked_values)

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
        if hasattr(self._api_server._db_conn, '_msgbus'):
            while self._api_server._db_conn._msgbus.num_pending_messages() > 0:
                gevent.sleep(0.001)
            vhost_url = self._api_server._db_conn._msgbus._urls
            while not FakeKombu.is_empty(vhost_url, 'vnc_config'):
                gevent.sleep(0.001)
    # wait_till_api_server_idle

    def create_virtual_network(self, vn_name, vn_subnet='10.0.0.0/24'):
        vn_obj = VirtualNetwork(name=vn_name)
        ipam_fq_name = [
            'default-domain', 'default-project', 'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnets = [vn_subnet] if isinstance(vn_subnet, basestring) else vn_subnet
        subnet_infos = []
        for subnet in subnets:
            cidr = IPNetwork(subnet)
            subnet_infos.append(
                IpamSubnetType(
                    subnet=SubnetType(
                        str(cidr.network),
                        int(cidr.prefixlen),
                    ),
                    default_gateway=str(IPAddress(cidr.last - 1)),
                    subnet_uuid=str(uuid.uuid4()),
                )
            )
        subnet_data = VnSubnetsType(subnet_infos)
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj.clear_pending_updates()
        return vn_obj
    # end create_virtual_network

    def _create_service(self, vn_list, si_name, auto_policy,
                        create_right_port=True, **kwargs):
        sa_set = None
        if kwargs.get('service_virtualization_type') == 'physical-device':
            pr = PhysicalRouter(si_name, physical_router_role='pnf')
            self._vnc_lib.physical_router_create(pr)
            sa_set = ServiceApplianceSet('sa_set-'+si_name)
            self._vnc_lib.service_appliance_set_create(sa_set)
            sa = ServiceAppliance('sa-'+si_name, parent_obj=sa_set)
            left_value = "default-global-system-config:5c3-qfx5:et-0/0/50"
            right_value = "default-global-system-config:5c3-qfx5:et-0/0/51"
            sa.set_service_appliance_properties(KeyValuePairs([KeyValuePair(key='left-attachment-point',
                                                               value= left_value),
                                                               KeyValuePair(key='right-attachment-point',
                                                               value= right_value)]))
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
                if if_type == 'right' and not create_right_port:
                    continue
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
                              auto_policy=False, create_right_port = True, **kwargs):
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
                     auto_policy, create_right_port, **kwargs))
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

    def create_logical_router(self, name, nb_of_attached_networks=1, **kwargs):
        lr = LogicalRouter(name, **kwargs)
        vns = []
        vmis = []
        iips = []
        for idx in range(nb_of_attached_networks):
            # Virtual Network
            vn = self.create_virtual_network('%s-network%d' % (name, idx),
                                             '10.%d.0.0/24' % idx)
            vns.append(vn)

            # Virtual Machine Interface
            vmi_name = '%s-network%d-vmi' % (name, idx)
            vmi = VirtualMachineInterface(
                vmi_name, parent_type='project',
                fq_name=['default-domain', 'default-project', vmi_name])
            vmi.set_virtual_machine_interface_device_owner(
                'network:router_interface')
            vmi.add_virtual_network(vn)
            self._vnc_lib.virtual_machine_interface_create(vmi)
            lr.add_virtual_machine_interface(vmi)
            vmis.append(vmi)

            # Instance IP
            gw_ip = vn.get_network_ipam_refs()[0]['attr'].ipam_subnets[0].\
                default_gateway
            subnet_uuid = vn.get_network_ipam_refs()[0]['attr'].\
                ipam_subnets[0].subnet_uuid
            iip = InstanceIp(name='%s-network%d-iip' % (name, idx))
            iip.set_subnet_uuid(subnet_uuid)
            iip.set_virtual_machine_interface(vmi)
            iip.set_virtual_network(vn)
            iip.set_instance_ip_family('v4')
            iip.set_instance_ip_address(gw_ip)
            self._vnc_lib.instance_ip_create(iip)
            iips.append(iip)


        self._vnc_lib.logical_router_create(lr)
        return lr, vns, vmis, iips

    def _security_group_rule_build(self, rule_info, sg_fq_name_str):
        protocol = rule_info['protocol']
        port_min = rule_info['port_min'] or 0
        port_max = rule_info['port_max'] or 65535
        direction = rule_info['direction'] or 'ingress'
        ip_prefix = rule_info['ip_prefix']
        ether_type = rule_info['ether_type']

        if ip_prefix:
            cidr = ip_prefix.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            endpt = [AddressType(subnet=SubnetType(pfx, pfx_len))]
        else:
            endpt = [AddressType(security_group=sg_fq_name_str)]

        local = None
        remote = None
        if direction == 'ingress':
            dir = '>'
            local = endpt
            remote = [AddressType(security_group='local')]
        else:
            dir = '>'
            remote = endpt
            local = [AddressType(security_group='local')]

        if not protocol:
            protocol = 'any'

        if protocol.isdigit():
            protocol = int(protocol)
            if protocol < 0 or protocol > 255:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)
        else:
            if protocol not in ['any', 'tcp', 'udp', 'icmp', 'icmp6']:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)

        if not ip_prefix and not sg_fq_name_str:
            if not ether_type:
                ether_type = 'IPv4'

        sgr_uuid = str(uuid.uuid4())
        rule = PolicyRuleType(rule_uuid=sgr_uuid, direction=dir,
                                  protocol=protocol,
                                  src_addresses=local,
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=remote,
                                  dst_ports=[PortType(port_min, port_max)],
                                  ethertype=ether_type)
        return rule
    #end _security_group_rule_build

    def _security_group_rule_append(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            rules = PolicyEntriesType([sg_rule])
        else:
            for sgr in rules.get_policy_rule() or []:
                sgr_copy = copy.copy(sgr)
                sgr_copy.rule_uuid = sg_rule.rule_uuid
                if sg_rule == sgr_copy:
                    raise Exception('SecurityGroupRuleExists %s' % sgr.rule_uuid)
            rules.add_policy_rule(sg_rule)

        sg_obj.set_security_group_entries(rules)
    #end _security_group_rule_append

    def _security_group_rule_remove(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            raise Exception('SecurityGroupRuleNotExists %s' % sgr.rule_uuid)
        else:
            for sgr in rules.get_policy_rule() or []:
                if sgr.rule_uuid == sg_rule.rule_uuid:
                    rules.delete_policy_rule(sgr)
                    sg_obj.set_security_group_entries(rules)
                    return
            raise Exception('SecurityGroupRuleNotExists %s' % sg_rule.rule_uuid)
    #end _security_group_rule_append

    @classmethod
    def _dump_db_contents(cls):
        if not cls._in_place_upgrade_path:
            return

        if not os.path.exists(os.path.dirname(cls._in_place_upgrade_path)):
            os.makedirs(os.path.dirname(cls._in_place_upgrade_path))
        db_json_exim.DatabaseExim(
            '--export-to %s --cluster_id %s' % (cls._in_place_upgrade_path,
                                                cls._cluster_id)
        ).db_export()

    @classmethod
    def _load_db_contents(cls):
        if (not cls._in_place_upgrade_path or
                not os.path.exists(cls._in_place_upgrade_path)):
            return

        db_json_exim.DatabaseExim(
            '--import-from %s --cluster_id %s' % (cls._in_place_upgrade_path,
                                                  cls._cluster_id)
        ).db_import()


class ErrorInterceptingLogger(sandesh_logger.SandeshLogger):

    _exceptions = []
    _other_errors = []

    @classmethod
    def register_exception(cls, msg, *args, **kwargs):
        if 'traceback' in msg.lower():
            cls._exceptions.append((msg, args, kwargs))
            return True
        return False

    @classmethod
    def register_error(cls, msg, *args, **kwargs):
        if not cls.register_exception(msg, *args, **kwargs):
            cls._other_errors.append((msg, args, kwargs))

    @classmethod
    def get_exceptions(cls):
        return list(cls._exceptions)

    @classmethod
    def get_other_errors(cls):
        return list(cls._other_errors)

    @classmethod
    def reset(cls):
        cls._exceptions, cls._other_errors = [], []

    @classmethod
    def get_qualified_name(cls):
        return '{module_name}.{class_name}'.format(
            module_name=cls.__module__, class_name=cls.__name__)

    class LoggerWrapper(object):

        def __init__(self, logger):
            self._logger = logger

        def __getattr__(self, item):
            return getattr(self._logger, item)

        def error(self, msg, *args, **kwargs):
            ErrorInterceptingLogger.register_error(msg, *args, **kwargs)
            return self._logger.error(msg, *args, **kwargs)

        def critical(self, msg, *args, **kwargs):
            ErrorInterceptingLogger.register_error(msg, *args, **kwargs)
            return self._logger.critical(msg, *args, **kwargs)

        def exception(self, msg, *args, **kwargs):
            ErrorInterceptingLogger.register_error(msg, *args, **kwargs)
            return self._logger.exception(msg, *args, **kwargs)

        def log(self, lvl, msg, *args, **kwargs):
            ErrorInterceptingLogger.register_exception(
                msg, *args, **kwargs)
            return self._logger.log(lvl, msg, *args, **kwargs)

    def __init__(self, *args, **kwargs):
        super(ErrorInterceptingLogger, self).__init__(*args, **kwargs)
        self._logger = ErrorInterceptingLogger.LoggerWrapper(
            self._logger)
