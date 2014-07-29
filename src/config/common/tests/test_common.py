#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys

import logging
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
from cfgm_common.zkclient import ZookeeperClient

from test_utils import *
sys.path.insert(0, '../../../../build/debug/api-lib/vnc_api')
sys.path.insert(0, '../../../../distro/openstack/')
import bottle
bottle.catchall=False

# import from package for non-api server test or directly from file
import vnc_cfg_api_server
if not hasattr(vnc_cfg_api_server, 'main'):
    from vnc_cfg_api_server import vnc_cfg_api_server

def launch_api_server(listen_ip, listen_port, http_server_port):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160"

    import cgitb
    cgitb.enable(format='text')

    vnc_cfg_api_server.main(args_str)
#end launch_api_server


def setup_flexmock():
    flexmock(ifmap_client.client, __init__=FakeIfmapClient.initialize,
             call=FakeIfmapClient.call,
             call_async_result=FakeIfmapClient.call_async_result)
    flexmock(ifmap_response.Response, __init__=stub, element=stub)
    flexmock(ifmap_response.newSessionResult, get_session_id=stub)
    flexmock(ifmap_response.newSessionResult, get_publisher_id=stub)

    flexmock(pycassa.system_manager.Connection, __init__=stub)
    flexmock(pycassa.system_manager.SystemManager, create_keyspace=stub,
             create_column_family=stub)
    flexmock(pycassa.ConnectionPool, __init__=stub)
    flexmock(pycassa.ColumnFamily, __new__=FakeCF)
    flexmock(pycassa.util, convert_uuid_to_time=Fake_uuid_to_time)

    flexmock(disc_client.DiscoveryClient, __init__=stub)
    flexmock(disc_client.DiscoveryClient, publish_obj=stub)
    flexmock(ZookeeperClient, __new__=ZookeeperClientMock)

    flexmock(kombu.Connection, __new__=FakeKombu.Connection)
    flexmock(kombu.Exchange, __new__=FakeKombu.Exchange)
    flexmock(kombu.Queue, __new__=FakeKombu.Queue)
#end setup_flexmock

cov_handle = None
class TestCase(testtools.TestCase, fixtures.TestWithFixtures):
    _HTTP_HEADERS =  {
        'Content-type': 'application/json; charset="UTF-8"',
    }
    def _add_request_detail(self, op, url, headers=None, query_params=None,
                         body=None):
        request_str = ' URL: ' + pformat(url) + \
                      ' OPER: ' + pformat(op) + \
                      ' Headers: ' + pformat(headers) + \
                      ' Query Params: ' + pformat(query_params) + \
                      ' Body: ' + pformat(body)
        self.addDetail('Requesting: ', content.text_content(request_str))

    def _http_get(self, uri, query_params=None):
        url = "http://%s:%s%s" % (self._web_host, self._web_port, uri)
        self._add_request_detail('GET', url, headers=self._HTTP_HEADERS,
                                 query_params=query_params)
        response = self._api_server_session.get(url, headers=self._HTTP_HEADERS,
                                                params=query_params)
        response = self._api_server_session.get(url, headers = headers,
                                                params = query_params)
        self.addDetail('Received Response: ',
                       content.text_content(pformat(response.status_code) +
                                            pformat(response.text)))
        return (response.status_code, response.text)
    #end _http_get

    def _http_post(self, uri, body):
        url = "http://%s:%s%s" % (self._web_host, self._web_port, uri)
        self._add_request_detail('POST', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.post(url, data=body,
                                                 headers=self._HTTP_HEADERS)
        self.addDetail('Received Response: ',
                       content.text_content(pformat(response.status_code) +
                                            pformat(response.text)))
        return (response.status_code, response.text)
    #end _http_post

    def _http_delete(self, uri, body):
        url = "http://%s:%s%s" % (self._web_host, self._web_port, uri)
        self._add_request_detail('DELETE', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.delete(url, data=body,
                                                   headers=self._HTTP_HEADERS)
        self.addDetail('Received Response: ',
                       content.text_content(pformat(response.status_code) +
                                            pformat(response.text)))
        return (response.status_code, response.text)
    #end _http_delete

    def _http_put(self, uri, body, headers):
        url = "http://%s:%s%s" % (self._web_host, self._web_port, uri)
        self._add_request_detail('PUT', url, headers=self._HTTP_HEADERS, body=body)
        response = self._api_server_session.put(url, data=body,
                                                headers=self._HTTP_HEADERS)
        self.addDetail('Received Response: ',
                       content.text_content(pformat(response.status_code) +
                                            pformat(response.text)))
        return (response.status_code, response.text)
    #end _http_put

    def _create_test_objects(self, count=1):
        ret_objs = []
        for i in range(count):
            obj_name = self.id() + '-vn-' + str(i)
            obj = VirtualNetwork(obj_name)
            self.addDetail('creating-object', content.text_content(obj_name))
            self._vnc_lib.virtual_network_create(obj)
            ret_objs.append(obj)

        return ret_objs

    def _create_test_object(self):
        return self._create_test_objects()[0]

    def setUp(self):
        super(TestCase, self).setUp()
        global cov_handle
        if not cov_handle:
            cov_handle = coverage.coverage(source=['./'], omit=['.venv/*'])
        cov_handle.start()
        setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._web_host = api_server_ip
        self._web_port = api_server_port
        self._api_svr_greenlet = gevent.spawn(launch_api_server,
                                     api_server_ip, api_server_port,
                                     http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        extra_env = {'HTTP_HOST':'%s%s' %(api_server_ip,
                                          api_server_port)}
        self._api_svr_app = TestApp(bottle.app(), extra_environ=extra_env)
        #FakeRequestsSession._api_svr_app = self._api_svr_app
        
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)

        self._api_server_session = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._api_server_session.mount("http://", adapter)
        self._api_server_session.mount("https://", adapter)
        self._api_server = vnc_cfg_api_server.server
        self._api_server._sandesh.set_logging_params(level="SYS_WARN")
    # end setUp

    def tearDown(self):
        self._api_svr_greenlet.kill()
        cov_handle.stop()
        cov_handle.report(file=open('covreport.txt', 'w'))
        super(TestCase, self).tearDown()
    # end tearDown
# end TestCase
