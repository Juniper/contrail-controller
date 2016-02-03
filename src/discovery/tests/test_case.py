import sys
import uuid
import inspect
import requests
sys.path.append("../config/common/tests")
from test_utils import *
import test_common
from testtools import content, content_type

from vnc_api.vnc_api import *
import cfgm_common.vnc_cpu_info
import cfgm_common.ifmap.client as ifmap_client
import cfgm_common.ifmap.response as ifmap_response
import kombu
import cfgm_common.zkclient
from cfgm_common.uve.vnc_api.ttypes import (VncApiConfigLog, VncApiError)
from cfgm_common import imid
import novaclient
import novaclient.client

class DsTestCase(test_common.TestCase):
    def __init__(self, *args, **kwargs):
        self._content_type = 'application/json; charset="UTF-8"'
        self._http_headers =  {
            'Content-type': 'application/json; charset="UTF-8"',
        }
        self._disc_server_config_knobs = [
            ('DEFAULTS', '', ''),
        ]
        super(DsTestCase, self).__init__(*args, **kwargs)

        self.mocks = [
            (cfgm_common.vnc_cpu_info.CpuInfo, '__init__',stub),
            (novaclient.client, 'Client',FakeNovaClient.initialize),

            (ifmap_client.client, '__init__', FakeIfmapClient.initialize),
            (ifmap_client.client, 'call_async_result', FakeIfmapClient.call_async_result),
            (ifmap_client.client, 'call', FakeIfmapClient.call),

            (pycassa.system_manager.Connection, '__init__',stub),
            (pycassa.system_manager.SystemManager, '__new__',FakeSystemManager),
            (pycassa.ConnectionPool, '__init__',stub),
            (pycassa.ColumnFamily, '__new__',FakeCF),
            (pycassa.util, 'convert_uuid_to_time',Fake_uuid_to_time),

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

    def setUp(self, extra_disc_server_config_knobs = None):
        extra_api_server_config_knobs = [
            ('DEFAULTS', 'disc_server_dsa_api', '/api/dsa'),
            ('DEFAULTS', 'log_level', 'SYS_DEBUG'),
        ]
        super(DsTestCase, self).setUp(extra_config_knobs = extra_api_server_config_knobs)

        self._disc_server_ip = socket.gethostbyname(socket.gethostname())
        self._disc_server_port = get_free_port()
        http_server_port = get_free_port()
        if extra_disc_server_config_knobs:
            self._disc_server_config_knobs.extend(extra_disc_server_config_knobs)

        test_common.setup_mocks(self.mocks)

        self._disc_server_greenlet = gevent.spawn(test_common.launch_disc_server,
            self.id(), self._disc_server_ip, self._disc_server_port,
            http_server_port, self._disc_server_config_knobs)

        block_till_port_listened(self._disc_server_ip, self._disc_server_port)
        self._disc_server_session = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._disc_server_session.mount("http://", adapter)
        self._disc_server_session.mount("https://", adapter)


    def tearDown(self):
        test_common.kill_disc_server(self._disc_server_greenlet)
        super(DsTestCase, self).tearDown()

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

