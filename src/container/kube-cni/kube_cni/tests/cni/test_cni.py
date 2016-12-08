import json
import mock
import unittest
import os
import requests
import sys
import errno
from pyroute2 import iproute
from mock import patch
from mock import Mock

from kube_cni import kube_cni
from kube_cni.cni import cni
from kube_cni.cni import cni

class CniNamespaceTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.close')
    @patch('os.open', new=Mock(return_value=1))
    def test_close_files(self, close):
        c = cni.CniNamespace("/run/netns/ns", Mock())
        c.ns_fd = None
        c.my_fd = None
        c.close_files()
        self.assertEqual(close.call_count, 0)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        close.reset_mock()
        c.ns_fd = 1
        c.close_files()
        close.assert_called_once_with(1)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        close.reset_mock()
        c.my_fd = 2
        c.close_files()
        close.assert_called_once_with(2)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))
        close.reset_mock()
        c.ns_fd = 1
        c.my_fd = 2
        c.close_files()
        self.assertEqual(close.call_count, 2)
        self.assertEqual((c.my_fd, c.ns_fd), (None, None))

#    @patch('ctypes.CDLL')
#    @patch('os.open', new=Mock(return_value=1))
#    def test_context_manager(self, mock_cdll):
#        mock_libc = Mock()
#        mock_libc.setns = Mock(return_value=0)
#        mock_cdll.return_value = mock_libc
#        with cni.CniNamespace("/run/netns/ns", Mock()):
#            mock_libc.setns.assert_called_once_with("/run/netns/ns")
#        mock_libc.setns.assert_called_once_with("/proc/self/ns/net")

def link_lookup(ifname):
    if ifname == "veth0":
        return [3]
    else:
        return []

class CniNetnsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.open', new=Mock(return_value=1))
    @patch('os.getpid', new=Mock(return_value=333))
    @patch.object(iproute.IPRouteMixin, 'link_lookup')
    @patch.object(iproute.IPRouteMixin, 'link')
    def test_delete_veth(self, mock_link, mock_link_lookup):
        mock_link_lookup.side_effect = link_lookup
        c = cni.CniNetns("veth0", "eth0", "netns")
        c.delete_veth()
        mock_link.assert_called_once_with("del", index=3)

    @patch('os.open', new=Mock(return_value=1))
    @patch('os.getpid', new=Mock(return_value=333))
    @patch.object(cni.CniNamespace, '__enter__', new=Mock())
    @patch.object(cni.CniNamespace, '__exit__', new=Mock())
    @patch.object(iproute.IPRouteMixin, 'link_lookup')
    @patch.object(iproute.IPRouteMixin, 'link_create')
    @patch.object(iproute.IPRouteMixin, 'link')
    def test_create_veth(self, mock_link, mock_link_create, mock_link_lookup):
        mock_link_lookup.side_effect = link_lookup
        c = cni.CniNetns("veth1", "eth0", "netns")
        c.create_veth()
        mock_link_create.assert_called_once_with(ifname="veth1", peer="eth0",
                                            kind="veth")
        mock_link.assert_called_once_with("set", index=3, net_ns_pid=333)
