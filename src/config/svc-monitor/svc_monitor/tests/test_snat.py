import mock
from mock import patch
import unittest
from vnc_api.vnc_api import *
from svc_monitor.instance_manager import NetworkNamespaceManager
from svc_monitor.config_db import *
import test_common_utils as test_utils

class SnatInstanceManager(unittest.TestCase):
    def setUp(self):
        InstanceIpSM._cassandra = mock.MagicMock()
        InstanceIpSM._cassandra.object_read = test_utils.iip_db_read
        VirtualMachineInterfaceSM._cassandra = mock.MagicMock()
        VirtualMachineInterfaceSM._cassandra.object_read = test_utils.vmi_db_read
        VirtualMachineSM._cassandra = mock.MagicMock()
        VirtualMachineSM._cassandra.object_read = test_utils.vm_db_read
        VirtualRouterSM._cassandra = mock.MagicMock()
        VirtualRouterSM._cassandra.object_read = test_utils.vr_db_read
        VirtualNetworkSM._cassandra = mock.MagicMock()
        VirtualNetworkSM._cassandra.object_read = test_utils.vn_db_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = test_utils.get_vn_id_for_fq_name
        self.mocked_vnc.virtual_machine_interface_create = test_utils.vmi_create
        self.mocked_vnc.virtual_network_create = test_utils.vn_create
        self.mocked_vnc.instance_ip_create = test_utils.iip_create

        self.mocked_scheduler = mock.MagicMock()
        self.mocked_scheduler.schedule = mock.Mock(return_value=('fake-virtual-router'))

        self.nova_mock = mock.MagicMock()
        self.mocked_db = mock.MagicMock()

        self.mocked_args = mock.MagicMock()
        self.mocked_args.availability_zone = None

        self.mocked_manager = mock.MagicMock()

        self.netns_manager = NetworkNamespaceManager(
            db=self.mocked_db, logger=mock.MagicMock(),
            vnc_lib=self.mocked_vnc, vrouter_scheduler=self.mocked_scheduler,
            nova_client=self.nova_mock, agent_manager=self.mocked_manager,
            args=self.mocked_args)

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        del InstanceIpSM._cassandra
        VirtualMachineInterfaceSM.reset()
        del VirtualMachineInterfaceSM._cassandra
        VirtualMachineSM.reset()
        del VirtualMachineSM._cassandra
        VirtualRouterSM.reset()
        del VirtualRouterSM._cassandra
        VirtualNetworkSM.reset()
        del VirtualNetworkSM._cassandra

    def test_snat_instance_create(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_virtual_network('fake-domain:fake-project:public-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:fake-vn-uuid')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_router('fake-virtual-router')

        st = test_utils.create_test_st(name='snat-template',
            virt_type='network-namespace',
            intf_list=[['right', True], ['left', True]])
        si = test_utils.create_test_si(name='snat-instance', count=2,
            intf_list=['public-vn', 'fake-vn-uuid'])

        self.netns_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(1))
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(2))
        self.assertEqual(si.vn_info[1]['net-id'], 'fake-domain:fake-project:fake-vn-uuid')

    def test_snat_instance_delete(self):
        def create_fake_virtual_machine(fq_name_str):
            vm_obj = {}
            vm_obj['fq_name'] = fq_name_str.split(':')
            vm_obj['uuid'] = fq_name_str
            vm_obj['display_name'] = fq_name_str
            vm = VirtualMachineSM.locate(vm_obj['uuid'], vm_obj)
            vm.proj_fq_name = ['fake-domain', 'fake-project']
            vm.virtual_machine_interfaces = set(['fake-vmi-uuid1', 'fake-vmi-uuid2', 'fake-vmi-uuid3'])
            vm.virtual_router = 'fake-vr-uuid'
            return vm

        mocked_vr = mock.MagicMock()
        mocked_vr.uuid = 'fake-vr-uuid'

        self.netns_manager._vnc_lib.virtual_router_read.\
            return_value = mocked_vr

        vm = create_fake_virtual_machine('fake-vm-uuid')
        self.netns_manager.delete_service(vm)

        self.netns_manager._vnc_lib.virtual_machine_delete\
            .assert_called_with(id='fake-vm-uuid')
        self.netns_manager._vnc_lib.ref_update.\
                assert_called_with('virtual-router', 'fake-vr-uuid',
                        'virtual-machine', 'fake-vm-uuid', None, 'DELETE')
