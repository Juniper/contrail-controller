import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from vnc_api.vnc_api import *

class ConfigDBTest(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.cassandra = mock.Mock()
        self.logger = mock.Mock()
        self.svc = mock.Mock()
        DBBase.init(self.svc, None, self.cassandra)
    # end setUp

    def tearDown(self):
        del self.cassandra
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
        config_db.ProjectSM._cassandra.object_read = mock.Mock(return_value=(True, [proj_dict]))
        config_db.ProjectSM.locate(uuid)
        return proj_obj
    # end add_project

    def add_domain(self, name, uuid):
        dom = Domain(name)
        dom_dict = self.obj_to_dict(dom)
        config_db.DomainSM._cassandra.object_read = mock.Mock(return_value=(True, [dom_dict]))
        config_db.DomainSM.locate(uuid)
    # end

    def add_vn(self, name, uuid, parent_obj):
        network = VirtualNetwork(name=name, parent_obj=parent_obj)
        net_dict = self.obj_to_dict(network)
        net_dict['parent_uuid'] = parent_obj.uuid
        net_dict['uuid'] = uuid
        net_obj = VirtualNetwork.from_dict(**net_dict)
        config_db.VirtualNetworkSM._cassandra.object_read = mock.Mock(return_value=(True, [net_dict]))
        config_db.VirtualNetworkSM.locate(uuid)
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
        config_db.VirtualMachineInterfaceSM._cassandra.object_read = mock.Mock(return_value=(True, [vmi_dict]))
        config_db.VirtualMachineInterfaceSM.locate(uuid)
        return vmi_obj
    # end add_vmi

    def add_vm(self, name, uuid):
        vm = VirtualMachine(name=name)
        vm_dict = self.obj_to_dict(vm)
        vm_dict['uuid'] = uuid
        vm_obj = VirtualMachine.from_dict(**vm_dict)
        config_db.VirtualMachineSM._cassandra.object_read = mock.Mock(return_value=(True, [vm_dict]))
        config_db.VirtualMachineSM.locate(uuid)
        return vm_obj
    # end add_vm

    def add_vr(self, name, uuid, vm_obj):
        vr = VirtualRouter(name=name)
        vr.add_virtual_machine(vm_obj)
        vr._pending_field_updates.add('virtual_machine_refs')
        vr_dict = self.obj_to_dict(vr)
        vr_dict['uuid'] = uuid
        vr_obj = VirtualRouter.from_dict(**vr_dict)
        config_db.VirtualRouterSM._cassandra.object_read = mock.Mock(return_value=(True, [vr_dict]))
        config_db.VirtualRouterSM.locate(uuid)
    # end add_vr

    def add_sas(self, name, uuid):
        sas_obj = ServiceApplianceSet(name)
        sas_obj.set_service_appliance_driver("Test.Driver.Name")
        kvp_array = []
        kvp = KeyValuePair("Key1","Value1")
        kvp_array.append(kvp)
        kvp = KeyValuePair("Key2","Value2")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sas_obj.set_service_appliance_set_properties(kvps)
        sas_dict = self.obj_to_dict(sas_obj)
        sas_dict['uuid'] = uuid
        sas_obj = ServiceApplianceSet.from_dict(**sas_dict)
        config_db.ServiceApplianceSetSM._cassandra.object_read= mock.Mock(
            return_value=(True, [sas_dict]))
        config_db.ServiceApplianceSetSM.locate(uuid)
        return sas_obj
    # end add_sas

    def add_st(self, name, uuid):
        st = ServiceTemplate(name=name)
        st_dict = self.obj_to_dict(st)
        st_dict['uuid'] = uuid
        st_obj = ServiceTemplate.from_dict(**st_dict)
        config_db.ServiceTemplateSM._cassandra.object_read = mock.Mock(return_value=(True, [st_dict]))
        config_db.ServiceTemplateSM.locate(uuid)
        return st_obj
    # end add_st

    def add_si(self, name, uuid, st):
        si = ServiceInstance(name=name)
        si.set_service_template(st)
        si_dict = self.obj_to_dict(si)
        si_dict['uuid'] = uuid
        si_obj = ServiceInstance.from_dict(**si_dict)
        config_db.ServiceInstanceSM._cassandra.object_read = mock.Mock(return_value=(True, [si_dict]))
        config_db.ServiceInstanceSM.locate(uuid)
        return si_obj
    # end add_si

    def test_add_delete_project(self):
        self.add_domain("Test-Dom", 'domain')
        self.add_project("Test-Proj", 'project')
        self.assertIsNotNone(config_db.DomainSM.get('domain'))
        self.assertIsNotNone(config_db.ProjectSM.get('project'))
        config_db.ProjectSM.delete('project')
        config_db.DomainSM.delete('domain')
    # end test_add_delete_project

    def test_add_delete_network(self):
        proj_obj = self.add_project("Test", 'project')
        self.add_vn("Test-VN", 'network', proj_obj)
        proj = config_db.ProjectSM.get('project')
        net = config_db.VirtualNetworkSM.get('network')
        self.assertIsNotNone(net)
        self.assertIsNotNone(proj)
        self.assertEqual(len(proj.virtual_networks), 1)
        self.assertTrue('network' in proj.virtual_networks)
        self.assertEqual(net.parent_key, 'project')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_network

    def test_add_delete_vmi(self):
        proj_obj = self.add_project("Test", 'project')
        net_obj = self.add_vn("Test-VN", 'network', proj_obj)
        self.add_vmi("Test-VMI", 'vmi', proj_obj, net_obj)

        proj = config_db.ProjectSM.get('project')
        net = config_db.VirtualNetworkSM.get('network')
        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')

        self.assertIsNotNone(net)
        self.assertIsNotNone(vmi)
        self.assertIsNotNone(proj)

        self.assertEqual(len(net.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in net.virtual_machine_interfaces)
        self.assertEqual(len(proj.virtual_networks), 1)
        self.assertTrue('network' in proj.virtual_networks)
        self.assertEqual(net.parent_key, 'project')
        self.assertEqual(vmi.virtual_network, 'network')

        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ProjectSM.delete('project')
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
        config_db.InstanceIpSM._cassandra.object_read = mock.Mock(return_value=(True, [iip_dict]))
        config_db.InstanceIpSM.locate('iip')

        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')
        iip = config_db.InstanceIpSM.get('iip')

        self.assertIsNotNone(vmi)
        self.assertIsNotNone(iip)
        self.assertEqual(len(iip.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in iip.virtual_machine_interfaces)
        self.assertEqual(list(vmi.instance_ips)[0], 'iip')

        config_db.InstanceIpSM.delete('iip')
        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_network

    def test_add_delete_sas_sa(self):
        sas_obj = self.add_sas("Test-SAS", 'sas')
        sa_obj = ServiceAppliance("Test-sa", sas_obj)
        sa_obj.set_service_appliance_ip_address("1.2.3.4")
        uci = UserCredentials("James", "Bond")
        sa_obj.set_service_appliance_user_credentials(uci)
        kvp_array = []
        kvp = KeyValuePair("SA-Key1","SA-Value1")
        kvp_array.append(kvp)
        kvp = KeyValuePair("SA-Key2","SA-Value2")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sa_obj.set_service_appliance_properties(kvps)
        sa_dict = self.obj_to_dict(sa_obj)
        sa_dict['uuid'] = 'sa'
        sa_dict['parent_uuid'] = 'sas'
        config_db.ServiceApplianceSM._cassandra.object_read = mock.Mock(return_value=(True, [sa_dict]))
        config_db.ServiceApplianceSM.locate('sa')

        sas = config_db.ServiceApplianceSetSM.get('sas')
        sa = config_db.ServiceApplianceSM.get('sa')

        self.assertIsNotNone(sa)
        self.assertIsNotNone(sas)

        # TODO add field validation

        config_db.ServiceApplianceSM.delete('sa')
        config_db.ServiceApplianceSetSM.delete('sas')
    # end test_add_delete_sas_sa

    def test_add_delete_pool(self):
        proj_obj = self.add_project("Test-Project", 'project')
        sas_obj = self.add_sas("Test-SAS", 'sas')

        hm_obj = LoadbalancerHealthmonitor("test-hm", proj_obj)
        hm_dict = self.obj_to_dict(hm_obj)
        hm_dict['uuid'] = 'hm'
        hm_dict['parent_uuid'] = 'project'
        hm_obj = LoadbalancerHealthmonitor.from_dict(**hm_dict)
        config_db.HealthMonitorSM._cassandra.object_read = mock.Mock(return_value=(True, [hm_dict]))
        config_db.HealthMonitorSM.locate("hm")

        pool_obj = LoadbalancerPool("Test-Pool", proj_obj)
        pool_obj.set_service_appliance_set(sas_obj)
        pool_obj.add_loadbalancer_healthmonitor(hm_obj)
        pool_obj._pending_field_updates.add('loadbalancer_healthmonitor_refs')
        pool_dict = self.obj_to_dict(pool_obj)
        pool_dict['uuid'] = 'pool'
        pool_dict['parent_uuid'] = 'project'
        pool_obj = LoadbalancerPool.from_dict(**pool_dict)
        config_db.LoadbalancerPoolSM._cassandra.object_read = mock.Mock(return_value=(True, [pool_dict]))
        config_db.LoadbalancerPoolSM.locate('pool')

        member_obj = LoadbalancerMember("member-0" , pool_obj)
        member_dict = self.obj_to_dict(pool_obj)
        member_dict['uuid'] = 'member'
        member_dict['parent_uuid'] = 'pool'
        member_obj = LoadbalancerMember.from_dict(**member_dict)
        config_db.LoadbalancerMemberSM._cassandra.object_read = mock.Mock(return_value=(True, [member_dict]))
        config_db.LoadbalancerMemberSM.locate('member')

        vip_obj = VirtualIp("Test-vip", proj_obj)
        vip_obj.set_loadbalancer_pool(pool_obj)
        vip_dict = self.obj_to_dict(vip_obj)
        vip_dict['uuid'] = 'vip'
        vip_dict['parent_uuid'] = 'project'
        vip_obj = VirtualIp.from_dict(**vip_dict)
        config_db.VirtualIpSM._cassandra.object_read = mock.Mock(return_value=(True, [vip_dict]))
        config_db.VirtualIpSM.locate('vip')

        hm = config_db.HealthMonitorSM.get('hm')
        member = config_db.LoadbalancerMemberSM.get('member')
        pool = config_db.LoadbalancerPoolSM.get('pool')
        vip = config_db.VirtualIpSM.get('vip')

        self.assertIsNotNone(hm)
        self.assertIsNotNone(pool)
        self.assertIsNotNone(member)
        self.assertIsNotNone(vip)
        self.assertEqual(len(pool.members), 1)
        self.assertEqual(len(pool.loadbalancer_healthmonitors), 1)
        self.assertEqual(pool.virtual_ip, 'vip')
        self.assertEqual(pool.parent_uuid, 'project')
        self.assertEqual(member.loadbalancer_pool, 'pool')
        self.assertEqual(vip.loadbalancer_pool, 'pool')
        self.assertEqual(vip.parent_uuid, 'project')
        self.assertEqual(len(hm.loadbalancer_pools), 1)
        self.assertEqual(hm.parent_uuid, 'project')
        # TODO add tests for params

        config_db.HealthMonitorSM.delete('hm')
        config_db.LoadbalancerMemberSM.delete('member')
        config_db.VirtualIpSM.delete('vip')
        config_db.LoadbalancerPoolSM.delete('pool')
        config_db.ServiceApplianceSetSM.delete('sas')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_pool

    def test_add_delete_vrouter_vm_vmi(self):
        project = self.add_project("Test-Project", 'project')
        net_obj = self.add_vn("Test-VN", 'network', project)
        vm_obj = self.add_vm("Test-VM", 'vm')
        vr_obj = self.add_vr('Test-VR', 'vr', vm_obj)
        vmi_obj = self.add_vmi('Test-VMI', 'vmi', project, net_obj, vm_obj)

        vr = config_db.VirtualRouterSM.get('vr')
        self.assertIsNotNone(vr)
        self.assertEqual(len(vr.virtual_machines), 1)
        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')
        self.assertIsNotNone(vmi)
        self.assertEqual(vmi.virtual_machine, 'vm')
        vm = config_db.VirtualMachineSM.get('vm')
        self.assertIsNotNone(vm)
        self.assertEqual(len(vm.virtual_machine_interfaces), 1)
        self.assertEqual(vm.virtual_router, 'vr')
        # TODO test with service instance

        config_db.VirtualMachineSM.delete('vm')
        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.VirtualRouterSM.delete('vr')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_vrouter_vm_vmi

    def test_add_delete_st_si(self):
        project = self.add_project("Test-Project", 'project')
        vm_obj = self.add_vm('Test-VM', 'vm')
        vr_obj = self.add_vr('Test-VR', 'vr', vm_obj)
        st_obj = self.add_st('Test-ST', 'st')
        si_obj = self.add_si('Test-SI', 'si', st_obj)

        si = config_db.ServiceInstanceSM.get('si')
        self.assertIsNotNone(si)
        self.assertEqual(si.service_template, 'st')

        config_db.VirtualMachineSM.delete('vm')
        config_db.VirtualRouterSM.delete('vr')
        config_db.ServiceTemplateSM.delete('st')
        config_db.ServiceInstanceSM.delete('si')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_st_si

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
        config_db.FloatingIpSM._cassandra.object_read = mock.Mock(return_value=(True, [fip_dict]))
        config_db.FloatingIpSM.locate('fip')

        fip = config_db.FloatingIpSM.get('fip')
        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')

        self.assertIsNotNone(fip)
        self.assertIsNotNone(vmi)

        self.assertEqual(len(fip.virtual_machine_interfaces), 1)
        self.assertTrue('vmi' in fip.virtual_machine_interfaces)
        self.assertEqual(fip.address, "1.2.3.33")
        self.assertEqual(list(vmi.floating_ips)[0], 'fip')

        config_db.FloatingIpSM.delete('fip')
        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_floating_ip

    def test_add_delete_sg(self):
        project = self.add_project("Test-Project", 'project')
        sg = SecurityGroup(name="Test-SG", parent_obj=project)
        sg_dict = self.obj_to_dict(sg)
        sg_dict['parent_uuid'] = 'project'
        sg_dict['uuid'] = 'sg'
        sg_obj = SecurityGroup.from_dict(**sg_dict)
        self.cassandra.object_read = mock.Mock(
            return_value=(True, [sg_dict]))
        config_db.SecurityGroupSM.locate('sg')

        sg = config_db.SecurityGroupSM.get('sg')
        self.assertIsNotNone(sg)

        config_db.SecurityGroupSM.delete('sg')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_sg(self):

    def test_add_delete_interface_route_table(self):
        project = self.add_project("Test-Project", 'project')
        net_obj = self.add_vn("Test-Network", 'network', project)

        route_table = RouteTableType("Test-Route-Table")
        route_table.set_route([])
        intf_route_table_obj = InterfaceRouteTable('Test-route-table',
		interface_route_table_routes=route_table, parent_obj=project)
        irt_dict = self.obj_to_dict(intf_route_table_obj)
        irt_dict['parent_uuid'] = 'project'
        irt_dict['uuid'] = 'irt'
        irt_obj = InterfaceRouteTable.from_dict(**irt_dict)
        config_db.InterfaceRouteTableSM._cassandra.object_read = mock.Mock(return_value=(True, [irt_dict]))
        config_db.InterfaceRouteTableSM.locate('irt')

        vmi_obj = self.add_vmi("Test-VMI", 'vmi', project, net_obj, None, irt_obj)

        irt = config_db.InterfaceRouteTableSM.get('irt')
        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')
        self.assertIsNotNone(vmi)
        self.assertIsNotNone(irt)
        self.assertEqual(len(irt.virtual_machine_interfaces), 1)
        self.assertEqual(vmi.interface_route_tables, set(['irt']))

        config_db.InterfaceRouteTableSM.delete('irt')
        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_interface_route_table

    def test_add_delete_pr_pi_li(self):
        pr = PhysicalRouter(name="Test-PR",
                            physical_router_management_ip="12.3.2.2",
                            physical_router_vendor_name="mx")
        pr_dict = self.obj_to_dict(pr)
        config_db.PhysicalRouterSM._cassandra.object_read = mock.Mock(return_value=(True, [pr_dict]))
        pr_dict['uuid'] = 'pr'
        pr_obj = PhysicalRouter.from_dict(**pr_dict)
        config_db.PhysicalRouterSM.locate('pr')

        pi = PhysicalInterface(name="Test-PI", parent_obj=pr_obj)
        pi_dict = self.obj_to_dict(pi)
        config_db.PhysicalInterfaceSM._cassandra.object_read = mock.Mock(return_value=(True, [pi_dict]))
        pi_dict['uuid'] = 'pi'
        pi_dict['parent_uuid'] = 'pr'
        pi_obj = PhysicalInterface.from_dict(**pi_dict)
        config_db.PhysicalInterfaceSM.locate('pi')

        for i in range(10):
            if i%2:
                li_0 = LogicalInterface(name="Test-LI", parent_obj=pr_obj)
            else:
                li_0 = LogicalInterface(name="Test-LI", parent_obj=pi_obj)
            li_dict = self.obj_to_dict(li_0)
            config_db.LogicalInterfaceSM._cassandra.object_read = mock.Mock(
                return_value=(True, [li_dict]))
            li_dict['uuid'] = 'li_'+str(i)
            if i%2:
                li_dict['parent_uuid'] = 'pr'
            else:
                li_dict['parent_uuid'] = 'pi'
            li_obj = LogicalInterface.from_dict(**li_dict)
            config_db.LogicalInterfaceSM.locate('li_'+str(i))

        pi = config_db.PhysicalInterfaceSM.get('pi')
        pr = config_db.PhysicalRouterSM.get('pr')
        self.assertIsNotNone(pr)
        self.assertIsNotNone(pi)
        self.assertEqual(pi.physical_router, 'pr')
        self.assertEqual(len(pi.logical_interfaces), 5)

        self.assertEqual(len(pr.physical_interfaces), 1)
        self.assertEqual(len(pr.logical_interfaces), 5)
        
        for i in range(10):
            li = config_db.LogicalInterfaceSM.get('li_'+str(i))
            self.assertIsNotNone(li)
            if i%2:
                self.assertEqual(li.physical_router, 'pr')
                self.assertEqual(li.physical_interface, None)
            else:
                self.assertEqual(li.physical_router, None)
                self.assertEqual(li.physical_interface, 'pi')
            config_db.LogicalInterfaceSM.delete('li_'+str(i))

        config_db.LogicalInterfaceSM.delete('li_0')
        config_db.PhysicalInterfaceSM.delete('pi')
        config_db.PhysicalRouterSM.delete('pr')
    # end test_add_delete_pr_pi_li

    def test_add_delete_lr(self):
        project = self.add_project("Test-Project", 'project')
        st_obj = self.add_st("Test-ST", 'st')
        si_obj = self.add_si("Test-SI", 'si', st_obj)
        net_obj = self.add_vn("Test", 'network', project)
        vmi_obj = self.add_vmi("Test-VMI", 'vmi', project, net_obj)

        lr = LogicalRouter(name="Test-LR", parent_obj=project)
        lr.set_service_instance(si_obj)
	lr.set_virtual_network(net_obj)
	lr.set_virtual_machine_interface(vmi_obj)
        lr._pending_field_updates.add('service_instance_refs')
        lr._pending_field_updates.add('virtual_network_refs')
        lr._pending_field_updates.add('virtual_machine_interface_refs')
        lr_dict = self.obj_to_dict(lr)
        lr_dict['parent_uuid'] = 'project'
        lr_dict['uuid'] = 'lr'
        config_db.LogicalRouterSM._cassandra.object_read = mock.Mock(return_value=(True, [lr_dict]))
        config_db.LogicalRouterSM.locate('lr')

        lr = config_db.LogicalRouterSM.get('lr')
        si = config_db.ServiceInstanceSM.get('si')
        vmi = config_db.VirtualMachineInterfaceSM.get('vmi')
        vn = config_db.VirtualNetworkSM.get('network')
        self.assertIsNotNone(lr)
        self.assertIsNotNone(vn)
        self.assertIsNotNone(vmi)
        self.assertIsNotNone(si)
        self.assertEqual(len(lr.virtual_machine_interfaces), 1)
        self.assertEqual(lr.service_instance, 'si')
        self.assertEqual(lr.virtual_network, 'network')

        config_db.LogicalRouterSM.delete('lr')
        config_db.VirtualMachineInterfaceSM.delete('vmi')
        config_db.VirtualNetworkSM.delete('network')
        config_db.ServiceInstanceSM.delete('si')
        config_db.ServiceTemplateSM.delete('st')
        config_db.ProjectSM.delete('project')
    # end test_add_delete_lr(self):
#end ConfigDBTest(unittest.TestCase):
