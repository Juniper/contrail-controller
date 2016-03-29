import mock
import unittest
from vnc_api.vnc_api import *
from svc_monitor.virtual_machine_manager import VirtualMachineManager
from svc_monitor.config_db import *


def test_get_instance_name(si, inst_count):
    name = si.name + '__' + str(inst_count + 1)
    instance_name = "__".join(si.fq_name[:-1] + [name])
    return instance_name


class VMObjMatcher(object):
    """
    Object for assert_called_with to check if vm object is created properly
    """
    def __init__(self, index):
        self.index = index

    def _has_field(self, index, ob):
        if str(index) == ob.display_name.split('__')[-2]:
            return True
        return False

    def __eq__(self, other):
        if not(self._has_field(self.index, other)):
            return False
        return True

class FakeNovaServer(object):
    def __init__(self, uuid, name):
        self.id = uuid
        self.name = name

    def get(self):
        if self.id:
            return True
        return False

    def delete(self):
        self.id = None
        self.name = None
        return

class VirtualMachineManagerTest(unittest.TestCase):
    def setUp(self):
        def get_vn_id(obj_type, fq_name):
            if obj_type != 'virtual-network':
                return
            for vn in VirtualNetworkSM.values():
                if vn.fq_name == fq_name:
                    return vn.uuid
            raise NoIdError(fq_name)

        def vn_create(vn_obj):
            vn_obj.uuid = (':').join(vn_obj.fq_name)
            vn = {}
            vn['uuid'] = vn_obj.uuid
            vn['fq_name'] = vn_obj.fq_name
            VirtualNetworkSM.locate(vn_obj.uuid, vn)
            return

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
            instance_name = test_get_instance_name(si, 0)
            vm_obj['display_name'] = instance_name + '__' + 'network-namespace'
            return True, [vm_obj]

        def vmi_create(vmi_obj):
            vmi_obj.uuid = 'fake-vmi-uuid'
            return
        def vmi_db_read(vmi_id):
            vmi_obj = {}
            vmi_obj['uuid'] = 'fake-vmi-uuid'
            vmi_obj['fq_name'] = ['fake-vmi-uuid']
            vmi_obj['parent_type'] = 'project'
            vmi_obj['parent_uuid'] = 'fake-project'
            return True, [vmi_obj]

        def iip_db_read(iip_id):
            iip_obj = {}
            iip_obj['uuid'] = 'fake-vmi-uuid'
            iip_obj['fq_name'] = ['fake-iip-uuid']
            return True, [iip_obj]

        def iip_create(iip_obj):
            iip_obj.uuid = 'fake-iip-uuid'
            return iip_obj.uuid

        VirtualMachineSM._cassandra = mock.MagicMock()
        VirtualMachineSM._cassandra._cassandra_virtual_machine_read = vm_read

        VirtualMachineInterfaceSM._cassandra = mock.MagicMock()
        VirtualMachineInterfaceSM._cassandra._cassandra_virtual_machine_interface_read = vmi_db_read

        InstanceIpSM._cassandra = mock.MagicMock()
        InstanceIpSM._cassandra._cassandra_instance_ip_read = iip_db_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = get_vn_id
        self.mocked_vnc.virtual_network_create = vn_create
        self.mocked_vnc.virtual_machine_interface_create = vmi_create
        self.mocked_vnc.instance_ip_create = iip_create

        self.nova_mock = mock.MagicMock()
        self.mocked_db = mock.MagicMock()

        self.mocked_args = mock.MagicMock()
        self.mocked_args.availability_zone = None

        self.vm_manager = VirtualMachineManager(
            db=self.mocked_db, logger=mock.MagicMock(),
            vnc_lib=self.mocked_vnc, vrouter_scheduler=mock.MagicMock(),
            nova_client=self.nova_mock, args=self.mocked_args)

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        VirtualMachineInterfaceSM.reset()
        VirtualMachineSM.reset()
        del VirtualMachineSM._cassandra

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

    def create_test_virtual_machine(self, fq_name_str):
        vm_obj = {}
        vm_obj['fq_name'] = fq_name_str.split(':')
        vm_obj['uuid'] = fq_name_str
        vm_obj['display_name'] = fq_name_str
        vm = VirtualMachineSM.locate(vm_obj['uuid'], vm_obj)
        vm.proj_fq_name = ['fake-domain', 'fake-project']
        return vm

    def test_virtual_machine_create(self):
        self.create_test_project('fake-domain:fake-project')
        self.create_test_virtual_network('fake-domain:fake-project:left-vn')
        self.create_test_virtual_network('fake-domain:fake-project:right-vn')

        st_obj = {}
        st_obj['fq_name'] = ['fake-domain', 'fake-template']
        st_obj['uuid'] = 'fake-st-uuid'
        st_obj['id_perms'] = 'fake-id-perms'
        st_props = {}
        st_props['flavor'] = 'm1.medium'
        st_props['image_name'] = 'nat-image'
        st_props['service_virtualization_type'] = 'virtual-machine'
        st_props['service_type'] = 'firewall'
        st_props['ordered_interfaces'] = True
        st_props['interface_type'] = [{'service_interface_type': 'management', 'shared_ip': False},
                                      {'service_interface_type': 'left', 'shared_ip': False},
                                      {'service_interface_type': 'right', 'shared_ip': False}]
        st_obj['service_template_properties'] = st_props

        si_obj = {}
        si_obj['fq_name'] = ['fake-domain', 'fake-project', 'fake-instance']
        si_obj['uuid'] = 'fake-si-uuid'
        si_obj['id_perms'] = 'fake-id-perms'
        si_props = {}
        si_props['scale_out'] = {'max_instances': 2}
        si_props['interface_list'] = [{'virtual_network': None},
                                      {'virtual_network': 'fake-domain:fake-project:left-vn'},
                                      {'virtual_network': 'fake-domain:fake-project:right-vn'}]
        si_obj['service_instance_properties'] = si_props

        st = ServiceTemplateSM.locate('fake-st-uuid', st_obj)
        si = ServiceInstanceSM.locate('fake-si-uuid', si_obj)

        def nova_oper(resource, oper, proj_name, **kwargs):
            if resource == 'servers' and oper == 'create':
                nova_vm = FakeNovaServer('fake-vm-uuid', kwargs['name'])
                return nova_vm
            else:
                return mock.MagicMock()
        self.nova_mock.oper = nova_oper

        self.vm_manager.create_service(st, si)
        self.mocked_vnc.virtual_machine_create.assert_any_call(VMObjMatcher(1))
        self.mocked_vnc.virtual_machine_create.assert_any_call(VMObjMatcher(2))

    def test_virtual_machine_delete(self):
        vm = self.create_test_virtual_machine('fake-vm-uuid')
        self.vm_manager.delete_service(vm)
