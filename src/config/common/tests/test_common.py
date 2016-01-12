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

from vnc_api.vnc_api import *
import cfgm_common.vnc_cpu_info
import cfgm_common.ifmap.client as ifmap_client
import cfgm_common.ifmap.response as ifmap_response
import kombu
import discoveryclient.client as disc_client
import cfgm_common.zkclient
from cfgm_common.uve.vnc_api.ttypes import (VncApiConfigLog, VncApiError)
from cfgm_common import imid

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

def launch_api_server(test_id, listen_ip, listen_port, http_server_port,
                      admin_port, conf_sections):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--admin_port %s " % (admin_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file api_server_%s.log " %(test_id)

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

def launch_svc_monitor(test_id, api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port())
    args_str = args_str + "--ifmap_username api-server "
    args_str = args_str + "--ifmap_password api-server "
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file svc_monitor_%s.log " %(test_id)

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

def launch_schema_transformer(test_id, api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " % (api_server_ip)
    args_str = args_str + "--api_server_port %s " % (api_server_port)
    args_str = args_str + "--http_server_port %s " % (get_free_port())
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160 "
    args_str = args_str + "--log_local "
    args_str = args_str + "--log_file schema_transformer_%s.log " %(test_id)
    args_str = args_str + "--trace_file schema_transformer_%s.err " %(test_id)
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

cov_handle = None
class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    _HTTP_HEADERS =  {
        'Content-type': 'application/json; charset="UTF-8"',
    }

    mocks = [
        (cfgm_common.vnc_cpu_info.CpuInfo, '__init__',stub),
        (novaclient.client, 'Client',FakeNovaClient.initialize),

        (ifmap_client.client, '__init__', FakeIfmapClient.initialize),
        (ifmap_client.client, 'call', FakeIfmapClient.call),
        (ifmap_client.client, 'call_async_result', FakeIfmapClient.call_async_result),

        (pycassa.system_manager.Connection, '__init__',stub),
        (pycassa.system_manager.SystemManager, '__new__',FakeSystemManager),
        (pycassa.ConnectionPool, '__init__',stub),
        (pycassa.ColumnFamily, '__new__',FakeCF),
        (pycassa.util, 'convert_uuid_to_time',Fake_uuid_to_time),

        (disc_client.DiscoveryClient, '__init__',stub),
        (disc_client.DiscoveryClient, 'publish_obj',stub),
        (disc_client.DiscoveryClient, 'publish',stub),
        (disc_client.DiscoveryClient, 'subscribe',stub),
        (disc_client.DiscoveryClient, 'syslog',stub),
        (disc_client.DiscoveryClient, 'def_pub',stub),

        (kazoo.client.KazooClient, '__new__',FakeKazooClient),
        (kazoo.handlers.gevent.SequentialGeventHandler, '__init__',stub),

        (kombu.Connection, '__new__',FakeKombu.Connection),
        (kombu.Exchange, '__new__',FakeKombu.Exchange),
        (kombu.Queue, '__new__',FakeKombu.Queue),
        (kombu.Consumer, '__new__',FakeKombu.Consumer),
        (kombu.Producer, '__new__',FakeKombu.Producer),

        (VncApiConfigLog, '__new__',FakeApiConfigLog),
        #(VncApiStatsLog, '__new__',FakeVncApiStatsLog)
    ]

    def __init__(self, *args, **kwargs):
        self._logger = logging.getLogger(__name__)
        self._assert_till_max_tries = 30
        self._config_knobs = [
            ('DEFAULTS', '', ''),
            ]

        super(TestCase, self).__init__(*args, **kwargs)
        self.addOnException(self._add_detailed_traceback)

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


    def setUp(self, extra_mocks=None, extra_config_knobs=None):
        super(TestCase, self).setUp()
        global cov_handle
        if not cov_handle:
            cov_handle = coverage.coverage(source=['./'], omit=['.venv/*'])
        #cov_handle.start()

        cfgm_common.zkclient.LOG_DIR = './'
        gevent.wsgi.WSGIServer.handler_class = FakeWSGIHandler
        setup_mocks(self.mocks + (extra_mocks or []))
        if extra_config_knobs:
            self._config_knobs.extend(extra_config_knobs)

        self._api_server_ip = socket.gethostbyname(socket.gethostname())
        self._api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_admin_port = get_free_port()
        self._api_svr_greenlet = gevent.spawn(launch_api_server,
                                     self.id(),
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
        self._api_server._sandesh.set_logging_level(level="SYS_DEBUG")
        self.addCleanup(self.cleanUp)
    # end setUp

    def cleanUp(self):
        self._api_svr_greenlet.kill()
        self._api_server._db_conn._msgbus.shutdown()
        FakeKombu.reset()
        FakeIfmapClient.reset()
        CassandraCFs.reset()
        FakeExtensionManager.reset()
        #cov_handle.stop()
        #cov_handle.report(file=open('covreport.txt', 'w'))
    # end cleanUp

    def get_obj_imid(self, obj):
        return 'contrail:%s:%s' %(obj._type, obj.get_fq_name_str())
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

    def _create_service(self, vn1_name, vn2_name, si_name, auto_policy, **kwargs):
        sti = [ServiceTemplateInterfaceType(
            'left'), ServiceTemplateInterfaceType('right')]
        st_prop = ServiceTemplateType(
            flavor='medium',
            image_name='junk',
            ordered_interfaces=True,
            interface_type=sti, **kwargs)
        service_template = ServiceTemplate(
            name=si_name + 'template',
            service_template_properties=st_prop)
        self._vnc_lib.service_template_create(service_template)
        scale_out = ServiceScaleOutType()
        if kwargs.get('service_mode') == 'in-network':
            if_list = [ServiceInstanceInterfaceType(virtual_network=vn1_name),
                       ServiceInstanceInterfaceType(virtual_network=vn2_name)]
            si_props = ServiceInstanceType(
                auto_policy=auto_policy, interface_list=if_list,
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
        return service_instance.get_fq_name_str()

    def create_network_policy(self, vn1, vn2, service_list=None, mirror_service=None,
                              auto_policy=False, **kwargs):
        vn1_name = vn1 if isinstance(vn1, basestring) else vn1.get_fq_name_str()
        vn2_name = vn2 if isinstance(vn2, basestring) else vn2.get_fq_name_str()
        addr1 = AddressType(virtual_network=vn1_name)
        addr2 = AddressType(virtual_network=vn2_name)
        port = PortType(-1, 0)
        service_name_list = []
        si_list = service_list or []
        if service_list:
            for service in si_list:
                service_name_list.append(self._create_service(
                    vn1_name, vn2_name, service, auto_policy, **kwargs))
        if mirror_service:
            mirror_si = self._create_service(
		vn1_name, vn2_name, mirror_service, False,
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
