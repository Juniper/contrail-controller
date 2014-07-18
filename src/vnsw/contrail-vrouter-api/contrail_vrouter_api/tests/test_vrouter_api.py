# Copyright (c) 2014 Juniper Networks, Inc

import mock
import unittest
import uuid

from contrail_vrouter_api.vrouter_api import ContrailVRouterApi
from contrail_vrouter_api.gen_py.instance_service import InstanceService
from contrail_vrouter_api.gen_py.instance_service import ttypes


class VRouterApiTest(unittest.TestCase):
    def setUp(self):
        self._api = ContrailVRouterApi()

    def test_create_port(self):
        mock_client = mock.Mock()
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = mock_client
        self._api.add_port(str(uuid.uuid1()), str(uuid.uuid1()), 'tapX',
                           'aa:bb:cc:ee:ff:00')
        self.assertTrue(mock_client.AddPort.called)

    def test_delete_port(self):
        mock_client = mock.Mock()
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = mock_client

        vm_uuid = uuid.uuid1()
        vif_uuid = uuid.uuid1()
        self._api.add_port(str(vm_uuid), str(vif_uuid), 'tapX',
                           'aa:bb:cc:ee:ff:00')
        self.assertTrue(mock_client.AddPort.called)
        self.assertTrue(self._api._ports[vif_uuid])

        self._api.delete_port(str(vif_uuid))
        self.assertTrue(mock_client.DeletePort.called)

    def test_resynchronize(self):
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = None

        vm_uuid = str(uuid.uuid1())
        vif_uuid = str(uuid.uuid1())
        port1 = ttypes.Port(self._api._uuid_string_to_hex(vif_uuid),
                           self._api._uuid_string_to_hex(vm_uuid),
                           'tapX', '0.0.0.0', [0] * 16, 'aa:bb:cc:ee:ff:00')
        self._api.add_port(vm_uuid, vif_uuid, 'tapX', 'aa:bb:cc:ee:ff:00')

        mock_client = mock.Mock()
        self._api._rpc_client_instance.return_value = mock_client
        self._api.periodic_connection_check()
        mock_client.AddPort.assert_called_with([port1])

    def test_resynchronize_multi_ports(self):
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = None

        vm_uuid = str(uuid.uuid1())
        vif_uuid = str(uuid.uuid1())
        port1 = ttypes.Port(self._api._uuid_string_to_hex(vif_uuid),
                            self._api._uuid_string_to_hex(vm_uuid),
                            'tapX', '0.0.0.0', [0] * 16, 'aa:bb:cc:ee:ff:00')
        self._api.add_port(vm_uuid, vif_uuid, 'tapX', 'aa:bb:cc:ee:ff:00')

        vm_uuid = str(uuid.uuid1())
        vif_uuid = str(uuid.uuid1())
        port2 = ttypes.Port(self._api._uuid_string_to_hex(vif_uuid),
                            self._api._uuid_string_to_hex(vm_uuid),
                            'tapY', '0.0.0.0', [0] * 16, '11:22:33:44:55:66')
        self._api.add_port(vm_uuid, vif_uuid, 'tapY', '11:22:33:44:55:66')

        mock_client = mock.Mock()
        self._api._rpc_client_instance.return_value = mock_client
        self._api.connect()
        self._api._resynchronize()
        mock_client.AddPort.assert_called_with([port1, port2])

    def test_additional_arguments(self):
        mock_client = mock.Mock()
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = mock_client
        vif_uuid = uuid.uuid1()
        network_uuid = uuid.uuid1()
        project_uuid = uuid.uuid1()
        self._api.add_port(str(uuid.uuid1()), str(vif_uuid), 'tapX',
                           'aa:bb:cc:ee:ff:00',
                           network_uuid=str(network_uuid),
                           vm_project_uuid=str(project_uuid))
        self.assertTrue(mock_client.AddPort.called)
        port = self._api._ports[vif_uuid]
        self.assertEqual(self._api._uuid_to_hex(network_uuid),
                         port.vn_id)
        self.assertEqual(self._api._uuid_to_hex(project_uuid),
                         port.vm_project_uuid)
