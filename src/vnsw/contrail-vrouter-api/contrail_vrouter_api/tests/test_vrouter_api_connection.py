# Copyright (c) 2014 Juniper Networks, Inc
import subprocess
import time
import unittest
import uuid

from contrail_vrouter_api.vrouter_api import ContrailVRouterApi

def PickUnusedPort():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('localhost', 0))
    addr, port = s.getsockname()
    s.close()
    return port

class VRouterApiTestConnection(unittest.TestCase):

    def setUp(self):
        self._port = PickUnusedPort()
        self._server = None

    def tearDown(self):
        if self._server is not None:
            self._server.terminate()

    def _start_server(self):
        cmd = ['python', 'contrail_vrouter_api/tests/instance_server.py',
               '-p', str(self._port)]
        self._server = subprocess.Popen(cmd)

    def _try_connect(self, client):
        attempts = 3
        while attempts > 0:
            if client.connect():
                return
            attempts -= 1
            time.sleep(1)
        self.fail('Unable to connect to server')

    def test_connection_failure(self):
        api = ContrailVRouterApi(server_port=self._port)
        self.assertIsNone(api._client)

    def test_unicode(self):
        self._start_server()
        api = ContrailVRouterApi(server_port=self._port)
        self._try_connect(api)
        response = api.add_port(str(uuid.uuid1()), str(uuid.uuid1()), 'tapX',
                                'aa:bb:cc:ee:ff:00',
                                display_name=u'p\u227do')
        self.assertTrue(response)

