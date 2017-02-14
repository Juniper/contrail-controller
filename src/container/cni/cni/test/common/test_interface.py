import json
import mock
import unittest
import os
import requests
import sys
import errno
from pyroute2 import iproute, NetlinkError
from mock import patch
from mock import Mock

from cni.common import interface
from cni.test.common.netns_mock import *

class CniNamespaceTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.close')
    @patch('os.open', new=Mock(return_value=1))
    def test_close_files(self, mock_close):
        c = interface.CniNamespace("/var/run/netns/ns")
        c.ns_fd = None
        c.my_fd = None
        c.close_files()
        self.assertEqual(mock_close.call_count, 0)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        mock_close.reset_mock()
        c.ns_fd = 1
        c.close_files()
        mock_close.assert_called_once_with(1)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        mock_close.reset_mock()
        c.my_fd = 2
        c.close_files()
        mock_close.assert_called_once_with(2)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        mock_close.reset_mock()
        c.ns_fd = 1
        c.my_fd = 2
        c.close_files()
        self.assertEqual(mock_close.call_count, 2)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))

#    @patch('os.open')
#    def test_context_manager(self, mock_open):
#        interface.logger = Mock()
#        mock_open.side_effect = lambda path, mode: 1 if path == "/netns/ns" else 2
#        mock_libc = Mock()
#        mock_libc.setns = Mock(return_value=0)
#        c = interface.CniNamespace("/netns/ns")
#        c.libc = mock_libc
#        with c:
#            mock_libc.setns.assert_called_once_with(1, 0)
#            mock_libc.setns.reset_mock()
#        mock_libc.setns.assert_called_once_with(2, 0)

class ExtendedInterface(interface.Interface, object):
    def __init__(self, cni):
        interface.Interface.__init__(self, cni)
    def create_interface(self):
        pass
    def delete_interface(self):
        pass

class InterfaceTest(unittest.TestCase):
    def setUp(self):
        NetnsMock.reset()
        self.mock_cni = Mock()
        self.mock_cni.container_ifname = "eth0"

    def tearDown(self):
        pass

    @patch("cni.common.interface.CniNamespace", new=NetnsMock)
    @patch("cni.common.interface.IPRoute", new=IPRouteMock)
    def test_get_link(self):
        iface = interface.Interface(self.mock_cni)
        self.mock_cni.container_ifname = "eth1"
        self.assertEquals(iface.get_link()["name"], "eth1")
        self.mock_cni.container_ifname = "eth13"
        self.assertIsNone(iface.get_link())

    @patch("cni.common.interface.CniNamespace", new=NetnsMock)
    @patch("cni.common.interface.IPRoute", new=IPRouteMock)
    def test_delete_link(self):
        iface = interface.Interface(self.mock_cni)
        self.mock_cni.container_ifname = "eth2"
        iface.delete_link()
        self.assertFalse(NetnsMock.find("eth2", CONTAINER_NS_PID) in
                         NetnsMock.ns_ifaces[CONTAINER_NS_PID])
        self.mock_cni.container_ifname = "eth200"
        iface.delete_link()

    @patch("cni.common.interface.CniNamespace", new=NetnsMock)
    @patch("cni.common.interface.IPRoute", new=IPRouteMock)
    def test_configure_link(self):
        iface = interface.Interface(self.mock_cni)
        self.mock_cni.container_ifname = "eth2"
        iface.configure_interface("10.10.0.3", 24, "10.10.0.1")
        eth2 = NetnsMock.get("eth2", CONTAINER_NS_PID)
        self.assertEquals("up", eth2["state"])
        self.assertEquals("10.10.0.3", eth2["addr"])
        self.assertEquals(24, eth2["plen"])
        self.assertIn({
            'dst': '0.0.0.0/0',
            'gw': '10.10.0.1'
        }, NetnsMock.routes)

    @patch.object(ExtendedInterface, "create_interface")
    @patch.object(ExtendedInterface, "configure_interface")
    def test_add(self, mock_configure, mock_create):
        iface = ExtendedInterface(self.mock_cni)
        iface.add("10.10.2.3", "16", "10.10.2.5")
        mock_create.assert_called_once_with()
        mock_configure.assert_called_once_with("10.10.2.3", "16", "10.10.2.5")

    @patch.object(ExtendedInterface, "delete_interface")
    def test_delete(self, mock_delete):
        iface = ExtendedInterface(self.mock_cni)
        iface.delete()
        mock_delete.assert_called_once_with()
