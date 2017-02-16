import json                                                                                                    
import mock
import unittest
import os
import requests
import sys
import errno
from mock import patch
from mock import Mock

from cni.common import cni

class CniTest(unittest.TestCase):
    def setUp(self):
        os.environ['CNI_COMMAND'] = "add"
        os.environ['CNI_CONTAINERID'] = "123"
        os.environ['CNI_IFNAME'] = "eth0"
        os.environ['CNI_NETNS'] = "/var/run/netns/container"
        pass

    def tearDown(self):
        pass

    def test_init(self):
        c = cni.Cni('{"key": "value"}')
        self.assertEquals(c.command, "add")
        self.assertEquals(c.container_id, "123")
        self.assertEquals(c.container_ifname, "eth0")
        self.assertEquals(c.container_netns, "/var/run/netns/container")
        self.stdin_string = '{"key": "value"}'
        self.stdin_json = { 'key': 'value' }

        def test_missing_env(env):
            os.environ.pop(env)
            with self.assertRaises(cni.Error) as err:
                c = cni.Cni("{}")
            self.assertEquals(err.exception.code, cni.CNI_ERR_ENV)
            self.assertEquals(err.exception.msg,
                              'Missing environment variable ' + env)
            self.setUp()

        test_missing_env('CNI_COMMAND')
        test_missing_env('CNI_CONTAINERID')
        test_missing_env('CNI_IFNAME')
        test_missing_env('CNI_NETNS')

    def test_update(self):
        c = cni.Cni("{}")
        self.assertEquals(c.container_uuid, None)
        self.assertEquals(c.container_name, None)
        self.assertEquals(c.container_vn, None)
        c.update("a", "b", "c")
        self.assertEquals(c.container_uuid, "a")
        self.assertEquals(c.container_name, "b")
        self.assertEquals(c.container_vn, "c")
        c.update("a", "b")
        self.assertEquals(c.container_uuid, "a")
        self.assertEquals(c.container_name, "b")
        self.assertEquals(c.container_vn, "c")
        c.update(None, "d", "e")
        self.assertEquals(c.container_uuid, "a")
        self.assertEquals(c.container_name, "d")
        self.assertEquals(c.container_vn, "e")
        c.update("f", None, "g")
        self.assertEquals(c.container_uuid, "f")
        self.assertEquals(c.container_name, "f")
        self.assertEquals(c.container_vn, "g")

    @patch('sys.exit')
    def test_error_exit(self, mock_exit):
        c = cni.Cni('{}')
        c.error_exit(3, "Test Message")
        mock_exit.assert_called_once_with(3)

    def test_build_response(self):
        c = cni.Cni('{}')
        resp = c.build_response("10.10.0.3", "24", "10.10.0.1", "10.10.0.2")
        self.assertEquals(resp, {
            'cniVersion': cni.Cni.CNI_VERSION,
            'ip4': {
                'gateway': '10.10.0.1',
                'ip': '10.10.0.3/24',
                'routes': [{
                    'dst': '0.0.0.0/0',
                    'gw': '10.10.0.1'
                }]
            },
            'dns': {
                'nameservers': [ '10.10.0.2' ]
            }
        })

    def test_delete_response(self):
        c = cni.Cni('{}')
        resp = c.delete_response()
        self.assertEquals(resp, {
            'cniVersion': cni.Cni.CNI_VERSION,
            'code': 0,
            'msg': 'Delete passed'
        })

    def test_version_response(self):
        c = cni.Cni('{}')
        resp = c.version_response()
        self.assertEquals(resp, {
            'cniVersion': cni.Cni.CNI_VERSION,
            'supportedVersions': [cni.Cni.CNI_VERSION],
        })

