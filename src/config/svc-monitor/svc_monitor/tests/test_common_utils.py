from svc_monitor.config_db import *
from vnc_api.vnc_api import *

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

class VRObjMatcher(object):
    """
    Object for assert_called_with to check if vr object is created properly
    """
    def __init__(self, vm):
        self.vm = vm

    def _has_field(self, vm, ob):
        if vm == ob.get_virtual_machine_refs()[0]['to']:
            return True
        return False

    def __eq__(self, other):
        if not(self._has_field(self.vm, other)):
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

class AnyStringWith(str):
    def __eq__(self, other):
        return self in other

def create_test_st(name='fake-template', virt_type='virtual-machine', intf_list=[],
                   version='1'):
    st_obj = {}
    st_obj['fq_name'] = ['fake-domain', name]
    st_obj['uuid'] = name
    st_obj['version'] = version
    st_obj['id_perms'] = 'fake-id-perms'
    st_props = {}
    st_props['flavor'] = 'm1.medium'
    st_props['image_name'] = 'nat-image'
    st_props['service_virtualization_type'] = virt_type
    if virt_type == 'vrouter-instance':
        st_props['vrouter_instance_type'] = 'docker'
    st_props['service_type'] = 'firewall'
    st_props['service_mode'] = 'in-network'
    st_props['ordered_interfaces'] = True
    st_props['service_scaling'] = True
    st_props['interface_type'] = []
    for intf in intf_list:
        try:
            static_route_enable = intf[2]
        except IndexError:
            static_route_enable = False
        st_props['interface_type'].append({'service_interface_type': intf[0],
            'shared_ip': intf[1], 'static_route_enable': static_route_enable})
    st_obj['service_template_properties'] = st_props
    st = ServiceTemplateSM.locate(st_obj['uuid'], st_obj)
    return st

def create_test_si(name='fake-instance', count=1, vr_id=None, intf_list=None):
    si_obj = {}
    si_obj['fq_name'] = ['fake-domain', 'fake-project', name]
    si_obj['uuid'] = name
    si_obj['id_perms'] = 'fake-id-perms'
    si_props = {}
    si_props['scale_out'] = {'max_instances': count}
    si_props['interface_list'] = []
    for vn_name in intf_list:
        if not vn_name:
            vn_fq_name = vn_name
        else:
            vn_fq_name = ':'.join(si_obj['fq_name'][0:-1]) + ':' + vn_name
        si_props['interface_list'].append({'virtual_network': vn_fq_name})
    si_props['virtual_router_id'] = 'fake-vr-uuid'
    si_obj['service_instance_properties'] = si_props
    si_obj['parent_type'] = 'project'
    si = ServiceInstanceSM.locate(si_obj['uuid'], si_obj)
    return si

def create_test_project(fq_name_str):
    proj_obj = {}
    proj_obj['fq_name'] = fq_name_str.split(':')
    proj_obj['uuid'] = fq_name_str
    proj_obj['id_perms'] = 'fake-id-perms'
    ProjectSM.locate(proj_obj['uuid'], proj_obj)

def create_test_virtual_network(fq_name_str):
    vn_obj = {}
    vn_obj['fq_name'] = fq_name_str.split(':')
    vn_obj['uuid'] = fq_name_str
    vn_obj['id_perms'] = 'fake-id-perms'
    vn_obj['parent_type'] = 'project'
    VirtualNetworkSM.locate(vn_obj['uuid'], vn_obj)

def create_test_virtual_machine(fq_name_str):
    vm_obj = {}
    vm_obj['fq_name'] = fq_name_str.split(':')
    vm_obj['uuid'] = fq_name_str
    vm_obj['display_name'] = fq_name_str
    vm = VirtualMachineSM.locate(vm_obj['uuid'], vm_obj)
    vm.proj_fq_name = ['fake-domain', 'fake-project']
    return vm

def create_test_virtual_router(fq_name_str):
    vr_obj = {}
    vr_obj['fq_name'] = fq_name_str.split(':')
    vr_obj['name'] = fq_name_str.split(':')[0]
    vr_obj['uuid'] = fq_name_str
    vr_obj['display_name'] = fq_name_str
    vr = VirtualRouterSM.locate(vr_obj['uuid'], vr_obj)
    vr.agent_state = True
    return vr

def create_test_port_tuple(fq_name_str, parent_uuid):
    pt_obj = {}
    pt_obj['fq_name'] = fq_name_str.split(':')
    pt_obj['name'] = fq_name_str.split(':')[-1]
    pt_obj['uuid'] = fq_name_str.split(':')[-1]
    pt_obj['display_name'] = fq_name_str
    pt_obj['parent_type'] = 'service-instance'
    pt_obj['parent_uuid'] = parent_uuid
    pt = PortTupleSM.locate(pt_obj['uuid'], pt_obj)
    return pt

def delete_test_port_tuple(pt):
    PortTupleSM.delete(pt.uuid)
    return

def create_test_vmi(fq_name_str, pt=None):
    vmi_obj = {}
    vmi_obj['fq_name'] = fq_name_str.split(':')
    vmi_obj['name'] = fq_name_str.split(':')[-1]
    vmi_obj['uuid'] = fq_name_str.split(':')[-1]
    vmi_obj['display_name'] = fq_name_str
    vmi_obj['parent_type'] = 'project'
    vmi = VirtualMachineInterfaceSM.locate(vmi_obj['uuid'], vmi_obj)
    if pt:
        pt.virtual_machine_interfaces.add(vmi.uuid)
        vmi.port_tuple = pt.uuid
    return vmi

def delete_test_vmi(vmi):
    VirtualMachineInterfaceSM.delete(vmi.uuid)
    return

def create_test_security_group(fq_name_str):
    sg_obj = {}
    sg_obj['fq_name'] = fq_name_str.split(':')
    sg_obj['uuid'] = fq_name_str
    sg_obj['id_perms'] = 'fake-id-perms'
    sg_obj['parent_type'] = 'project'
    SecurityGroupSM.locate(sg_obj['uuid'], sg_obj)

def create_test_service_health_check(fq_name_str,
                                     parent,
                                     health_check_type = 'end-to-end'):
    """
    Create, store and return ServiceHealthCheck object.

    Creation of the health check object involves:
    * Construction of ServiceHealthCheck template object.
    * Invoke ServiceHealthCheckSM DB to instatiate and add object.

    """

    # Construct template service health check object.
    shc_obj = {}
    shc_obj['uuid'] = fq_name_str.split(':')[-1]
    shc_obj['fq_name'] = fq_name_str.split(':')
    shc_obj['parent_uuid'] = parent

    # Instantiate and store service health check object.
    shc = ServiceHealthCheckSM.locate(shc_obj['uuid'], shc_obj)

    # Update/init params of interest.
    shc.params = {}
    shc.params['health_check_type'] = health_check_type

    return shc

def create_test_iip(fq_name_str, iip_uuid='fake-iip-uuid'):
    """
    Create and return iip object.

    """

    iip_obj = {}
    iip_obj['uuid'] = iip_uuid
    iip_obj['fq_name'] = fq_name_str.split(':')

    iip = InstanceIpSM.locate(iip_obj['uuid'], iip_obj)

    return iip

def get_vn_id_for_fq_name(obj_type, fq_name):
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
    vn['parent_type'] = 'project'
    VirtualNetworkSM.locate(vn_obj.uuid, vn)
    return vn_obj.uuid

def vmi_create(vmi_obj):
    vmi_obj.uuid = 'fake-vmi-uuid'
    return vmi_obj.uuid

def iip_create(iip_obj):
    iip_obj.uuid = 'fake-iip-uuid'
    return iip_obj.uuid

def vm_create(vm_obj):
    vm_obj.uuid = (':').join(vm_obj.fq_name)
    vm = {}
    vm['uuid'] = vm_obj.uuid
    vm['fq_name'] = vm_obj.fq_name
    vm['display_name'] = vm_obj.display_name
    VirtualMachineSM.locate(vm_obj.uuid, vm)
    si = ServiceInstanceSM.get(vm_obj.get_service_instance_refs()[0]['uuid'])
    if si:
        si.virtual_machines.add(vm_obj.uuid)
        vm_db = VirtualMachineSM.locate(vm_obj.uuid)
        vm_db.index = int(vm_obj.display_name.split('__')[-2]) - 1
    return vm_obj.uuid

def st_create(st_obj):
    st_obj.uuid = st_obj.name
    st = {}
    st['uuid'] = st_obj.uuid
    st['fq_name'] = st_obj.fq_name
    ServiceTemplateSM.locate(st_obj.uuid, st)
    return st_obj.uuid

def test_get_instance_name(si, inst_count):
    name = si.name + '__' + str(inst_count + 1)
    instance_name = "__".join(si.fq_name[:-1] + [name])
    return instance_name

def vm_db_read(obj_type, vm_id, **kwargs):
    class SI(object):
        def __init__(self, name, fq_name):
            self.name = name
            self.fq_name = fq_name

    vm_obj = {}
    vm_obj['uuid'] = 'fake-vm-uuid'
    vm_obj['fq_name'] = ['fake-vm-uuid']
    fq_name = ['fake-domain', 'fake-project', 'fake-instance']
    name = 'fake-instance'
    si = SI(name, fq_name)
    instance_name = test_get_instance_name(si, 0)
    vm_obj['display_name'] = instance_name + '__' + 'vrouter-instance'
    return True, [vm_obj]

def vr_db_read(obj_type, vr_id, **kwargs):
    vr_obj = {}
    vr_obj['uuid'] = 'fake-vr-uuid'
    vr_obj['fq_name'] = ['fake-vr-uuid']
    return True, [vr_obj]

def vmi_db_read(obj_type, vmi_id, **kwargs):
    vmi_obj = {}
    vmi_obj['uuid'] = vmi_id
    vmi_obj['fq_name'] = ['fake-vmi-uuid']
    vmi_obj['parent_type'] = 'project'
    vmi_obj['parent_uuid'] = 'fake-project'
    return True, [vmi_obj]

def iip_db_read(obj_type, iip_id, **kwargs):
    iip_obj = {}
    iip_obj['uuid'] = iip_id
    iip_obj['fq_name'] = ['fake-iip-uuid']
    return True, [iip_obj]

def si_db_read(obj_type, si_id, **kwargs):
    name = si_id[0]
    si = ServiceInstanceSM.get(name)
    si_obj = {}
    si_obj['fq_name'] = ['fake-domain', 'fake-project', name]
    si_obj['uuid'] = name
    si_obj['id_perms'] = 'fake-id-perms'
    si_props = {}
    si_props['scale_out'] = {'max_instances': si.params['scale_out']['max_instances']}
    si_props['interface_list'] = []
    for vn_fq_name in si.params['interface_list']:
        si_props['interface_list'].append({'virtual_network': vn_fq_name['virtual_network']})
    si_props['virtual_router_id'] = 'fake-vr-uuid'
    si_obj['service_instance_properties'] = si_props
    si_obj['parent_type'] = 'project'
    return True, [si_obj]

def vn_db_read(obj_type, vn_id, **kwargs):
    vn_obj = {}
    vn_obj['uuid'] = 'fake-vn-uuid'
    vn_obj['fq_name'] = ['fake-domain', 'fake-project', 'fake-vn-uuid']
    return True, [vn_obj]

def irt_db_read(obj_type, irt_id, **kwargs):
    irt_obj = {}
    irt_obj['uuid'] = 'fake-irt-uuid'
    irt_obj['fq_name'] = ['fake-domain', 'fake-project', 'fake-irt-uuid']
    return True, [irt_obj]
