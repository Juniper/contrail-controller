#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import logging
import uuid

from netaddr import IPNetwork
from vnc_api.gen.resource_client import Fabric
from vnc_api.gen.resource_client import FabricNamespace
from vnc_api.gen.resource_client import GlobalSystemConfig
from vnc_api.gen.resource_client import InstanceIp
from vnc_api.gen.resource_client import LogicalRouter
from vnc_api.gen.resource_client import NetworkIpam
from vnc_api.gen.resource_client import PhysicalInterface
from vnc_api.gen.resource_client import PhysicalRouter
from vnc_api.gen.resource_client import PortTuple
from vnc_api.gen.resource_client import Project
from vnc_api.gen.resource_client import ServiceAppliance
from vnc_api.gen.resource_client import ServiceApplianceSet
from vnc_api.gen.resource_client import ServiceInstance
from vnc_api.gen.resource_client import ServiceTemplate
from vnc_api.gen.resource_client import VirtualMachineInterface
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import FabricNetworkTag
from vnc_api.gen.resource_xsd import IpamSubnets
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.gen.resource_xsd import ServiceApplianceInterfaceType
from vnc_api.gen.resource_xsd import ServiceInstanceType
from vnc_api.gen.resource_xsd import ServiceTemplateInterfaceType
from vnc_api.gen.resource_xsd import ServiceTemplateType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import VirtualNetworkType
from vnc_api.gen.resource_xsd import VnSubnetsType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)
# logger.setLevel(logging.DEBUG)


class TestPortTupleBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPortTupleBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPortTupleBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestPnfPortTuple(TestPortTupleBase):
    def setUp(self):
        def _carve_out_subnets(subnets, cidr):
            carved_subnets = []
            for subnet in subnets:
                slash_x_subnets = IPNetwork(subnet.get('cidr')).subnet(cidr)
                for slash_x_sn in slash_x_subnets:
                    carved_subnets.append({'cidr': str(slash_x_sn)})
            return carved_subnets
        # end _carve_out_subnets

        def _get_network_ipam(ipam_name, subnets, subnetting):
            def _new_subnet(cidr):
                split_cidr = cidr.split('/')
                return SubnetType(
                    ip_prefix=split_cidr[0],
                    ip_prefix_len=split_cidr[1])
            # end _new_subnet
            ipam = NetworkIpam(
                name=ipam_name,
                ipam_subnets=IpamSubnets([
                    IpamSubnetType(
                        subnet=_new_subnet(sn.get('cidr')),
                        default_gateway=sn.get('gateway'),
                        subnet_uuid=str(uuid.uuid1())
                    ) for sn in subnets if int(sn.get('cidr').split('/')[-1]) < 31
                ]),
                ipam_subnet_method='flat-subnet',
                ipam_subnetting=subnetting
            )
            return ipam
        # end _add_network_ipam
        super(TestPnfPortTuple, self).setUp()
        logger.debug("setUp called")
        # Create Global objects
        default_gsc_name = 'default-global-system-config'
        gsc_obj = self.api.global_system_config_read(
            GlobalSystemConfig().fq_name)
        proj_obj = self.api.project_read(Project().fq_name)
        ipam_obj = self.api.network_ipam_read(NetworkIpam().fq_name)
        # Create left/right VN
        left_vn_name = "left_vn-" + self.id()
        left_vn_obj = VirtualNetwork(left_vn_name, parent_obj=proj_obj)
        left_vn_subnet = "10.100.1.0"
        left_vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(left_vn_subnet, 24))]))
        self.left_vn_uuid = self.api.virtual_network_create(left_vn_obj)
        right_vn_name = "right_vn-" + self.id()
        right_vn_obj = VirtualNetwork(right_vn_name, parent_obj=proj_obj)
        right_vn_subnet = "20.100.1.0"
        right_vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(right_vn_subnet, 24))]))
        self.right_vn_uuid = self.api.virtual_network_create(right_vn_obj)
        # Create left/right VMI
        left_vmi_name = 'left_vmi-' + self.id()
        left_vmi = VirtualMachineInterface(
            name=left_vmi_name, parent_obj=proj_obj)
        left_vmi.add_virtual_network(left_vn_obj)
        left_vmi.set_virtual_machine_interface_device_owner(
            'network:router_interface')
        self.left_vmi_uuid = self.api.virtual_machine_interface_create(
            left_vmi)
        right_vmi_name = 'right_vmi-' + self.id()
        right_vmi = VirtualMachineInterface(
            name=right_vmi_name, parent_obj=proj_obj)
        right_vmi.add_virtual_network(right_vn_obj)
        right_vmi.set_virtual_machine_interface_device_owner(
            'network:router_interface')
        self.right_vmi_uuid = self.api.virtual_machine_interface_create(
            right_vmi)
        # Create left/right IIP
        left_iip_name = 'left_iip-' + self.id()
        left_iip = InstanceIp(name=left_iip_name)
        left_iip.add_virtual_network(left_vn_obj)
        left_iip.add_virtual_machine_interface(left_vmi)
        self.left_iip_uuid = self.api.instance_ip_create(left_iip)
        right_iip_name = 'right_iip-' + self.id()
        right_iip = InstanceIp(name=right_iip_name)
        right_iip.add_virtual_network(right_vn_obj)
        right_iip.add_virtual_machine_interface(right_vmi)
        self.right_iip_uuid = self.api.instance_ip_create(right_iip)
        # Create PNF Physical Router object
        pnf_obj = PhysicalRouter('pnf-' + self.id(), gsc_obj)
        pnf_obj.set_physical_router_role('pnf')
        self.pnf_uuid = self.api.physical_router_create(pnf_obj)
        # Create spine Physical Router object
        spine_obj = PhysicalRouter('spine-' + self.id(), gsc_obj)
        self.spine_uuid = self.api.physical_router_create(spine_obj)
        # Create left/right LR
        left_lr_name = 'left_lr-' + self.id()
        left_lr_obj = LogicalRouter(name=left_lr_name, parent_obj=proj_obj)
        left_lr_obj.add_virtual_machine_interface(left_vmi)
        left_lr_obj.add_physical_router(spine_obj)
        self.left_lr_uuid = self.api.logical_router_create(left_lr_obj)
        right_lr_name = 'right_lr-' + self.id()
        right_lr_obj = LogicalRouter(name=right_lr_name, parent_obj=proj_obj)
        right_lr_obj.add_virtual_machine_interface(right_vmi)
        right_lr_obj.add_physical_router(spine_obj)
        self.right_lr_uuid = self.api.logical_router_create(right_lr_obj)
        # Connect networks to respective LR
        left_vn_obj.add_logical_router(left_lr_obj)
        self.api.virtual_network_update(left_vn_obj)
        right_vn_obj.add_logical_router(right_lr_obj)
        self.api.virtual_network_update(right_vn_obj)
        # create left, right PNF PI
        left_pnf_pi_obj = PhysicalInterface(
            'ge-0/0/1-' + self.id(), parent_obj=pnf_obj)
        right_pnf_pi_obj = PhysicalInterface(
            'ge-0/0/2-' + self.id(), parent_obj=pnf_obj)
        lo_pnf_pi_obj = PhysicalInterface('lo0', parent_obj=pnf_obj)
        self.left_pnf_pi_uuid = self.api.physical_interface_create(
            left_pnf_pi_obj)
        self.right_pnf_pi_uuid = self.api.physical_interface_create(
            right_pnf_pi_obj)
        self.lo_pnf_pi_uuid = self.api.physical_interface_create(lo_pnf_pi_obj)
        # create left, right spine PI
        left_spine_pi_obj = PhysicalInterface(
            'xe-0/0/1-' + self.id(), parent_obj=spine_obj)
        right_spine_pi_obj = PhysicalInterface(
            'xe-0/0/2-' + self.id(), parent_obj=spine_obj)
        self.left_spine_pi_uuid = self.api.physical_interface_create(
            left_spine_pi_obj)
        self.right_spine_pi_uuid = self.api.physical_interface_create(
            right_spine_pi_obj)
        # Create Service Appliance Set object
        sas_obj = ServiceApplianceSet('sas-' + self.id(), gsc_obj)
        sas_obj.set_service_appliance_set_virtualization_type(
            'physical-device')
        self.sas_uuid = self.api.service_appliance_set_create(sas_obj)
        # Create Service template object
        st_obj = ServiceTemplate(name='st-' + self.id())
        st_obj.set_service_appliance_set(sas_obj)
        svc_properties = ServiceTemplateType()
        svc_properties.set_service_virtualization_type('physical-device')
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('left')
        svc_properties.add_interface_type(if_type)
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('right')
        svc_properties.add_interface_type(if_type)
        st_obj.set_service_template_properties(svc_properties)
        self.st_uuid = self.api.service_template_create(st_obj)
        # Create Service Instance object
        si_fqn = ['default-domain', 'default-project', 'si-' + self.id()]
        si_obj = ServiceInstance(fq_name=si_fqn)
        si_obj.fq_name = si_fqn
        si_obj.add_service_template(st_obj)
        kvp_array = []
        kvp = KeyValuePair("left-svc-vlan", "100")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-vlan", "101")
        kvp_array.append(kvp)
        kvp = KeyValuePair("left-svc-asns", "66000,66001")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-asns", "66000,66002")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        si_obj.set_annotations(kvps)
        props = ServiceInstanceType()
        props.set_service_virtualization_type('physical-device')
        props.set_ha_mode("active-standby")
        si_obj.set_service_instance_properties(props)
        self.si_uuid = self.api.service_instance_create(si_obj)
        # Create service appliance
        sa_obj = ServiceAppliance('sa-' + self.id(), parent_obj=sas_obj)
        kvp_array = []
        kvp = KeyValuePair(
            "left-attachment-point",
            default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/1-' +
            self.id())
        kvp_array.append(kvp)
        kvp = KeyValuePair(
            "right-attachment-point",
            default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/2-' +
            self.id())
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sa_obj.set_service_appliance_properties(kvps)
        sa_obj.set_service_appliance_virtualization_type('physical-device')
        attr = ServiceApplianceInterfaceType(interface_type='left')
        sa_obj.add_physical_interface(left_pnf_pi_obj, attr)
        attr = ServiceApplianceInterfaceType(interface_type='right')
        sa_obj.add_physical_interface(right_pnf_pi_obj, attr)
        self.sa_uuid = self.api.service_appliance_create(sa_obj)
        # Create fabric and add it to spine and PNF
        fab_obj = Fabric('fab-' + self.id())
        self.fab_uuid = self.api.fabric_create(fab_obj)
        pnf_obj.add_fabric(fab_obj)
        self.api.physical_router_update(pnf_obj)
        spine_obj.add_fabric(fab_obj)
        self.api.physical_router_update(spine_obj)
        fab_obj = self.api.fabric_read(id=self.fab_uuid)
        # Create PNF service chain IPAM/network
        pnf_subnets = [{'ip_prefix': "10.1.1.0", 'ip_prefix_len': '28'}]
        pnf_cidr = [{'cidr': "10.1.1.0/28"}]
        sc_ns_obj = FabricNamespace(
            'servicechain-subnet-' + self.id(),
            parent_obj=fab_obj,
            fabric_namespace_type='IPV4-CIDR',
            fabric_namespace_value={
                'ipv4_cidr': {
                    'subnet': pnf_subnets}})
        peer_subnets = _carve_out_subnets(pnf_cidr, 29)
        self.sc_ns_uuid = self.api.fabric_namespace_create(sc_ns_obj)
        pnf_vn_obj = VirtualNetwork(
            name='fab-' + self.id() + '-pnf-servicechain-network',
            virtual_network_properties=VirtualNetworkType(
                forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        self.pnf_vn_uuid = self.api.virtual_network_create(pnf_vn_obj)
        pnf_ipam_obj = _get_network_ipam(
            'fab-' +
            self.id() +
            '-pnf-servicechain-network-ipam',
            peer_subnets,
            True)
        self.pnf_ipam_uuid = self.api.network_ipam_create(pnf_ipam_obj)
        pnf_vn_obj.add_network_ipam(pnf_ipam_obj, VnSubnetsType([]))
        self.api.virtual_network_update(pnf_vn_obj)
        fab_obj = self.api.fabric_read(id=self.fab_uuid)
        fab_obj.add_virtual_network(
            pnf_vn_obj, FabricNetworkTag(
                network_type='pnf-servicechain'))
        self.api.fabric_update(fab_obj)
        # Create loopback IPAM/network
        lo_subnets = [{'ip_prefix': "100.100.100.0", 'ip_prefix_len': '28'}]
        lo_cidr = [{'cidr': "100.100.100.0/28"}]
        peer_subnets = _carve_out_subnets(lo_cidr, 28)
        lo_ns_obj = FabricNamespace(
            'loopback-subnet-' + self.id(),
            parent_obj=fab_obj,
            fabric_namespace_type='IPV4-CIDR',
            fabric_namespace_value={
                'ipv4_cidr': {
                    'subnet': lo_subnets}})
        self.lo_ns_uuid = self.api.fabric_namespace_create(lo_ns_obj)
        lo_vn_obj = VirtualNetwork(
            name='fab-' + self.id() + '-loopback-network',
            virtual_network_properties=VirtualNetworkType(
                forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        self.lo_vn_uuid = self.api.virtual_network_create(lo_vn_obj)
        lo_ipam_obj = _get_network_ipam(
            'fab-' +
            self.id() +
            '-loopback-network-ipam',
            peer_subnets,
            False)
        self.lo_ipam_uuid = self.api.network_ipam_create(lo_ipam_obj)
        lo_vn_obj.add_network_ipam(lo_ipam_obj, VnSubnetsType([]))
        self.api.virtual_network_update(lo_vn_obj)
        fab_obj = self.api.fabric_read(id=self.fab_uuid)
        fab_obj.add_virtual_network(
            lo_vn_obj, FabricNetworkTag(
                network_type='loopback'))
        self.api.fabric_update(fab_obj)
        pass

    def tearDown(self):
        super(TestPnfPortTuple, self).tearDown()
        logger.debug("TearDown called")
        fab_obj = self.api.fabric_read(id=self.fab_uuid)
        pnf_vn_obj = self.api.virtual_network_read(id=self.pnf_vn_uuid)
        lo_vn_obj = self.api.virtual_network_read(id=self.lo_vn_uuid)
        pnf_ipam_obj = self.api.network_ipam_read(id=self.pnf_ipam_uuid)
        lo_ipam_obj = self.api.network_ipam_read(id=self.lo_ipam_uuid)
        pnf_obj = self.api.physical_router_read(id=self.pnf_uuid)
        spine_obj = self.api.physical_router_read(id=self.spine_uuid)
        pnf_obj.del_fabric(fab_obj)
        spine_obj.del_fabric(fab_obj)
        self.api.physical_router_update(spine_obj)
        self.api.physical_router_update(pnf_obj)
        pnf_vn_obj.del_network_ipam(pnf_ipam_obj)
        lo_vn_obj.del_network_ipam(lo_ipam_obj)
        self.api.virtual_network_update(pnf_vn_obj)
        self.api.virtual_network_update(lo_vn_obj)
        fab_obj.del_virtual_network(pnf_vn_obj)
        fab_obj.del_virtual_network(lo_vn_obj)
        self.api.fabric_update(fab_obj)
        self.api.virtual_network_delete(id=self.pnf_vn_uuid)
        self.api.virtual_network_delete(id=self.lo_vn_uuid)
        self.api.network_ipam_delete(id=self.pnf_ipam_uuid)
        self.api.network_ipam_delete(id=self.lo_ipam_uuid)
        self.api.fabric_namespace_delete(id=self.sc_ns_uuid)
        self.api.fabric_namespace_delete(id=self.lo_ns_uuid)
        self.api.fabric_delete(id=self.fab_uuid)
        self.api.service_appliance_delete(id=self.sa_uuid)
        self.api.service_instance_delete(id=self.si_uuid)
        self.api.service_template_delete(id=self.st_uuid)
        self.api.service_appliance_set_delete(id=self.sas_uuid)
        self.api.physical_interface_delete(id=self.left_spine_pi_uuid)
        self.api.physical_interface_delete(id=self.right_spine_pi_uuid)
        self.api.physical_interface_delete(id=self.left_pnf_pi_uuid)
        self.api.physical_interface_delete(id=self.right_pnf_pi_uuid)
        self.api.physical_interface_delete(id=self.lo_pnf_pi_uuid)
        left_vn_obj = self.api.virtual_network_read(id=self.left_vn_uuid)
        right_vn_obj = self.api.virtual_network_read(id=self.right_vn_uuid)
        left_lr_obj = self.api.logical_router_read(id=self.left_lr_uuid)
        right_lr_obj = self.api.logical_router_read(id=self.right_lr_uuid)
        left_vn_obj.del_logical_router(left_lr_obj)
        right_vn_obj.del_logical_router(right_lr_obj)
        self.api.virtual_network_update(left_vn_obj)
        self.api.virtual_network_update(right_vn_obj)
        self.api.logical_router_delete(id=self.left_lr_uuid)
        self.api.logical_router_delete(id=self.right_lr_uuid)
        self.api.physical_router_delete(id=self.spine_uuid)
        self.api.physical_router_delete(id=self.pnf_uuid)
        self.api.instance_ip_delete(id=self.left_iip_uuid)
        self.api.instance_ip_delete(id=self.right_iip_uuid)
        self.api.virtual_machine_interface_delete(id=self.left_vmi_uuid)
        self.api.virtual_machine_interface_delete(id=self.right_vmi_uuid)
        self.api.virtual_network_delete(id=self.left_vn_uuid)
        self.api.virtual_network_delete(id=self.right_vn_uuid)
        pass

    def test_valid_pt(self):
        si_obj = self.api.service_instance_read(id=self.si_uuid)
        left_lr_obj = self.api.logical_router_read(id=self.left_lr_uuid)
        right_lr_obj = self.api.logical_router_read(id=self.right_lr_uuid)
        pt_obj = PortTuple('pt-' + self.id(), parent_obj=si_obj)
        pt_obj.add_logical_router(left_lr_obj)
        pt_obj.add_logical_router(right_lr_obj)
        kvp_array = []
        kvp = KeyValuePair("left-lr", self.left_lr_uuid)
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-lr", self.right_lr_uuid)
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        pt_obj.set_annotations(kvps)
        self.api.port_tuple_create(pt_obj)

        left_pnf_pi_obj = self.api.physical_interface_read(
            id=self.left_pnf_pi_uuid)
        right_pnf_pi_obj = self.api.physical_interface_read(
            id=self.left_pnf_pi_uuid)
        left_spine_pi_obj = self.api.physical_interface_read(
            id=self.left_spine_pi_uuid)
        right_spine_pi_obj = self.api.physical_interface_read(
            id=self.right_spine_pi_uuid)
        lo_pnf_pi_obj = self.api.physical_interface_read(
            id=self.lo_pnf_pi_uuid)

        self.assertEqual(len(lo_pnf_pi_obj.get_logical_interfaces()), 1)
        self.assertEqual(len(left_pnf_pi_obj.get_logical_interfaces()), 1)
        self.assertEqual(len(right_pnf_pi_obj.get_logical_interfaces()), 1)
        self.assertEqual(len(left_spine_pi_obj.get_logical_interfaces()), 1)
        self.assertEqual(len(right_spine_pi_obj.get_logical_interfaces()), 1)

        left_pnf_li_uuid = left_pnf_pi_obj.get_logical_interfaces()[
            0].get('uuid')
        right_pnf_li_uuid = right_pnf_pi_obj.get_logical_interfaces()[
            0].get('uuid')
        left_spine_li_uuid = left_spine_pi_obj.get_logical_interfaces()[
            0].get('uuid')
        right_spine_li_uuid = right_spine_pi_obj.get_logical_interfaces()[
            0].get('uuid')
        lo_pnf_li_uuid = lo_pnf_pi_obj.get_logical_interfaces()[0].get('uuid')

        left_pnf_li_obj = self.api.logical_interface_read(id=left_pnf_li_uuid)
        right_pnf_li_obj = self.api.logical_interface_read(id=right_pnf_li_uuid)
        left_spine_li_obj = self.api.logical_interface_read(
            id=left_spine_li_uuid)
        right_spine_li_obj = self.api.logical_interface_read(
            id=right_spine_li_uuid)
        lo_pnf_li_obj = self.api.logical_interface_read(id=lo_pnf_li_uuid)

        self.assertEqual(len(left_pnf_li_obj.get_instance_ip_back_refs()), 1)
        self.assertEqual(len(right_pnf_li_obj.get_instance_ip_back_refs()), 1)
        self.assertEqual(len(left_spine_li_obj.get_instance_ip_back_refs()), 1)
        self.assertEqual(
            len(right_spine_li_obj.get_instance_ip_back_refs()), 1)
        self.assertEqual(len(lo_pnf_li_obj.get_instance_ip_back_refs()), 1)

        left_pnf_iip_uuid = left_pnf_li_obj.get_instance_ip_back_refs()[
            0].get('uuid')
        right_pnf_iip_uuid = right_pnf_li_obj.get_instance_ip_back_refs()[
            0].get('uuid')
        left_spine_iip_uuid = left_spine_li_obj.get_instance_ip_back_refs()[
            0].get('uuid')
        right_spine_iip_uuid = right_spine_li_obj.get_instance_ip_back_refs()[
            0].get('uuid')
        lo_pnf_iip_uuid = lo_pnf_li_obj.get_instance_ip_back_refs()[
            0].get('uuid')

        self.assertIsNotNone(
            self.api.instance_ip_read(
                id=left_pnf_iip_uuid).instance_ip_address)
        self.assertIsNotNone(self.api.instance_ip_read(
            id=right_pnf_iip_uuid).instance_ip_address)
        self.assertIsNotNone(self.api.instance_ip_read(
            id=left_spine_iip_uuid).instance_ip_address)
        self.assertIsNotNone(self.api.instance_ip_read(
            id=right_spine_iip_uuid).instance_ip_address)
        self.assertIsNotNone(
            self.api.instance_ip_read(
                id=lo_pnf_iip_uuid).instance_ip_address)

        self.api.port_tuple_delete(id=pt_obj.uuid)
        pass
