import json
import mock
import unittest
import os
import requests
import sys
import errno
from pyroute2 import iproute, IPRoute, NetlinkError
from mock import patch
from mock import Mock

from cni.test.common.netns_mock import *
from cni.common.macvlan import *
from cni.common import interface

class CniMacVlanTest(unittest.TestCase):
    def setUp(self):
        NetnsMock.reset()
        self.mock_cni = Mock()
        self.mock_cni.container_ifname = "eth0"
        self.mock_cni.container_uuid = "default_uuid"

    def tearDown(self):
        pass

    @patch.object(CniMacVlan, "get_link")
    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_delete_interface(self, mock_get_link):
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "iface", "tag")
        mock_get_link.return_value = [{
            "attrs": [ ("IFLA_LINK", 6) ]
            }]
        self.assertIn(6, NetnsMock.ns_ifaces[CONTAINER_NS_PID])
        mvlan.delete_interface()
        self.assertNotIn(6, NetnsMock.ns_ifaces[CONTAINER_NS_PID])
        with self.assertRaises(Error) as err:
            mvlan.delete_interface()
        self.assertEquals(CNI_ERROR_DEL_VLAN_INTF, err.exception.code)
        mock_get_link.return_value = [{
            "attrs": [
                ('IFLA_TXQLEN', 1),
                ('IFLA_OPERSTATE', 'UNKNOWN'),
                ('IFLA_LINKMODE', 0),
                ('IFLA_MTU', 65536),
                ('IFLA_GROUP', 0)
            ]
        }]
        with self.assertRaises(Error) as err:
            mvlan.delete_interface()
        self.assertEquals(CNI_ERROR_DEL_VLAN_INTF, err.exception.code)

    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_locate_parent_interface(self):
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "veth1", "tag")
        idx = mvlan._locate_parent_interface(IPRouteMock())
        self.assertEquals("veth1", NetnsMock.ifaces[idx]["name"])
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "veth9", "tag")
        with self.assertRaises(Error) as err:
            mvlan._locate_parent_interface(IPRouteMock())
        self.assertEquals(CNI_ERROR_GET_PARENT_INTF, err.exception.code)

    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_locate_vlan_interface(self):
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "veth1", "tag")
        self.assertIsNone(NetnsMock.get("cn-tag", HOST_NS_PID))
        idx = mvlan._locate_vlan_interface(IPRouteMock(), 2)
        iface = NetnsMock.get("cn-tag", HOST_NS_PID)
        self.assertIsNotNone(iface)
        self.assertEquals("cn-tag", iface["name"])
        self.assertEquals("vlan", iface["kind"])
        self.assertEquals("tag", iface["vlan_id"])
        self.assertEquals(2, iface["link"])
        idx2 = mvlan._locate_vlan_interface(IPRouteMock(), 3)
        self.assertEquals(idx, idx2)
        self.assertEquals(2, iface["link"])

    @patch("os.getpid", new=NetnsMock.getpid)
    @patch("cni.common.macvlan.CniNamespace", new=NetnsMock)
    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_locate_peer_vlan_interface(self):
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "veth1", "tag")
        self.assertIsNone(NetnsMock.get("cn-ifname", CONTAINER_NS_PID))
        idx = mvlan._locate_peer_vlan_interface(
            IPRouteMock(HOST_NS_PID),
            IPRouteMock(CONTAINER_NS_PID),
            2,
            "cn-ifname")
        iface = NetnsMock.get("cn-ifname", CONTAINER_NS_PID)
        self.assertIsNotNone(iface)
        self.assertEquals(CONTAINER_NS_PID, iface["ns"])
        self.assertEquals("macvlan", iface["kind"])
        self.assertEquals(2, iface["link"])
        self.assertEquals("vepa", iface["macvlan_mode"])

        NetnsMock.add("cn-ifname-2", CONTAINER_NS_PID, link=2)
        idx = mvlan._locate_peer_vlan_interface(
            IPRouteMock(HOST_NS_PID),
            IPRouteMock(CONTAINER_NS_PID),
            20,
            "cn-ifname-2")
        iface = NetnsMock.get("cn-ifname-2", CONTAINER_NS_PID)
        self.assertIsNotNone(iface)
        self.assertEquals(2, iface["link"])

        NetnsMock.add("cn-ifname-3", HOST_NS_PID, link=2)
        idx = mvlan._locate_peer_vlan_interface(
            IPRouteMock(HOST_NS_PID),
            IPRouteMock(CONTAINER_NS_PID),
            20,
            "cn-ifname-3")
        iface = NetnsMock.get("cn-ifname-3", CONTAINER_NS_PID)
        self.assertIsNotNone(iface)
        self.assertEquals(2, iface["link"])

    @patch("os.getpid", new=NetnsMock.getpid)
    @patch("cni.common.macvlan.CniNamespace", new=NetnsMock)
    @patch("cni.common.interface.CniNamespace", new=NetnsMock)
    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_create_interface(self):
        self.mock_cni.container_ifname = "container_ifname"
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "eth1", "tag")
        mvlan.create_interface()
        parent_idx = NetnsMock.find("eth1", HOST_NS_PID)
        self.assertIsNotNone(parent_idx)
        vlan_iface = NetnsMock.get("cn-tag", HOST_NS_PID)
        vlan_idx = NetnsMock.find("cn-tag", HOST_NS_PID)
        self.assertIsNotNone(vlan_iface)
        self.assertEquals(parent_idx, vlan_iface["link"])
        cn_iface = NetnsMock.get("container_ifname", CONTAINER_NS_PID)
        self.assertIsNotNone(cn_iface)
        self.assertEquals(vlan_idx, cn_iface["link"])

    @patch.object(interface.Interface, 'configure_interface')
    @patch("os.getpid", new=NetnsMock.getpid)
    @patch("cni.common.macvlan.CniNamespace", new=NetnsMock)
    @patch("cni.common.macvlan.IPRoute", new=IPRouteMock)
    def test_configure_interfcae(self, mock_configure):
        mvlan = CniMacVlan(self.mock_cni, "aa:bb:cc:dd:ee:ff", "eth1", "tag")
        NetnsMock.add("cn-tag", HOST_NS_PID)
        mvlan.configure_interface("10.10.10.2", 8, "10.0.0.1")
        mock_configure.assert_called_once_with("10.10.10.2", 8, "10.0.0.1")
        self.assertEquals("up", NetnsMock.get("cn-tag", HOST_NS_PID)["state"])

