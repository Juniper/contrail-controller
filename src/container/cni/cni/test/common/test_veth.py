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

from cni.test.common.netns_mock import *
from cni.common import veth
from cni.common import interface

class CniVethPairTest(unittest.TestCase):
    def setUp(self):
        NetnsMock.reset()
        self.mock_cni = Mock()
        self.mock_cni.container_ifname = "eth0"
        self.mock_cni.container_uuid = "default_uuid"

    def tearDown(self):
        pass

    @patch.object(veth.CniVEthPair, "delete_link")
    @patch("cni.common.interface.CniNamespace", new=NetnsMock)
    def test_delete_interface(self, mock_delete):
        v = veth.CniVEthPair(self.mock_cni, "aa:bb:cc:dd:ee:ff")
        v.delete_interface()
        mock_delete.assert_called_once_with()

    @patch("os.getpid", new=NetnsMock.getpid)
    @patch("cni.common.veth.CniNamespace", new=NetnsMock)
    @patch("cni.common.veth.IPRoute", new=IPRouteMock)
    def test_create_interface(self):
        self.mock_cni.container_uuid = "12345"
        self.mock_cni.container_ifname = "new_container_iface"
        v = veth.CniVEthPair(self.mock_cni, "aa:bb:cc:dd:ee:ff")
        v.create_interface()
        cont_iface = NetnsMock.get("new_container_iface", CONTAINER_NS_PID)
        host_iface = NetnsMock.get("tap12345", HOST_NS_PID)
        self.assertEquals(CONTAINER_NS_PID, cont_iface["ns"])
        self.assertEquals(HOST_NS_PID, host_iface["ns"])
        self.assertEquals("aa:bb:cc:dd:ee:ff", cont_iface["mac"])

    @patch.object(interface.Interface, 'configure_interface')
    @patch("os.getpid", new=NetnsMock.getpid)
    @patch("cni.common.veth.CniNamespace", new=NetnsMock)
    @patch("cni.common.veth.IPRoute", new=IPRouteMock)
    def test_configure_interfcae(self, mock_configure):
        self.mock_cni.container_uuid = "789"
        v = veth.CniVEthPair(self.mock_cni, "aa:bb:cc:dd:ee:ff")
        NetnsMock.add("tap789", HOST_NS_PID)
        v.configure_interface("10.10.10.2", 8, "10.0.0.1")
        mock_configure.assert_called_once_with("10.10.10.2", 8, "10.0.0.1")
        self.assertEquals("up", NetnsMock.get("tap789", HOST_NS_PID)["state"])

