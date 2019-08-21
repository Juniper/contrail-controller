from __future__ import absolute_import
import mock
from mock import patch
import unittest
from vnc_api.vnc_api import *
from svc_monitor.vrouter_instance_manager import VRouterInstanceManager
from svc_monitor.config_db import *
from . import test_common_utils as test_utils

class VRouterInstanceManagerTest(unittest.TestCase):
    def setUp(self):
        VirtualMachineSM._object_db = mock.MagicMock()
        VirtualMachineSM._object_db.object_read = test_utils.vm_db_read
        VirtualRouterSM._object_db = mock.MagicMock()
        VirtualRouterSM._object_db.object_read = test_utils.vr_db_read
        VirtualMachineInterfaceSM._object_db = mock.MagicMock()
        VirtualMachineInterfaceSM._object_db.object_read = test_utils.vmi_db_read
        InstanceIpSM._object_db = mock.MagicMock()
        InstanceIpSM._object_db.object_read = test_utils.iip_db_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = test_utils.get_vn_id_for_fq_name
        self.mocked_vnc.virtual_machine_interface_create = test_utils.vmi_create
        self.mocked_vnc.instance_ip_create = test_utils.iip_create

        self.nova_mock = mock.MagicMock()
        self.mocked_db = mock.MagicMock()

        self.mocked_args = mock.MagicMock()
        self.mocked_args.availability_zone = None

        self.vrouter_manager = VRouterInstanceManager(
            db=self.mocked_db, logger=mock.MagicMock(),
            vnc_lib=self.mocked_vnc, vrouter_scheduler=mock.MagicMock(),
            nova_client=self.nova_mock, args=self.mocked_args,
            agent_manager=mock.MagicMock())

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        del InstanceIpSM._object_db
        VirtualMachineInterfaceSM.reset()
        del VirtualMachineInterfaceSM._object_db
        VirtualMachineSM.reset()
        del VirtualMachineSM._object_db
        del VirtualRouterSM._object_db

    def test_vrouter_instance_create(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:mgmt-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vrouter-template',
            virt_type='vrouter-instance',
            intf_list=[['management', False], ['left', False], ['right', False]])
        si = test_utils.create_test_si(name='vr-instance', count=1, vr_id='fake-vr-id',
            intf_list=['mgmt-vn', 'left-vn', 'right-vn'])

        self.vrouter_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(1))
        self.mocked_vnc.virtual_router_update.assert_any_call(test_utils.VRObjMatcher(['fake-vm-uuid']))

    def test_vrouter_instance_delete(self):
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

        self.vrouter_manager._vnc_lib.virtual_router_read.\
            return_value = mocked_vr

        vm = create_fake_virtual_machine('fake-vm-uuid')
        self.vrouter_manager.delete_service(vm)

        self.vrouter_manager._vnc_lib.virtual_machine_delete\
            .assert_called_with(id='fake-vm-uuid')
        self.vrouter_manager._vnc_lib.ref_update.\
                assert_called_with('virtual-router', 'fake-vr-uuid',
                        'virtual-machine', 'fake-vm-uuid', None, 'DELETE')
