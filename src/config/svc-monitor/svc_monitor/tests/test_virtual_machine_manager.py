import mock
import unittest
from vnc_api.vnc_api import *
from svc_monitor.virtual_machine_manager import VirtualMachineManager
from svc_monitor.config_db import *
import test_common_utils as test_utils

class VirtualMachineManagerTest(unittest.TestCase):
    def setUp(self):
        VirtualMachineSM._cassandra = mock.MagicMock()
        VirtualMachineSM._cassandra.object_read = test_utils.vm_db_read
        VirtualMachineInterfaceSM._cassandra = mock.MagicMock()
        VirtualMachineInterfaceSM._cassandra.object_read = test_utils.vmi_db_read
        InstanceIpSM._cassandra = mock.MagicMock()
        InstanceIpSM._cassandra.object_read = test_utils.iip_db_read
        InterfaceRouteTableSM._cassandra = mock.MagicMock()
        InterfaceRouteTableSM._cassandra.object_read = test_utils.irt_db_read
        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = test_utils.get_vn_id_for_fq_name
        self.mocked_vnc.virtual_network_create = test_utils.vn_create
        self.mocked_vnc.virtual_machine_interface_create = test_utils.vmi_create
        self.mocked_vnc.instance_ip_create = test_utils.iip_create

        self.nova_mock = mock.MagicMock()
        self.mocked_db = mock.MagicMock()

        self.mocked_args = mock.MagicMock()
        self.mocked_args.availability_zone = 'default-availability-zone'

        self.log_mock = mock.MagicMock()

        self.vm_manager = VirtualMachineManager(
            db=self.mocked_db, logger=self.log_mock,
            vnc_lib=self.mocked_vnc, vrouter_scheduler=mock.MagicMock(),
            nova_client=self.nova_mock, args=self.mocked_args,
            agent_manager=mock.MagicMock())

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        VirtualMachineInterfaceSM.reset()
        VirtualMachineSM.reset()
        del InterfaceRouteTableSM._cassandra
        del VirtualMachineSM._cassandra

    def test_virtual_machine_create(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                nova_vm = test_utils.FakeNovaServer('fake-vm-uuid', kwargs['name'])
                return nova_vm
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.vm_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(1))
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(2))
        self.assertTrue(si.availability_zone, 'default-availability-zone') 

    def test_virtual_machine_delete(self):
        vm = test_utils.create_test_virtual_machine('fake-vm-uuid')
        self.vm_manager.delete_service(vm)

    def test_missing_image_in_template(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        st.params['image_name'] = None
        self.vm_manager.create_service(st, si)
        self.log_mock.error.assert_called_with("Image not present in %s" % ((':').join(st.fq_name)))

    def test_missing_image_in_nova(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'images' and oper == 'find':
                return None
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.vm_manager.create_service(st, si)
        self.log_mock.error.assert_called_with("Image not found %s" % si.image)

    def test_nova_vm_create_fail(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                return None
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.vm_manager.create_service(st, si)
        self.log_mock.error.assert_any_call(test_utils.AnyStringWith('Nova vm create failed'))

    def test_missing_flavor_in_template(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'flavors' and oper == 'find':
                return None
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        st.params['flavor'] = None
        self.vm_manager.create_service(st, si)
        self.log_mock.error.assert_called_with(test_utils.AnyStringWith("Flavor not found"))

    def test_availability_zone_setting(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                nova_vm = test_utils.FakeNovaServer('fake-vm-uuid', kwargs['name'])
                return nova_vm
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        st.params['availability_zone_enable'] = True
        si.params['availability_zone'] = 'test-availability-zone'
        self.vm_manager.create_service(st, si)
        self.assertTrue(si.availability_zone, 'test-availability-zone') 

    def test_network_config_validation(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        st.params['interface_type'] = []
        self.vm_manager.create_service(st, si)
        self.log_mock.notice.assert_called_with("Interface list empty for ST %s SI %s" %
            ((':').join(st.fq_name), (':').join(si.fq_name)))

    def test_virtual_machine_exists(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                nova_vm = test_utils.FakeNovaServer(kwargs['name'], kwargs['name'])
                return nova_vm
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.mocked_vnc.virtual_machine_create = test_utils.vm_create

        self.vm_manager.create_service(st, si)
        self.log_mock.info.assert_any_call(test_utils.AnyStringWith('Launching VM :'))
        self.log_mock.info.assert_any_call(test_utils.AnyStringWith('Created VM :'))
        self.log_mock.info.assert_any_call(test_utils.AnyStringWith(si.name))
        self.log_mock.reset_mock()

        self.vm_manager.create_service(st, si)
        self.assertTrue(self.log_mock.info.call_count, 1)

    def test_virtual_machine_static_routes(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        test_utils.create_test_virtual_network('fake-domain:fake-project:left-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st = test_utils.create_test_st(name='vm-template',
            virt_type='virtual-machine',
            intf_list=[['management', False], ['left', True, True], ['right', False]])
        si = test_utils.create_test_si(name='vm-instance', count=2,
            intf_list=['', 'left-vn', 'right-vn'])

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                nova_vm = test_utils.FakeNovaServer('fake-vm-uuid', kwargs['name'])
                return nova_vm
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.vm_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(1))
        self.mocked_vnc.virtual_machine_create.assert_any_call(test_utils.VMObjMatcher(2))
