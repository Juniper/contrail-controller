#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#


from builtins import str
import json
import logging
import uuid

from cfgm_common import get_lr_internal_vn_name
from netaddr import IPNetwork
from testtools import ExpectedException
from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import NoIdError
from vnc_api.exceptions import RefsExistError
from vnc_api.gen.resource_client import Domain
from vnc_api.gen.resource_client import InstanceIp
from vnc_api.gen.resource_client import LogicalRouter
from vnc_api.gen.resource_client import NetworkIpam
from vnc_api.gen.resource_client import Project
from vnc_api.gen.resource_client import RouteTable
from vnc_api.gen.resource_client import VirtualMachine
from vnc_api.gen.resource_client import VirtualMachineInterface
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import IdPermsType
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import IpamType
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import RouteTableType
from vnc_api.gen.resource_xsd import RouteType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import VirtualNetworkRoutedPropertiesType
from vnc_api.gen.resource_xsd import VnSubnetsType

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestLogicalRouter(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestLogicalRouter, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestLogicalRouter, cls).tearDownClass(*args, **kwargs)

    def test_lr_v4_subnets(self):
        # Create Domain
        domain = Domain('my-lr-domain')
        self._vnc_lib.domain_create(domain)

        # Create Project
        project = Project('my-lr-proj', domain)
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        ipam = self._vnc_lib.network_ipam_read(
            ['my-lr-domain', 'my-lr-proj', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn1 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6_vn1 = IpamSubnetType(subnet=SubnetType('fd11::', 120))
        ipam_sn_v4_vn2 = IpamSubnetType(subnet=SubnetType('11.1.2.0', 24))
        ipam_sn_v6_vn2 = IpamSubnetType(subnet=SubnetType('fd12::', 120))

        # Create VN my-vn-1
        vn1 = VirtualNetwork('my-vn-1', project)
        vn1.add_network_ipam(ipam,
                             VnSubnetsType([ipam_sn_v4_vn1, ipam_sn_v6_vn1]))
        self._vnc_lib.virtual_network_create(vn1)
        net_obj1 = self._vnc_lib.virtual_network_read(id=vn1.uuid)

        # Create VN my-vn-2
        vn2 = VirtualNetwork('my-vn-2', project)
        vn2.add_network_ipam(ipam,
                             VnSubnetsType([ipam_sn_v4_vn2, ipam_sn_v6_vn2]))
        self._vnc_lib.virtual_network_create(vn2)
        net_obj2 = self._vnc_lib.virtual_network_read(id=vn2.uuid)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)

        # Create a Virtual Machine Interface belonging to my-vn-1
        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn1)
        port_obj1.set_virtual_machine_interface_device_owner(
            'DEVICE_OWNER_ROUTER_INTF')
        # Assign gateway ip
        ipam_refs = net_obj1.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version == 4:
                    gateway_ip = subnet.get_default_gateway()
        self._vnc_lib.virtual_machine_interface_create(port_obj1)

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()),
                             instance_ip_address=gateway_ip,
                             instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj1)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)

        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj1)
        self._vnc_lib.logical_router_update(lr)

        # Create a Virtual Machine Interface belonging to my-vn-2
        port_obj2 = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj2.uuid = port_obj2.name
        port_obj2.set_virtual_network(vn2)
        port_obj2.set_virtual_machine_interface_device_owner(
            'DEVICE_OWNER_ROUTER_INTF')
        # Assign gateway ip
        ipam_refs = net_obj2.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version == 4:
                    gateway_ip = subnet.get_default_gateway()
        self._vnc_lib.virtual_machine_interface_create(port_obj2)

        # Create v4 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()),
                             instance_ip_address=gateway_ip,
                             instance_ip_family='v4')
        ip_obj2.uuid = ip_obj2.name
        ip_obj2.set_virtual_machine_interface(port_obj2)
        ip_obj2.set_virtual_network(net_obj2)
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)

        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj2)
        self._vnc_lib.logical_router_update(lr)

        # TODO: Schema transformer not integrated in the tests,
        #       hence route-target refs not set yet
        # Verify Route Target Creation
        rt_refs = lr.get_route_target_refs()
        for rt_ref in rt_refs or []:
            rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
            ri_refs = rt_obj.get_routing_instance_back_refs()
            for ri_ref in ri_refs:
                ri_obj = self.vnc_lib.routing_instance_read(id=ri_ref['uuid'])
                ri_name = ri_obj.get_display_name()
                if ri_name != 'my-vn-1' and ri_name != 'my-vn-2':
                    pass

        # cleanup
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.logical_router_delete(id=lr_uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)

    def test_lr_v6_subnets(self):
        # Create Domain
        domain = Domain('my-lr-domain')
        self._vnc_lib.domain_create(domain)

        # Create Project
        project = Project('my-lr-proj', domain)
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        ipam = self._vnc_lib.network_ipam_read(
            ['my-lr-domain', 'my-lr-proj', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn1 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6_vn1 = IpamSubnetType(subnet=SubnetType('fd11::', 120))
        ipam_sn_v4_vn2 = IpamSubnetType(subnet=SubnetType('11.1.2.0', 24))
        ipam_sn_v6_vn2 = IpamSubnetType(subnet=SubnetType('fd12::', 120))

        # Create VN my-vn-1
        vn1 = VirtualNetwork('my-vn-1', project)
        vn1.add_network_ipam(ipam,
                             VnSubnetsType([ipam_sn_v4_vn1, ipam_sn_v6_vn1]))
        self._vnc_lib.virtual_network_create(vn1)
        net_obj1 = self._vnc_lib.virtual_network_read(id=vn1.uuid)

        # Create VN my-vn-2
        vn2 = VirtualNetwork('my-vn-2', project)
        vn2.add_network_ipam(ipam,
                             VnSubnetsType([ipam_sn_v4_vn2, ipam_sn_v6_vn2]))
        self._vnc_lib.virtual_network_create(vn2)
        net_obj2 = self._vnc_lib.virtual_network_read(id=vn2.uuid)

        # Create Logical Router
        lr = LogicalRouter('router-test-v6-%s' % self.id(), project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)

        # Create a Virtual Machine Interface belonging to my-vn-1
        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn1)
        port_obj1.set_virtual_machine_interface_device_owner(
            'DEVICE_OWNER_ROUTER_INTF')
        # Assign gateway ip
        ipam_refs = net_obj1.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version == 6:
                    gateway_ip = subnet.get_default_gateway()
        self._vnc_lib.virtual_machine_interface_create(port_obj1)

        # Create v6 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()),
                             instance_ip_address=gateway_ip,
                             instance_ip_family='v6')
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj1)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)

        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj1)
        lr_obj = self._vnc_lib.logical_router_read(id=lr_uuid)
        self._vnc_lib.logical_router_update(lr_obj)

        # Create a Virtual Machine Interface belonging to my-vn-2
        port_obj2 = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj2.uuid = port_obj2.name
        port_obj2.set_virtual_network(vn2)
        port_obj2.set_virtual_machine_interface_device_owner(
            'DEVICE_OWNER_ROUTER_INTF')
        # Assign gateway ip
        ipam_refs = net_obj2.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version == 6:
                    gateway_ip = subnet.get_default_gateway()
        self._vnc_lib.virtual_machine_interface_create(port_obj2)

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()),
                             instance_ip_address=gateway_ip,
                             instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        ip_obj2.set_virtual_machine_interface(port_obj2)
        ip_obj2.set_virtual_network(net_obj2)
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)

        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj2)
        lr_obj = self._vnc_lib.logical_router_read(id=lr_uuid)
        self._vnc_lib.logical_router_update(lr_obj)

        # TODO: Schema transformer not integrated in the tests,
        #       hence route-target refs not set yet
        # Verify Route Target Creation
        rt_refs = lr.get_route_target_refs()
        for rt_ref in rt_refs or []:
            rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
            ri_refs = rt_obj.get_routing_instance_back_refs()
            for ri_ref in ri_refs:
                ri_obj = self.vnc_lib.routing_instance_read(id=ri_ref['uuid'])
                ri_name = ri_obj.get_display_name()
                if ri_name() != 'my-vn-1' and ri_name() != 'my-vn-2':
                    pass

        # cleanup
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.logical_router_delete(id=lr_uuid)
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)

    def test_route_table_prefixes(self):
        rt = RouteTable("rt1")
        routes = RouteTableType()
        route1 = RouteType(prefix="1.1.1.1/0", next_hop="10.10.10.10",
                           next_hop_type="ip-address")
        route2 = RouteType(prefix="1.1.1.1/0", next_hop="20.20.20.20",
                           next_hop_type="ip-address")
        routes.add_route(route1)
        routes.add_route(route2)
        rt.set_routes(routes)
        try:
            self._vnc_lib.route_table_create(rt)
            self.fail("Create succeeded unexpectedly: duplicate prefixes "
                      "routes")
        except BadRequest:
            pass

        routes.delete_route(route2)
        route2 = RouteType(prefix="1.1.1.2/0", next_hop="20.20.20.20",
                           next_hop_type="ip-address")
        routes.add_route(route2)
        rt.set_routes(routes)
        self._vnc_lib.route_table_create(rt)

        routes.delete_route(route2)
        route2 = RouteType(prefix="1.1.1.1/0", next_hop="20.20.20.20",
                           next_hop_type="ip-address")
        routes.add_route(route2)
        rt.set_routes(routes)
        try:
            self._vnc_lib.route_table_update(rt)
            self.failt("Update succeeded unexpectedly - duplicate prefixe"
                       "routes")
        except BadRequest:
            pass

    def test_vm_port_not_added_to_lr(self):
        project = self._vnc_lib.project_read(
            ['default-domain', 'default-project'])
        ipam = self._vnc_lib.network_ipam_read(
            ['default-domain', 'default-project', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN my-vn
        vn = VirtualNetwork('%s-vn' % self.id(), project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)

        # Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name

        # Create VM
        vm_inst_obj = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj.uuid = vm_inst_obj.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj)

        id_perms = IdPermsType(enable=True)
        vm_port_obj = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj, id_perms=id_perms)
        vm_port_obj.uuid = vm_port_obj.name
        vm_port_obj.set_virtual_network(vn)
        ip_obj.set_virtual_machine_interface(vm_port_obj)
        ip_obj.set_virtual_network(net_obj)
        self._vnc_lib.virtual_machine_interface_create(vm_port_obj)

        self._vnc_lib.instance_ip_create(ip_obj)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        self._vnc_lib.logical_router_create(lr)

        # Add Router Interface
        lr.add_virtual_machine_interface(vm_port_obj)
        with ExpectedException(RefsExistError):
            self._vnc_lib.logical_router_update(lr)
        lr.del_virtual_machine_interface(vm_port_obj)

        # Create Port
        port_obj = self.create_port(project, net_obj)
        lr.add_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)
        with ExpectedException(BadRequest):
            port_obj.add_virtual_machine(vm_inst_obj)
            self._vnc_lib.virtual_machine_interface_update(port_obj)
        self._vnc_lib.logical_router_delete(id=lr.uuid)

    def create_port(self, project, vn):
        # Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name

        # Create Port
        id_perms = IdPermsType(enable=True)
        port_obj = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)
        ip_obj.set_virtual_machine_interface(port_obj)
        ip_obj.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(port_obj)

        self._vnc_lib.instance_ip_create(ip_obj)
        return port_obj

    def test_same_network_not_attached_to_lr(self):
        project = self._vnc_lib.project_read(
            ['default-domain', 'default-project'])
        ipam = self._vnc_lib.network_ipam_read(
            ['default-domain', 'default-project', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN my-vn
        vn = VirtualNetwork('%s-vn' % self.id(), project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)

        # Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name

        # Create Port
        port_obj = self.create_port(project, net_obj)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr.set_logical_router_type('snat-routing')
        self._vnc_lib.logical_router_create(lr)

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)

        # set router_external
        net_obj.set_router_external(True)
        self._vnc_lib.virtual_network_update(net_obj)

        with ExpectedException(BadRequest):
            lr.add_virtual_network(net_obj)
            self._vnc_lib.logical_router_update(lr)
        lr.del_virtual_network(net_obj)

        lr.del_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)
        lr.add_virtual_network(net_obj)
        self._vnc_lib.logical_router_update(lr)

        # Create Port
        port_obj = self.create_port(project, net_obj)
        with ExpectedException(BadRequest):
            lr.add_virtual_machine_interface(port_obj)
            self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.logical_router_delete(id=lr.uuid)

    def test_vxlan_routing_for_internal_vn(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)

        # Check to see whether internal VN for VxLAN Routing is created.
        int_vn_name = get_lr_internal_vn_name(lr_uuid)
        int_vn_fqname = ['default-domain', 'default-project', int_vn_name]
        try:
            self._vnc_lib.virtual_network_read(fq_name=int_vn_fqname)
        except NoIdError as e:
            # Invisible objects do not come up in read
            # calls but throws up a exception saying the
            # object is invisible but cannot be read, confirming
            # the presence of the object. Hack!
            if "This object is not visible" not in str(e):
                assert(False)

        # Check to see if deleting the VN deletes the internal VN
        # that was created.
        self._vnc_lib.logical_router_delete(id=lr_uuid)
        try:
            self._vnc_lib.virtual_network_read(fq_name=int_vn_fqname)
            self.fail("Logical router internal virtual network was not "
                      "removed")
        except NoIdError:
            pass

        # Check to see if we are able to disable VxLAN Routing
        # after LR is deleted in the project.
        project.set_vxlan_routing(False)
        self._vnc_lib.project_update(project)

    def test_vxlan_create_for_lr(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)
        mock_zk = self._api_server._db_conn._zk_db

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr.set_vxlan_network_identifier('5000')
        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)
        vxlan_id = lr.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, '5000')

        int_vn_name = get_lr_internal_vn_name(lr_uuid)
        int_vn_fqname = ['default-domain', 'default-project', int_vn_name]

        self.assertEqual(':'.join(int_vn_fqname) + "_vxlan",
                         mock_zk.get_vn_from_id(int(vxlan_id)))
        self._vnc_lib.logical_router_delete(id=lr_uuid)

    def test_vxlan_update_for_lr(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)
        mock_zk = self._api_server._db_conn._zk_db

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr.set_vxlan_network_identifier('5001')
        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr_read = self._vnc_lib.logical_router_read(id=lr_uuid)
        vxlan_id = lr_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, '5001')

        lr.set_vxlan_network_identifier('5002')
        self._vnc_lib.logical_router_update(lr)
        lr_read = self._vnc_lib.logical_router_read(id=lr_uuid)
        vxlan_id = lr_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, '5002')

        int_vn_name = get_lr_internal_vn_name(lr_uuid)
        int_vn_fqname = ['default-domain', 'default-project', int_vn_name]

        self.assertEqual(':'.join(int_vn_fqname) + "_vxlan",
                         mock_zk.get_vn_from_id(int(vxlan_id)))

        self._vnc_lib.logical_router_delete(id=lr_uuid)

    def test_vxlan_update_failure_for_lr(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)
        mock_zk = self._api_server._db_conn._zk_db

        # Create Logical Router
        lr1 = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr1.set_vxlan_network_identifier('5003')
        lr1.set_logical_router_type('vxlan-routing')
        lr1_uuid = self._vnc_lib.logical_router_create(lr1)
        lr1_read = self._vnc_lib.logical_router_read(id=lr1_uuid)
        vxlan_id1 = lr1_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id1, '5003')

        lr2 = LogicalRouter('router-test-v4-%s-2' % self.id(), project)
        lr2.set_vxlan_network_identifier('5004')
        lr2.set_logical_router_type('vxlan-routing')
        lr2_uuid = self._vnc_lib.logical_router_create(lr2)
        lr2_read = self._vnc_lib.logical_router_read(id=lr2_uuid)
        vxlan_id2 = lr2_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id2, '5004')

        lr2.set_vxlan_network_identifier('5003')

        with ExpectedException(BadRequest):
            self._vnc_lib.logical_router_update(lr2)

        lr_read = self._vnc_lib.logical_router_read(id=lr2_uuid)
        vxlan_id = lr_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, '5004')

        int_vn_name = get_lr_internal_vn_name(lr2_uuid)
        int_vn_fqname = ['default-domain', 'default-project', int_vn_name]

        self.assertEqual(':'.join(int_vn_fqname) + "_vxlan",
                         mock_zk.get_vn_from_id(int(vxlan_id)))

        self._vnc_lib.logical_router_delete(id=lr1_uuid)
        self._vnc_lib.logical_router_delete(id=lr2_uuid)

    def test_vxlan_deallocate_for_lr(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)
        mock_zk = self._api_server._db_conn._zk_db

        # Create Logical Router
        lr = LogicalRouter('router-test-v4-%s' % self.id(), project)
        lr.set_vxlan_network_identifier('5005')
        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)
        vxlan_id = lr.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, '5005')

        int_vn_name = get_lr_internal_vn_name(lr_uuid)
        int_vn_fqname = ['default-domain', 'default-project', int_vn_name]

        self._vnc_lib.logical_router_delete(id=lr_uuid)
        self.assertNotEqual(':'.join(int_vn_fqname) + "_vxlan",
                            mock_zk.get_vn_from_id(int(vxlan_id)))

    def test_change_from_vxlan_to_snat(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        # Create Logical Router enabled for VxLAN.
        lr = LogicalRouter('router-test-vxlan-to-snat-%s' %
                           (self.id()), project)

        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)
        logger.debug('Created Logical Router ')

        lr.set_logical_router_type('snat-routing')
        with ExpectedException(BadRequest):
            self._vnc_lib.logical_router_update(lr)

        logger.debug('PASS - Could not update LR from VXLAN to SNAT')

    def test_change_from_snat_to_vxlan(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        # Create Logical Router enabled for SNAT.
        lr = LogicalRouter(
            'router-test-snat-to-vxlan-%s'
            % (self.id()), project)

        lr.set_logical_router_type('snat-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)
        logger.debug('Created Logical Router ')

        lr.set_logical_router_type('vxlan-routing')
        with ExpectedException(BadRequest):
            self._vnc_lib.logical_router_update(lr)

        logger.debug('PASS: Could not update LR from SNAT to VXLAN')

    # test create LR with non-shared VN already in another LR
    # 1. Create LR_A and assign a VN VN_not_shared --> Ok
    # 2. VN_not_shared's annotations should have LR_A in the list
    # 3. Create LR_B and try to assign VN_not_shared --> Error
    # 4. Delete LR_A and VN_not_shared's annotations should not have
    #    LR key value pair

    def test_lr_create_delete_with_vn(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        # Create Logical Router
        lr = LogicalRouter(
            'LR_A-%s' % (self.id()), project)

        ipam = self._vnc_lib.network_ipam_read(
            ['default-domain', 'default-project', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create unshared VN
        vn_not_shared = VirtualNetwork(
            'VN_not_shared-%s' % (self.id()), project)
        vn_not_shared.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))

        self._vnc_lib.virtual_network_create(vn_not_shared)
        net_obj = self._vnc_lib.virtual_network_read(id=vn_not_shared.uuid)

        # Create Port
        port_obj = self.create_port(project, net_obj)

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj)
        lr_a_uuid = self._vnc_lib.logical_router_create(lr)

        net_obj = self._vnc_lib.virtual_network_read(id=vn_not_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNotNone(net_annotations)
        net_kvps = net_annotations.get_key_value_pair()
        self.assertIsNotNone(net_kvps)
        for kvp in net_kvps:
            if kvp.get_key() == 'LogicalRouter':
                self.assertEqual(
                    kvp.get_value(),
                    json.dumps({"lr_fqname_strs":
                                ["default-domain:default-project:LR_A-%s"
                                 % (self.id())]}))

        # Create Logical Router
        lr = LogicalRouter('LR_B-%s' % (self.id()), project)
        # Try to add Router Interface
        lr.add_virtual_machine_interface(port_obj)
        with ExpectedException(BadRequest):
            lr_b_uuid = self._vnc_lib.logical_router_create(lr)

        self._vnc_lib.logical_router_delete(id=lr_a_uuid)
        net_obj = self._vnc_lib.virtual_network_read(id=vn_not_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNone(net_annotations)

    # test update LR with LR already having a shared VN and a non shared VN
    # 1. Create LR_A and assign a VN VN_not_shared
    # 2. Update LR_A and assign VN_shared --> Ok
    # 3. Create LR_B
    # 4. Update LR_B by trying to assign VN VN_shared --> Ok
    # 5. VN_not_shared's annotations should have LR_A in the list
    # 6. VN_shared's annotations should have LR_A and LR_B in the list
    # 7. Update LR_B by trying to assign VN_not_shared --> Error
    # 8. Delete LR_A and VN_not_shared's annotations should not have
    #    LR key value pair while VN_shared's annotations will only have VN_B
    #    in LR key value pair

    def test_lr_update_delete_with_vn(self):
        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        # Create Logical Router
        lr = LogicalRouter(
            'LR_A-%s' % (self.id()), project)

        ipam = self._vnc_lib.network_ipam_read(
            ['default-domain', 'default-project', 'default-network-ipam'])

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create unshared VN
        vn_not_shared = VirtualNetwork(
            'VN_not_shared-%s' % (self.id()), project)
        vn_not_shared.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))

        self._vnc_lib.virtual_network_create(vn_not_shared)
        net_obj_unshared = self._vnc_lib.virtual_network_read(
            id=vn_not_shared.uuid)

        # Create Port
        port_obj_unshared = self.create_port(project, net_obj_unshared)

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj_unshared)
        lr_a_uuid = self._vnc_lib.logical_router_create(lr)

        # Create shared VN
        vn_shared = VirtualNetwork(
            'VN_shared-%s' % (self.id()), project)
        vn_shared.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))
        vn_routed_prop = VirtualNetworkRoutedPropertiesType(
            shared_across_all_lrs=True)
        vn_shared.set_virtual_network_routed_properties(vn_routed_prop)

        self._vnc_lib.virtual_network_create(vn_shared)
        net_obj_shared = self._vnc_lib.virtual_network_read(id=vn_shared.uuid)

        # Create Port
        port_obj_shared = self.create_port(project, net_obj_shared)

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj_shared)
        self._vnc_lib.logical_router_update(lr)

        # Create Logical Router
        lr = LogicalRouter('LR_B-%s' % (self.id()), project)
        lr_b_uuid = self._vnc_lib.logical_router_create(lr)

        # Try to add Router Interface
        lr.add_virtual_machine_interface(port_obj_shared)
        self._vnc_lib.logical_router_update(lr)

        net_obj = self._vnc_lib.virtual_network_read(id=vn_not_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNotNone(net_annotations)
        net_kvps = net_annotations.get_key_value_pair()
        self.assertIsNotNone(net_kvps)
        for kvp in net_kvps:
            if kvp.get_key() == 'LogicalRouter':
                self.assertEqual(
                    kvp.get_value(),
                    json.dumps({"lr_fqname_strs":
                                ["default-domain:default-project:LR_A-%s"
                                 % (self.id())]}))

        net_obj = self._vnc_lib.virtual_network_read(id=vn_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNotNone(net_annotations)
        net_kvps = net_annotations.get_key_value_pair()
        self.assertIsNotNone(net_kvps)
        for kvp in net_kvps:
            if kvp.get_key() == 'LogicalRouter':
                self.assertEqual(
                    kvp.get_value(),
                    json.dumps({"lr_fqname_strs":
                                ["default-domain:default-project:LR_A-%s"
                                 % (self.id()),
                                 "default-domain:default-project:LR_B-%s"
                                 % (self.id())]}))

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj_unshared)
        with ExpectedException(BadRequest):
            self._vnc_lib.logical_router_update(lr)

        self._vnc_lib.logical_router_delete(id=lr_a_uuid)
        net_obj = self._vnc_lib.virtual_network_read(id=vn_not_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNone(net_annotations)

        net_obj = self._vnc_lib.virtual_network_read(id=vn_shared.uuid)
        net_annotations = net_obj.get_annotations()
        self.assertIsNotNone(net_annotations)
        net_kvps = net_annotations.get_key_value_pair()
        self.assertIsNotNone(net_kvps)
        for kvp in net_kvps:
            if kvp.get_key() == 'LogicalRouter':
                self.assertEqual(
                    kvp.get_value(),
                    json.dumps({
                        "lr_fqname_strs":
                        ["default-domain:default-project:LR_B-%s"
                         % (self.id())]}))
