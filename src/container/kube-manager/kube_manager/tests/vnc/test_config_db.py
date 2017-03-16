#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from kube_manager.vnc import config_db
from vnc_api.vnc_api import *

class ConfigDBTest(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.object_db = mock.Mock()
        self.logger = mock.Mock()
        self.svc = mock.Mock()
        DBBase.init(self.svc, None, self.object_db)
    # end setUp

    def tearDown(self):
        del self.object_db
    # end tearDown

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json(obj.get_pending_updates())
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))
    # end obj_to_dict

    def add_project(self, name, uuid):
        project = Project(name=name)
        proj_dict = self.obj_to_dict(project)
        proj_dict['uuid'] = 'project'
        proj_obj = Project.from_dict(**proj_dict)
        config_db.ProjectKM._object_db.object_read = mock.Mock(return_value=(True, [proj_dict]))
        config_db.ProjectKM.locate(uuid)
        return proj_obj
    # end add_project

    def add_domain(self, name, uuid):
        dom = Domain(name)
        dom_dict = self.obj_to_dict(dom)
        config_db.DomainKM._object_db.object_read = mock.Mock(return_value=(True, [dom_dict]))
        config_db.DomainKM.locate(uuid)
    # end

    def add_vn(self, name, uuid, parent_obj):
        network = VirtualNetwork(name=name, parent_obj=parent_obj)
        net_dict = self.obj_to_dict(network)
        net_dict['parent_uuid'] = parent_obj.uuid
        net_dict['uuid'] = uuid
        net_obj = VirtualNetwork.from_dict(**net_dict)
        config_db.VirtualNetworkKM._object_db.object_read = mock.Mock(return_value=(True, [net_dict]))
        config_db.VirtualNetworkKM.locate(uuid)
        return net_obj
    # end add_vn

    def add_vmi(self, name, uuid, parent_obj, net_obj, vm_obj=None, irt_obj=None):
        vmi = VirtualMachineInterface(name=name, parent_obj=parent_obj)
        vmi.set_virtual_network(net_obj)
        if vm_obj:
            vmi.set_virtual_machine(vm_obj)

        if irt_obj:
            vmi.add_interface_route_table(irt_obj)
            vmi._pending_field_updates.add('interface_route_table_refs')
        vmi_dict = self.obj_to_dict(vmi)
        vmi_dict['parent_uuid'] = parent_obj.uuid
        vmi_dict['uuid'] = uuid
        vmi_obj = VirtualMachineInterface.from_dict(**vmi_dict)
        config_db.VirtualMachineInterfaceKM._object_db.object_read = mock.Mock(return_value=(True, [vmi_dict]))
        config_db.VirtualMachineInterfaceKM.locate(uuid)
        return vmi_obj
    # end add_vmi

    def add_vm(self, name, uuid):
        vm = VirtualMachine(name=name)
        vm_dict = self.obj_to_dict(vm)
        vm_dict['uuid'] = uuid
        vm_obj = VirtualMachine.from_dict(**vm_dict)
        config_db.VirtualMachineKM._object_db.object_read = mock.Mock(return_value=(True, [vm_dict]))
        config_db.VirtualMachineKM.locate(uuid)
        return vm_obj
    # end add_vm

    def add_lb(self, name, uuid, proj_obj, vmi_obj, vip_address, annotations=None):
        lb = Loadbalancer(name=name, parent_obj=proj_obj,
                          loadbalancer_provider='native')
        lb.uuid = uuid
        lb.set_virtual_machine_interface(vmi_obj)
        id_perms = IdPermsType(enable=True)
        props = LoadbalancerType(provisioning_status='ACTIVE', id_perms=id_perms,
                      operating_status='ONLINE', vip_address=vip_address)
        lb.set_loadbalancer_properties(props)
        if annotations:
            for key in annotations:
                lb.add_annotations(KeyValuePair(key=key, value=annotations[key]))

        lb_dict = self.obj_to_dict(lb)
        lb_dict['parent_uuid'] = proj_obj.uuid
        lb_dict['uuid'] = uuid
        lb_obj = Loadbalancer.from_dict(**lb_dict)
        config_db.LoadbalancerKM._object_db.object_read = mock.Mock(return_value=(True, [lb_dict]))
        config_db.LoadbalancerKM.locate(uuid)
        return lb_obj
    # end add_lb

    def add_lb_listener(self, name, uuid, lb_obj, proj_obj, protocol, port, 
                        target_port):
        id_perms = IdPermsType(enable=True)
        ll = LoadbalancerListener(name, proj_obj, id_perms=id_perms,
                                  display_name=name)
        ll.uuid = uuid
        if lb_obj:
            ll.set_loadbalancer(lb_obj)

        props = LoadbalancerListenerType()
        props.set_protocol(protocol)
        props.set_protocol_port(port)

        ll.set_loadbalancer_listener_properties(props)
        if target_port:
            ll.add_annotations(KeyValuePair(key='targetPort', value=str(target_port)))

        ll_dict = self.obj_to_dict(ll)
        ll_dict['uuid'] = uuid
        ll_dict['parent_uuid'] = proj_obj.uuid
        ll_obj = LoadbalancerListener.from_dict(**ll_dict)
        config_db.LoadbalancerListenerKM._object_db.object_read = mock.Mock(return_value=(True, [ll_dict]))
        config_db.LoadbalancerListenerKM.locate(uuid)
        return ll_obj
    # end add_lb_listener

    def add_lb_pool(self, name, uuid, ll_obj, proj_obj): 
        props = LoadbalancerPoolType()
        props.set_protocol("TCP")
        id_perms = IdPermsType(enable=True)
        pool = LoadbalancerPool(name, proj_obj, uuid=uuid,
                                loadbalancer_pool_properties=props,
                                id_perms=id_perms)
        pool.uuid = uuid
        pool.set_loadbalancer_listener(ll_obj)
        pool_dict = self.obj_to_dict(pool)
        pool_dict['uuid'] = uuid
        pool_dict['parent_uuid'] = proj_obj.uuid
        pool_obj = LoadbalancerListener.from_dict(**pool_dict)
        config_db.LoadbalancerPoolKM._object_db.object_read = mock.Mock(return_value=(True, [pool_dict]))
        config_db.LoadbalancerPoolKM.locate(uuid)
        return pool_obj
    # end add_lb_listener

    def add_lb_member(self, name, uuid, pool_obj, address="10.20.30.41", 
                      port=3000, annotations=None):
        props = LoadbalancerMemberType(address=address, protocol_port=port)
        id_perms = IdPermsType(enable=True)

        member = LoadbalancerMember(
            name, pool_obj, loadbalancer_member_properties=props,
            id_perms=id_perms)
        member.uuid = uuid
        if annotations:
            for key in annotations:
                member.add_annotations(KeyValuePair(key=key, value=annotations[key]))
        member_dict = self.obj_to_dict(member)
        member_dict['uuid'] = uuid
        member_dict['parent_uuid'] = pool_obj.uuid
        member_obj = LoadbalancerMember.from_dict(**member_dict)
        config_db.LoadbalancerMemberKM._object_db.object_read = mock.Mock(return_value=(True, [member_dict]))
        config_db.LoadbalancerMemberKM.locate(uuid)
    # end add_lb_member

    def test_add_delete_network(self):
        proj_obj = self.add_project("Test", 'project')
        self.add_vn("Test-VN", 'network', proj_obj)
        proj = config_db.ProjectKM.get('project')
        net = config_db.VirtualNetworkKM.get('network')
        self.assertIsNotNone(net)
        self.assertIsNotNone(proj)
        self.assertEqual(net.parent_key, 'project')
        config_db.VirtualNetworkKM.delete('network')
        config_db.ProjectKM.delete('project')
    # end test_add_delete_network

    def test_add_delete_vmi(self):
        proj_obj = self.add_project("Test", 'project')
        net_obj = self.add_vn("Test-VN", 'network', proj_obj)
        self.add_vmi("Test-VMI", 'vmi', proj_obj, net_obj)

        proj = config_db.ProjectKM.get('project')
        net = config_db.VirtualNetworkKM.get('network')
        vmi = config_db.VirtualMachineInterfaceKM.get('vmi')

        self.assertIsNotNone(net)
        self.assertIsNotNone(vmi)
        self.assertIsNotNone(proj)

        self.assertEqual(len(net.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in net.virtual_machine_interfaces)
        self.assertEqual(vmi.virtual_network, 'network')

        config_db.VirtualMachineInterfaceKM.delete('vmi')
        config_db.VirtualNetworkKM.delete('network')
        config_db.ProjectKM.delete('project')
    # end test_add_delete_network

    def test_add_delete_iip(self):
        proj_obj = self.add_project("Test", 'project')
        net_obj = self.add_vn("Test-VN", 'network', proj_obj)
        vmi_obj = self.add_vmi("Test-VMI", 'vmi', proj_obj, net_obj)

        iip = InstanceIp(name="Test", instance_ip_address="1.1.1.2", instance_ip_family="v4")
        iip.set_virtual_network(net_obj)
        iip.set_virtual_machine_interface(vmi_obj)
        iip_dict = self.obj_to_dict(iip)
        iip_dict['uuid'] = 'iip'
        iip_obj = InstanceIp.from_dict(**iip_dict)
        config_db.InstanceIpKM._object_db.object_read = mock.Mock(return_value=(True, [iip_dict]))
        config_db.InstanceIpKM.locate('iip')

        vmi = config_db.VirtualMachineInterfaceKM.get('vmi')
        iip = config_db.InstanceIpKM.get('iip')

        self.assertIsNotNone(vmi)
        self.assertIsNotNone(iip)
        self.assertEqual(len(iip.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in iip.virtual_machine_interfaces)
        self.assertEqual(list(vmi.instance_ips)[0], 'iip')

        config_db.InstanceIpKM.delete('iip')
        config_db.VirtualMachineInterfaceKM.delete('vmi')
        config_db.VirtualNetworkKM.delete('network')
        config_db.ProjectKM.delete('project')
    # end test_add_delete_network

    def test_add_delete_loadbalancer(self):
        proj_obj = self.add_project("kube-system", 'project')
        net_obj = self.add_vn("cluster-network", 'network', proj_obj)
        vmi_obj = self.add_vmi("kubernetes-lb-vmi", 'vmi', proj_obj, net_obj)
        #LB
        annotations1 = {}
        annotations1['owner'] = 'k8s'
        lb_obj = self.add_lb("kubernetes", 'loadbalancer', proj_obj, vmi_obj, "10.20.30.40", annotations1)
        ll_obj = self.add_lb_listener("kubernetes-ll", 'listener', lb_obj, proj_obj, "TCP", 80, 3000)
        pool_obj = self.add_lb_pool("kubernetes-pool", 'pool', ll_obj, proj_obj)
        annotations2 = {}
        annotations2['vmi'] = "vmi"
        annotations2['vm'] = "vm"
        member_obj = self.add_lb_member("kubernetes-member", 'member', pool_obj, annotations=annotations2)

        net = config_db.VirtualNetworkKM.get('network')
        vmi = config_db.VirtualMachineInterfaceKM.get('vmi')
        lb = config_db.LoadbalancerKM.get('loadbalancer')
        ll = config_db.LoadbalancerListenerKM.get('listener')
        pool = config_db.LoadbalancerPoolKM.get('pool')
        member = config_db.LoadbalancerMemberKM.get('member')

        self.assertIsNotNone(member)
        self.assertIsNotNone(pool)
        self.assertIsNotNone(ll)
        self.assertIsNotNone(lb)
        self.assertIsNotNone(vmi)
        self.assertIsNotNone(net)

        #Test 'member'
        self.assertEqual(member.uuid, 'member')
        self.assertEqual(member.name, 'kubernetes-member')
        self.assertEqual(member.loadbalancer_pool, 'pool')
        # SAS FIXME self.assertEqual(member.vmi, 'vmi')
        # SAS FIXME self.assertEqual(member.vm, 'vm')

        #Test 'pool'
        self.assertEqual(pool.uuid, 'pool')
        self.assertEqual(pool.name, 'kubernetes-pool')
        self.assertEqual(len(pool.members), 1)
        self.assertEqual(pool.loadbalancer_listener, 'listener')
        self.assertEqual(pool.parent_uuid, 'project')

        #Test 'listener'
        self.assertEqual(ll.uuid, 'listener')
        self.assertEqual(ll.name, 'kubernetes-ll')
        self.assertEqual(ll.loadbalancer, 'loadbalancer')
        self.assertEqual(ll.loadbalancer_pool, 'pool')
        #self.assertEqual(ll.target_port, '3000')
        self.assertEqual(pool.parent_uuid, 'project')

        #Test 'loadbalancer'
        self.assertEqual(lb.uuid, 'loadbalancer')
        self.assertEqual(lb.name, 'kubernetes')
        self.assertEqual(len(lb.loadbalancer_listeners), 1)
        self.assertEqual(lb.parent_uuid, 'project')

        # Delete memeber and check update of pool
        config_db.LoadbalancerMemberKM.delete('member')
        self.assertEqual(len(pool.members), 0)

        # Delete pool and check update of listener
        config_db.LoadbalancerPoolKM.delete('pool')
        self.assertNotEqual(ll.loadbalancer_pool, 'pool')

        # Delete listener and check update of loadbalancer
        config_db.LoadbalancerListenerKM.delete('listener')
        self.assertEqual(len(lb.loadbalancer_listeners), 0)

        # Delete loadbalancer
        config_db.LoadbalancerKM.delete('loadbalancer')

        # Delete project
        config_db.ProjectKM.delete('project')
    # end test_add_delete_pool

    def test_add_delete_floating_ip(self):
        project = self.add_project("Test-Project", 'project')
        net_obj = self.add_vn("Test-Network", 'network', project)
        vmi_obj = self.add_vmi("Test-VMI", 'vmi', project, net_obj)

        fip_pool_obj = FloatingIpPool('Test-fip-pool', parent_obj=net_obj)
        fip = FloatingIp(name="Test-FIP", parent_obj=fip_pool_obj, floating_ip_address = "1.2.3.33")
        fip.set_project(project)
        fip.set_virtual_machine_interface(vmi_obj)
        fip_dict = self.obj_to_dict(fip)
        fip_dict['parent_uuid'] = 'pool'
        fip_dict['uuid'] = 'fip'
        fip_obj = FloatingIp.from_dict(**fip_dict)
        config_db.FloatingIpKM._object_db.object_read = mock.Mock(return_value=(True, [fip_dict]))
        config_db.FloatingIpKM.locate('fip')

        fip = config_db.FloatingIpKM.get('fip')
        vmi = config_db.VirtualMachineInterfaceKM.get('vmi')

        self.assertIsNotNone(fip)
        self.assertIsNotNone(vmi)

        self.assertEqual(len(fip.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in fip.virtual_machine_interfaces)
        self.assertEqual(fip.address, "1.2.3.33")
        self.assertEqual(list(vmi.floating_ips)[0], 'fip')

        config_db.FloatingIpKM.delete('fip')
        config_db.VirtualMachineInterfaceKM.delete('vmi')
        config_db.VirtualNetworkKM.delete('network')
        config_db.ProjectKM.delete('project')
    # end test_add_delete_floating_ip

    def test_add_delete_sg(self):
        project = self.add_project("Test-Project", 'project')
        sg = SecurityGroup(name="Test-SG", parent_obj=project)
        sg_dict = self.obj_to_dict(sg)
        sg_dict['parent_uuid'] = 'project'
        sg_dict['uuid'] = 'sg'
        sg_obj = SecurityGroup.from_dict(**sg_dict)
        self.object_db.object_read = mock.Mock(
            return_value=(True, [sg_dict]))
        config_db.SecurityGroupKM.locate('sg')

        sg = config_db.SecurityGroupKM.get('sg')
        self.assertIsNotNone(sg)

        config_db.SecurityGroupKM.delete('sg')
        config_db.ProjectKM.delete('project')
    # end test_add_delete_sg(self):

#end ConfigDBTest(unittest.TestCase):
