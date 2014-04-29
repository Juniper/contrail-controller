import mock
import unittest
import uuid

from contrail_vrouter_api.vrouter_api import ContrailVRouterApi
from contrail_vrouter_api.gen_py.instance_service import InstanceService, ttypes

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

        vm_uuid = str(uuid.uuid1())
        vif_uuid = str(uuid.uuid1())
        self._api.add_port(vm_uuid, vif_uuid, 'tapX', 'aa:bb:cc:ee:ff:00')
        self.assertTrue(mock_client.AddPort.called)
        self.assertTrue(self._api._ports[vif_uuid])

        self._api.delete_port(vif_uuid)
        self.assertTrue(mock_client.DeletePort.called)


    def test_resynchronize(self):
        self._api._rpc_client_instance = mock.MagicMock(
            name='rpc_client_instance')
        self._api._rpc_client_instance.return_value = None

        vm_uuid = str(uuid.uuid1())
        vif_uuid = str(uuid.uuid1())
        self._api.add_port(vm_uuid, vif_uuid, 'tapX', 'aa:bb:cc:ee:ff:00')

        mock_client = mock.Mock()
        self._api._rpc_client_instance.return_value = mock_client
        self._api.periodic_connection_check()
        self.assertTrue(mock_client.AddPort.called)
        
