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
from kube_cni.vrouter import vrouter
from kube_cni.params import params

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

class CniNetnsTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    @patch('os.open', new=Mock(return_value=1))
    @patch('os.getppid', new=Mock(return_value=333))
    @patch.object(iproute.IPRouteMixin, 'link_lookup')
    @patch.object(iproute.IPRouteMixin, 'link')
    def test_delete_veth(self, mock_link, mock_link_lookup):
        mock_link_lookup.side_effect = lambda ifname: [3] if ifname=="veth_3" else []
        c = cni.CniNetns("veth_3", "eth0", "netns")
        c.delete_veth()
        mock_link.assert_called_once_with("del", index=3)

    @patch('os.open', new=Mock(return_value=1))
    @patch('os.getppid', new=Mock(return_value=333))
    @patch.object(cni.CniNamespace, '__enter__')
    @patch.object(cni.CniNamespace, '__exit__')
    @patch.object(iproute.IPRouteMixin, 'link_lookup')
    @patch.object(iproute.IPRouteMixin, 'link_create')
    @patch.object(iproute.IPRouteMixin, 'link')
    def test_create_veth(self, mock_link, mock_link_create, mock_link_lookup,
                         mock_exit, mock_enter):
        netns = ["host"]
        ifaces = ["eth0", "eth1", "eth2", "eth3"]

        def enter():
            netns[0] = "container"
        mock_enter.side_effect = enter

        def exit(type, value, tb):
            netns[0] = "host"
        mock_exit.side_effect = exit

        def link_lookup(ifname):
            for i,x in enumerate(ifaces):
                if x==ifname:
                    return [i]
            return []
        mock_link_lookup.side_effect = link_lookup

        def link_create(ifname, peer, kind):
            self.assertEquals(netns[0], "container")
            ifaces.append(ifname)
        mock_link_create.side_effect = link_create

        c = cni.CniNetns("veth_4", "eth0", "netns")
        c.create_veth()
        mock_link_create.assert_called_once_with(ifname="veth_4", peer="eth0",
                                            kind="veth")
        mock_link.assert_called_once_with("set", index=4, net_ns_pid=333)

    @patch('os.open', new=Mock(return_value=1))
    @patch('os.getppid', new=Mock(return_value=333))
    @patch.object(cni.CniNamespace, '__enter__')
    @patch.object(cni.CniNamespace, '__exit__')
    @patch.object(iproute.IPRouteMixin, 'link_lookup')
    @patch.object(iproute.IPRouteMixin, 'link')
    @patch.object(iproute.IPRouteMixin, 'addr')
    @patch.object(iproute.IPRouteMixin, 'route')
    def test_configure_veth(self, mock_route, mock_addr, mock_link, mock_link_lookup,
                         mock_exit, mock_enter):
        netns = ["host"]
        ifaces = {
            'host': ['eth0', 'eth1', 'veth0', 'veth2'],
            'container': ['eth0', 'eth1']
        }

        def enter():
            netns[0] = "container"
        mock_enter.side_effect = enter

        def exit(type, value, tb):
            netns[0] = "host"
        mock_exit.side_effect = exit

        def link_lookup(ifname):
            for i,x in enumerate(ifaces[netns[0]]):
                if x==ifname:
                    return [i]
            return []
        mock_link_lookup.side_effect = link_lookup

        def link(cmd, index, state=None, address=None):
            self.assertEqual(cmd, 'set')
            if netns[0] == "container" and state is not None:
                self.assertEqual(state, "up")
                self.assertEquals(ifaces[netns[0]][index], "eth1")
            elif netns[0] == "container" and address is not None:
                self.assertEqual(address, "00:0A:E6:3E:FD:E1")
                self.assertEquals(ifaces[netns[0]][index], "eth1")
            else:
                self.assertEqual(netns[0], "host")
                self.assertEqual(state, "up")
                self.assertEquals(ifaces[netns[0]][index], "veth0")
        mock_link.side_effect = link

        def addr(cmd, index, address, prefixlen):
            self.assertEqual(netns[0], "container")
            self.assertEqual(cmd, "add")
            self.assertEqual(ifaces[netns[0]][index], "eth1")
            self.assertEquals(address, "10.1.2.3")
            self.assertEquals(prefixlen, 16)
        mock_addr.side_effect = addr

        def route(cmd, dst, gateway):
            self.assertEqual(netns[0], "container")
            self.assertEqual(cmd, "add")
            self.assertEquals(gateway, "10.1.0.1")
        mock_route.side_effect = route

        c = cni.CniNetns("veth0", "eth1", "netns")
        c.configure_veth("00:0A:E6:3E:FD:E1", "10.1.2.3", 16, "10.1.0.1")

class CniTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_BuildTapName(self):
        c = cni.Cni(Mock(spec=vrouter.VRouter), Mock(spec=params.Params))
        self.assertEqual(c.BuildTapName("something"), "cn-something")

    def test_BuildCniResponse(self):
        vr_resp = {
            'ip-address': '10.1.2.3',
            'dns-server': '10.1.0.100',
            'gateway': '10.1.0.1',
            'plen': 16
        }
        c = cni.Cni(Mock(spec=vrouter.VRouter), Mock(spec=params.Params))
        self.assertEqual(c.BuildCniResponse(vr_resp), {
            'cniVersion': cni.CNI_VERSION,
            'ip4': {
                'gateway': '10.1.0.1',
                'ip': '10.1.2.3/16',
                'routes': [{
                    'dst': '0.0.0.0/0',
                    'gw': '10.1.0.1'
                }]
            },
            'dns': {
                'nameservers': ['10.1.0.100']
            }
        })

    @patch.object(cni.Cni, "BuildCniResponse")
    def test_get_cmd(self, mock_build_resp):
        mock_build_resp.side_effect = lambda x: x
        mock_params = Mock(spec=params.Params)
        mock_params.k8s_params = Mock(spec=params.K8SParams)
        mock_params.k8s_params.pod_uuid = 123
        mock_vrouter = Mock(spec=vrouter.VRouter)
        mock_vrouter.get_cmd = lambda x, y: {'result':'ok'} if x==123 else {}
        c = cni.Cni(mock_vrouter, mock_params)
        self.assertEqual(c.get_cmd(), {'result': 'ok'})

    @patch.object(cni.Cni, "BuildCniResponse")
    def test_poll_cmd(self, mock_build_resp):
        mock_build_resp.side_effect = lambda x: x
        mock_params = Mock(spec=params.Params)
        mock_params.k8s_params = Mock(spec=params.K8SParams)
        mock_params.k8s_params.pod_uuid = 123
        mock_vrouter = Mock(spec=vrouter.VRouter)
        mock_vrouter.poll_cmd = lambda x, y: {'result':'ok'} if x==123 else {}
        c = cni.Cni(mock_vrouter, mock_params)
        self.assertEqual(c.poll_cmd(), {'result': 'ok'})


    @patch.object(cni.CniNetns, "create_veth")
    @patch.object(cni.CniNetns, "configure_veth")
    @patch.object(cni.Cni, "BuildCniResponse")
    def test_add_cmd(self, mock_build_resp, mock_configure_veth,
                     mock_create_veth):
        mock_build_resp.side_effect = lambda x: x
        mock_params = Mock(spec=params.Params)
        mock_params.container_id = "ContainerXYZ"
        mock_params.container_ifname = "veth0"
        mock_params.container_netns = "cont_netns"
        mock_params.k8s_params = Mock(spec=params.K8SParams)
        mock_params.k8s_params.pod_uuid = "123"
        mock_params.k8s_params.pod_pid = 234
        mock_params.k8s_params.pod_name = "pod"
        mock_params.k8s_params.pod_namespace = "netns"
        mock_vrouter = Mock(spec=vrouter.VRouter)
        mock_vrouter.add_cmd = lambda a,b,c,d,e,f: {
            'mac-address': '00',
            'ip-address': '10.0.0.' + a,
            'plen': 16,
            'gateway': '10.0.0.1'
        }
        c = cni.Cni(mock_vrouter, mock_params)
        self.assertEqual(c.add_cmd(), {
            'mac-address': '00',
            'ip-address': '10.0.0.123',
            'plen': 16,
            'gateway': '10.0.0.1'
        })
        mock_create_veth.assert_called_once_with()
        mock_configure_veth.assert_called_once_with("00", "10.0.0.123", 16, "10.0.0.1")

    @patch.object(cni.CniNetns, "delete_veth")
    def test_delete_cmd(self, mock_delete_veth):
        mock_params = Mock(spec=params.Params)
        mock_params.container_id = "ContainerXYZ"
        mock_params.container_ifname = "veth0"
        mock_params.container_netns = "cont_netns"
        mock_params.k8s_params = Mock(spec=params.K8SParams)
        mock_params.k8s_params.pod_uuid = "123"
        mock_params.k8s_params.pod_pid = 234
        mock_vrouter = Mock(spec=vrouter.VRouter)
        mock_vrouter.delete_cmd = Mock()
        c = cni.Cni(mock_vrouter, mock_params)
        self.assertEqual(c.delete_cmd(), {
            'cniVersion': cni.CNI_VERSION,
            'code': 0,
            'msg': 'Delete passed'
        })
        mock_delete_veth.assert_called_once_with()
        mock_vrouter.delete_cmd.assert_called_once_with("123", None)

    def test_Version(self):
        c = cni.Cni(Mock(spec=vrouter.VRouter), Mock(spec=params.Params))
        self.assertEqual(c.Version(), {
            'cniVersion': cni.CNI_VERSION,
            'supportedVersions': [cni.CNI_VERSION]
        })

    @patch.object(cni.Cni, "Version")
    @patch.object(cni.Cni, "add_cmd")
    @patch.object(cni.Cni, "delete_cmd")
    @patch.object(cni.Cni, "get_cmd")
    @patch.object(cni.Cni, "poll_cmd")
    @patch("common.logger.Logger", new=Mock())
    def test_Run(self, mock_poll, mock_get, mock_delete, mock_add, mock_version):
        mock_vrouter = Mock(spec=vrouter.VRouter)
        mock_params = Mock(spec=params.Params)
        mock_params.contrail_params = Mock()
        mock_params.contrail_params.log_file = Mock()
        mock_params.contrail_params.log_level = Mock()
        mock_poll.return_value = { }
        mock_get.return_value = { }
        mock_delete.return_value = { }
        mock_add.return_value = { }
        mock_version.return_value = { }
        c = cni.Cni(mock_vrouter, mock_params)
        mock_params.command = "version"
        c.Run(mock_vrouter, mock_params)
        mock_version.assert_called_once_with()
        mock_params.command = "add"
        c.Run(mock_vrouter, mock_params)
        mock_add.assert_called_once_with()
        mock_params.command = "delete"
        c.Run(mock_vrouter, mock_params)
        mock_delete.assert_called_once_with()
        mock_params.command = "get"
        c.Run(mock_vrouter, mock_params)
        mock_get.assert_called_once_with()
        mock_params.command = "poll"
        c.Run(mock_vrouter, mock_params)
        mock_poll.assert_called_once_with()
        mock_params.command = "unknown"
        self.assertRaises(cni.CniError, c.Run, mock_vrouter, mock_params)
