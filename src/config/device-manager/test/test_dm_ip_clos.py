#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
from attrdict import AttrDict

from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *

from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleUnderlayIpClosDM(TestAnsibleCommonDM):

    def test_dm_ansible_underlay_ip_clos(self):
        self.create_feature_objects_and_params()
        jt1 = self.create_job_template('job-template-1')
        jt2 = self.create_job_template('job-template-2')
        name = "test_ipclos"
        fabric_uuid = self.create_fabric(name)
        fabric = self._vnc_lib.fabric_read(id=fabric_uuid)
        asn_ranges = [{'asn_min': 64501, 'asn_max': 64601}]
        ns_fq_name = fabric.fq_name + ["test_ipclos_ns_eBGP-ASN-pool"]
        fabric_namespace = FabricNamespace(
            name='test_ipclos_ns_eBGP-ASN-pool',
            fq_name=ns_fq_name,
            parent_type='fabric',
            fabric_namespace_type='ASN_RANGE',
            fabric_namespace_value=NamespaceValue(asn_ranges=asn_ranges)
        )
        tag_obj = Tag(
            name="label=fabric-ebgp-as-number",
            tag_type_name="label",
            tag_value="fabric-ebgp-as-number",
            tag_predefined=True)
        self._vnc_lib.tag_create(tag_obj)
        fabric_namespace.add_tag(tag_obj)
        self._vnc_lib.fabric_namespace_create(fabric_namespace)
        np1, rc1 = self.create_node_profile(
            'node-profile-1',
            device_family='junos-qfx',
            role_mappings=[
            AttrDict(
                {'physical_role': 'spine',
                 'rb_roles': ['crb-gateway']}
            )], job_template=jt1)
        np2, rc2 = self.create_node_profile(
            'node-profile-2', device_family='junos-qfx',
            role_mappings=[
            AttrDict(
                {'physical_role': 'leaf',
                 'rb_roles': ['crb-access']}
            )], job_template=jt2)
        pr1_mgmtip = '10.10.0.1'
        pr2_mgmtip = '10.10.0.2'
        bgp_router1, pr1 = self.create_router(
            'router1', pr1_mgmtip, role='spine',
            ignore_bgp=False, fabric=fabric,
            node_profile=np1,
            physical_role=self.physical_roles['spine'],
            overlay_role=self.overlay_roles[
                'crb-gateway'],
            rb_roles=['crb-gateway'],
            product='qfx10k', family='junos-qfx')
        bgp_router2, pr2 = self.create_router(
            'router2', pr2_mgmtip, role='leaf',
            ignore_bgp=False, fabric=fabric,
            node_profile=np2,
            physical_role=self.physical_roles['leaf'],
            overlay_role=self.overlay_roles[
                'crb-access'],
            rb_roles=['crb-access'],
            product='qfx5110', family='junos-qfx')
        pr1.set_physical_router_underlay_managed(True)
        pr2.set_physical_router_underlay_managed(True)
        pr1.set_physical_router_loopback_ip('1.1.1.1')
        pr2.set_physical_router_loopback_ip('1.1.1.2')
        self._vnc_lib.physical_router_update(pr1)
        self._vnc_lib.physical_router_update(pr2)
        pi1 = PhysicalInterface(name='xe-0/0/0', parent_obj=pr1)
        pi2 = PhysicalInterface(name='xe-0/0/0', parent_obj=pr2)
        self._vnc_lib.physical_interface_create(pi1)
        self._vnc_lib.physical_interface_create(pi2)
        pi1.set_physical_interface(pi2)
        pi2.set_physical_interface(pi1)
        self._vnc_lib.physical_interface_update(pi1)
        self._vnc_lib.physical_interface_update(pi2)
        subnet1 = SubnetType(ip_prefix='192.168.2.0', ip_prefix_len=24)
        net_ipam1 = NetworkIpam()
        li1 = LogicalInterface(name='0', parent_obj=pi1)
        self._vnc_lib.logical_interface_create(li1)
        li2 = LogicalInterface(name='0', parent_obj=pi2)
        self._vnc_lib.logical_interface_create(li2)
        vn_obj1 = VirtualNetwork(
            name='vn1',
            virtual_network_category='infra',
            address_allocation_mode='flat-subnet-only')
        vnsubnet_obj = VnSubnetsType(
            ipam_subnets=[
                IpamSubnetType(
                    subnet=subnet1)])
        self._vnc_lib.virtual_network_create(vn_obj1)
        vn_obj1.set_network_ipam(net_ipam1, vnsubnet_obj)
        self._vnc_lib.virtual_network_update(vn_obj1)
        ins_ip1 = InstanceIp(
            name='ip1',
            instance_ip_address='192.168.2.1',
            instance_ip_family='v4')
        ins_ip1.set_logical_interface(li1)
        ins_ip1.set_virtual_network(vn_obj1)
        self._vnc_lib.instance_ip_create(ins_ip1)
        ins_ip2 = InstanceIp(
            name='ip2',
            instance_ip_address='192.168.2.2',
            instance_ip_family='v4')
        ins_ip2.set_logical_interface(li2)
        ins_ip2.set_virtual_network(vn_obj1)
        self._vnc_lib.instance_ip_create(ins_ip2)
        pr1.set_physical_router_product_name('qfx10k-6s-4c')
        self._vnc_lib.physical_router_update(pr1)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config1 = abstract_config.get('device_abstract_config')
        pr2.set_physical_router_product_name('qfx5110-6s-4c')
        self._vnc_lib.physical_router_update(pr2)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config2 = abstract_config.get('device_abstract_config')
        system1 = device_abstract_config1.get('system')
        bgp_config1 = device_abstract_config1.get('features', {}).get(
            'underlay-ip-clos', {}).get('bgp', [])
        bgp_peer1 = bgp_config1[0].get('peers', [])
        system2 = device_abstract_config2.get('system')
        bgp_config2 = device_abstract_config2.get('features', {}).get(
            'underlay-ip-clos', {}).get('bgp', [])
        bgp_peer2 = bgp_config2[0].get('peers', [])
        self.assertEqual(bgp_config1[0].get('ip_address'), '192.168.2.1')
        self.assertEqual(bgp_peer1[0].get('ip_address'), '192.168.2.2')
        self.assertEqual(bgp_config2[0].get('ip_address'), '192.168.2.2')
        self.assertEqual(bgp_peer2[0].get('ip_address'), '192.168.2.1')
        self.delete_objects()

    def create_feature_objects_and_params(self):
        self.create_features(['underlay-ip-clos'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway', 'crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway@spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['underlay-ip-clos'],
                'feature_configs': {}
            }),
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['underlay-ip-clos'],
                'feature_configs': {}
            })
        ])

    def create_fabric(self, name):
        fab = Fabric(
            name=name,
            manage_underlay=True,
            fabric_enterprise_style=False,
            fabric_ztp=True,
            fabric_credentials={
                'device_credential': [{
                    'credential': {
                        'username': 'root', 'password': '123'
                    },
                    'vendor': 'Juniper',
                    'device_family': None
                }]
            }
        )
        fab_uuid = self._vnc_lib.fabric_create(fab)
        return fab_uuid

    def delete_objects(self):
        vpg_list = self._vnc_lib.virtual_port_groups_list().get(
            'virtual-port-groups')
        for vpg in vpg_list:
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg['uuid'])
            vpg_obj.set_virtual_machine_interface_list([])
            vpg_obj.set_physical_interface_list([])
            self._vnc_lib.virtual_port_group_update(vpg_obj)
        vmi_list = self._vnc_lib.virtual_machine_interfaces_list().get(
            'virtual-machine-interfaces', [])
        for vmi in vmi_list:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])
        instance_ip_list = self._vnc_lib.instance_ips_list().get(
            'instance-ips', [])
        for ip in instance_ip_list:
            self._vnc_lib.instance_ip_delete(id=ip['uuid'])
        logical_interfaces_list = self._vnc_lib.logical_interfaces_list().get(
            'logical-interfaces', [])
        for logical_interface in logical_interfaces_list:
            self._vnc_lib.logical_interface_delete(id=logical_interface['uuid'])
        pi_list = self._vnc_lib.physical_interfaces_list().get(
            'physical-interfaces', [])
        for pi in pi_list:
            self._vnc_lib.physical_interface_delete(id=pi['uuid'])
        for vpg in vpg_list:
            self._vnc_lib.virtual_port_group_delete(id=vpg['uuid'])
        pr_list = self._vnc_lib.physical_routers_list().get(
            'physical-routers', [])
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])
        bgpr_list = self._vnc_lib.bgp_routers_list().get('bgp-routers', [])
        for br in bgpr_list:
            self._vnc_lib.bgp_router_delete(id=br['uuid'])
        rc_list = self._vnc_lib.role_configs_list().get('role-configs', [])
        for rc in rc_list:
            self._vnc_lib.role_config_delete(id=rc['uuid'])
        ports_list = self._vnc_lib.ports_list().get('ports', [])
        for port in ports_list:
            self._vnc_lib.port_delete(id=port['uuid'])
        nodes_list = self._vnc_lib.nodes_list().get('nodes', [])
        for node in nodes_list:
            self._vnc_lib.node_delete(id=node['uuid'])
        fabric_namespace_list = self._vnc_lib.fabric_namespaces_list().get(
            'fabric-namespaces', [])
        for fabric_namespace in fabric_namespace_list:
            self._vnc_lib.fabric_namespace_delete(id=fabric_namespace['uuid'])
        fab_list = self._vnc_lib.fabrics_list().get('fabrics', [])
        for fab in fab_list:
            self._vnc_lib.fabric_delete(id=fab['uuid'])
        vn_list = self._vnc_lib.virtual_networks_list().get(
            'virtual-networks', [])
        for vn in vn_list:
            self._vnc_lib.virtual_network_delete(id=vn['uuid'])
        np_list = self._vnc_lib.node_profiles_list().get('node-profiles', [])
        for np in np_list:
            self._vnc_lib.node_profile_delete(id=np['uuid'])
        jt_list = self._vnc_lib.job_templates_list().get('job-templates', [])
        for jt in jt_list:
            self._vnc_lib.job_template_delete(id=jt['uuid'])
        pp_list = self._vnc_lib.port_profiles_list().get('port-profiles', [])
        for pp in pp_list:
            self._vnc_lib.port_profile_delete(id=pp['uuid'])
        sc_list = self._vnc_lib.storm_control_profiles_list().get(
            'storm-control-profiles', [])
        for sc in sc_list:
            self._vnc_lib.storm_control_profile_delete(id=sc['uuid'])
        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
