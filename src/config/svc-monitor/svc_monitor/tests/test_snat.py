import mock
from mock import patch
import unittest
from vnc_api.vnc_api import *
from svc_monitor.instance_manager import NetworkNamespaceManager
from svc_monitor.config_db import *

class VMObjMatcher(object):
    """
    Object for assert_called_with to check if vm object is created properly
    """
    def __init__(self, index, check_delete=False):
        self.check_delete = check_delete
        self.index = index

    def _has_field(self, index, ob):
        if self.check_delete:
            if index == ob.fq_name[0]:
                return True
        else:
            if str(index) == ob.display_name.split('__')[-2]:
                return True
        return False

    def __eq__(self, other):
        if not(self._has_field(self.index, other)):
            return False
        return True

class SnatInstanceManager(unittest.TestCase):
    def setUp(self):
        def get_vn_id(obj_type, fq_name):
            if obj_type != 'virtual-network':
                return
            for vn in VirtualNetworkSM.values():
                if vn.fq_name == fq_name:
                    return vn.uuid
            raise NoIdError(fq_name)

        def vmi_create(vmi_obj):
            vmi_obj.uuid = 'fake-vmi-uuid'
            return

        def vn_create(vn_obj):
            vn_obj.uuid = 'fake-vn-uuid'
            return

        def vn_read(vn_id):
            vn_obj = {}
            vn_obj['uuid'] = 'fake-vn-uuid'
            vn_obj['fq_name'] = ['fake-domain', 'fake-project', 'fake-vn-uuid']
            return True, [vn_obj]

        def vm_read(vm_id):
            class SI(object):
                def __init__(self, name, fq_name):
                    self.name = name
                    self.fq_name = fq_name

            vm_obj = {}
            vm_obj['uuid'] = 'fake-vm-uuid'
            vm_obj['fq_name'] = ['fake-vm-uuid']
            fq_name = ['fake-domain', 'fake-project', 'fake-snat-instance']
            name = 'fake-snat-instance'
            si = SI(name, fq_name)
            instance_name = self.netns_manager._get_instance_name(si, 0)
            vm_obj['display_name'] = instance_name + '__' + 'network-namespace'
            return True, [vm_obj]

        def vr_read(vm_id):
            vr_obj = {}
            vr_obj['uuid'] = 'fake-vr-uuid'
            vr_obj['fq_name'] = ['fake-vr-uuid']
            return True, [vr_obj]

        VirtualMachineSM._cassandra = mock.MagicMock()
        VirtualMachineSM._cassandra._cassandra_virtual_machine_read = vm_read

        VirtualRouterSM._cassandra = mock.MagicMock()
        VirtualRouterSM._cassandra._cassandra_virtual_router_read = vr_read

        VirtualNetworkSM._cassandra = mock.MagicMock()
        VirtualNetworkSM._cassandra._cassandra_virtual_network_read = vn_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = get_vn_id
        self.mocked_vnc.virtual_machine_interface_create = vmi_create
        self.mocked_vnc.virtual_network_create = vn_create

        self.nova_mock = mock.MagicMock()
        self.mocked_db = mock.MagicMock()

        self.mocked_args = mock.MagicMock()
        self.mocked_args.availability_zone = None

        self.mocked_scheduler = mock.MagicMock()
        self.mocked_scheduler.schedule = mock.Mock(return_value=('fake-virtual-router'))

        self.netns_manager = NetworkNamespaceManager(
            db=self.mocked_db, logger=mock.MagicMock(),
            vnc_lib=self.mocked_vnc, vrouter_scheduler=self.mocked_scheduler,
            nova_client=self.nova_mock, args=self.mocked_args)

    def tearDown(self):
        ServiceTemplateSM.delete('fake-st-uuid')
        ServiceInstanceSM.delete('fake-si-uuid')
        pass

    def create_test_project(self, fq_name_str):
        proj_obj = {}
        proj_obj['fq_name'] = fq_name_str.split(':')
        proj_obj['uuid'] = fq_name_str
        proj_obj['id_perms'] = 'fake-id-perms'
        ProjectSM.locate(proj_obj['uuid'], proj_obj)

    def create_test_virtual_network(self, fq_name_str):
        vn_obj = {}
        vn_obj['fq_name'] = fq_name_str.split(':')
        vn_obj['uuid'] = fq_name_str
        vn_obj['id_perms'] = 'fake-id-perms'
        VirtualNetworkSM.locate(vn_obj['uuid'], vn_obj)

    def create_test_security_group(self, fq_name_str):
        sg_obj = {}
        sg_obj['fq_name'] = fq_name_str.split(':')
        sg_obj['uuid'] = fq_name_str
        sg_obj['id_perms'] = 'fake-id-perms'
        SecurityGroupSM.locate(sg_obj['uuid'], sg_obj)

    def create_test_virtual_machine(self, fq_name_str):
        vm_obj = {}
        vm_obj['fq_name'] = fq_name_str.split(':')
        vm_obj['uuid'] = fq_name_str
        vm_obj['display_name'] = fq_name_str
        vm = VirtualMachineSM.locate(vm_obj['uuid'], vm_obj)
        vm.proj_fq_name = ['fake-domain', 'fake-project']
        return vm

    def create_test_virtual_router(self, fq_name_str):
        vr_obj = {}
        vr_obj['fq_name'] = fq_name_str.split(':')
        vr_obj['name'] = fq_name_str.split(':')[0]
        vr_obj['uuid'] = fq_name_str
        vr_obj['display_name'] = fq_name_str
        vr = VirtualRouterSM.locate(vr_obj['uuid'], vr_obj)
        vr.agent_state = True
        return vr

    def test_snat_instance_create(self):
        self.create_test_project('fake-domain:fake-project')
        self.create_test_virtual_network('fake-domain:fake-project:public-vn')
        self.create_test_security_group('fake-domain:fake-project:default')
        self.create_test_virtual_router('fake-virtual-router')

        st_obj = {}
        st_obj['fq_name'] = ['fake-domain', 'fake-snat-template']
        st_obj['uuid'] = 'fake-st-uuid'
        st_obj['id_perms'] = 'fake-id-perms'
        st_props = {}
        st_props['service_virtualization_type'] = 'network-namespace'
        st_props['service_mode'] = 'in-network-nat'
        st_props['service_type'] = 'source-nat'
        st_props['ordered_interfaces'] = True
        st_props['service_scaling'] = True
        st_props['interface_type'] = [{'service_interface_type': 'right', 'shared_ip': True},
                                      {'service_interface_type': 'left', 'shared_ip': True}]
        st_obj['service_template_properties'] = st_props

        si_obj = {}
        si_obj['fq_name'] = ['fake-domain', 'fake-project', 'fake-snat-instance']
        si_obj['uuid'] = 'fake-si-uuid'
        si_obj['id_perms'] = 'fake-id-perms'
        si_props = {}
        si_props['scale_out'] = {'max_instances': 2}
        si_props['interface_list'] = [{'virtual_network': 'fake-domain:fake-project:public-vn'},
                                      {'virtual_network': ''}]
        si_obj['service_instance_properties'] = si_props

        st = ServiceTemplateSM.locate('fake-st-uuid', st_obj)
        si = ServiceInstanceSM.locate('fake-si-uuid', si_obj)

        self.netns_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(VMObjMatcher(1))
        self.mocked_vnc.virtual_machine_create.assert_any_call(VMObjMatcher(2))
        self.assertEqual(si.vn_info[1]['net-id'], 'fake-vn-uuid')

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
        mocked_vr.del_virtual_machine.assert_called_with(VMObjMatcher('fake-vm-uuid', True))
