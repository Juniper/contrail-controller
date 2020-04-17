#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import json
import gevent
from attrdict import AttrDict

from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *

from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleInfraBMSAccessDM(TestAnsibleCommonDM):

    def test_infra_bms_access_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['infra-bms-access'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['infra-bms-access'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-bms-infra-access')
            fabric = self.create_fabric(
                'test-bms-infra-access',
                fabric_enterprise_style=False)
            fabric.set_fabric_ztp(True)
            self._vnc_lib.fabric_update(fabric)
            np, rc = self.create_node_profile('node-profile-qfx',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)
            bgp_router, pr = self.create_router('router' + self.id(), '10.10.1.2',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'],
                                                fabric=fabric,
                                                node_profile=np)
            switch_info = {}
            switch_info['name'] = 'router'
            switch_info['port'] = 'xe-0/0/1'
            switch_info['mac'] = 'e4:fc:82:38:83:40'
            pr.set_physical_router_loopback_ip('20.20.0.1')
            pi1_name = "xe-0/0/1"
            pi1_obj = PhysicalInterface(pi1_name, parent_obj=pr)
            self._vnc_lib.physical_interface_create(pi1_obj)
            tag_obj = self.create_tag('infra-vn')
            vn_obj = self.create_virtual_network('infra-vn', 220,
                                                 '192.168.251.0',
                                                 24, '192.168.251.1', tag_obj)
            end_server, port_obj, end_server_profile = \
                self.create_end_server(name='node1',
                                       ip_address='10.10.1.3',
                                       mac_address='90:e2:ba:50:ad:f8',
                                       interface_name='eth2',
                                       switch_info=switch_info,
                                       tag_obj=tag_obj)
            pi1_obj.set_port(port_obj)
            self._vnc_lib.physical_interface_update(pi1_obj)
            fabric.add_node_profile(end_server_profile)
            self._vnc_lib.physical_router_update(pr)
            self._vnc_lib.fabric_update(fabric)
            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()
            device_abstract_config = \
                abstract_config.get('device_abstract_config')
            vlans = device_abstract_config.get('features', {}).get(
                'infra-bms-access', {}).get('vlans', {})
            self.assertEqual(vlans[0].get('vlan_id'), 220)
            self.assertEqual(vlans[0].get('l3_interface'), 'irb.220')
            phy_intf = self.get_phy_interfaces(
                device_abstract_config.get(
                    'features', {}).get(
                        'infra-bms-access', {}), name='xe-0/0/1')
            irb_intf = self.get_phy_interfaces(
                device_abstract_config.get('features', {}).get(
                    'infra-bms-access', {}), name='irb')
            logical_intf_vlans = phy_intf.get(
                'logical_interfaces', [])[0].get('vlans', [])
            irb_logical_intf = irb_intf.get('logical_interfaces', [])[0]
            irb_ip_addresses = irb_logical_intf.get('ip_addresses')[0]
            self.assertEqual(logical_intf_vlans[0].get(
                'name'), vlans[0].get('name'))
            self.assertEqual(irb_intf.get('interface_type'), 'irb')
            self.assertEqual(irb_ip_addresses.get('address'),
                             '192.168.251.1/24')
            self.assertEqual(irb_logical_intf.get('name'), 'irb.220')
            self.assertEqual(irb_logical_intf.get('unit'), 220)
        except Exception as e:
            print(e)
        finally:
            self.delete_objects()

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
        hardwares_list = self._vnc_lib.hardwares_list().get('hardwares', [])
        for hardware in hardwares_list:
            self._vnc_lib.hardware_delete(id=hardware['uuid'])
        card_list = self._vnc_lib.cards_list().get('cards', [])
        for card in card_list:
            self._vnc_lib.card_delete(id=card['uuid'])
        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()

    def create_network_ipam(self, name, ip_prefix, ip_prefix_length,
                            ipam_subnet_method, vlan_tag, default_gateway):
        subnet = SubnetType(ip_prefix=ip_prefix,
                            ip_prefix_len=ip_prefix_length)
        ipam_subnet_type = IpamSubnetType(subnet=subnet,
                                          vlan_tag=vlan_tag,
                                          default_gateway=default_gateway)
        ipam_subnets = IpamSubnets(subnets=[ipam_subnet_type])
        network_ipam_obj = NetworkIpam(name=name, fq_name=[
            'default-domain', 'default-project', name],
                                       parent_type='project',
                                       ipam_subnets=ipam_subnets,
                                       ipam_subnet_method='flat-subnet')
        network_ipam_obj.set_ipam_subnets(ipam_subnets)
        network_ipam_obj.set_ipam_subnet_method('flat-subnet')
        netipam_obj = self._vnc_lib.network_ipam_create(network_ipam_obj)
        vnsubnet_obj = VnSubnetsType()
        return network_ipam_obj, vnsubnet_obj

    def create_tag(self, name):
        tag_type_obj = self._vnc_lib.tag_type_read(fq_name=["label"])
        tag_obj = Tag(name=name, fq_name=["label="+name],
                      tag_type_name='label',
                      tag_value=name, tag_predefined=False,
                      display_name="label="+name)
        self._vnc_lib.tag_create(tag_obj)
        return tag_obj

    def create_end_server(self, name, ip_address, mac_address,
                          interface_name, switch_info, tag_obj):
        serv_obj = Node(name=name, node_type='baremetal',
                        ip_address=ip_address,
                        parent_type='global-system-config',
                        hostname=name, display_name=name)
        self._vnc_lib.node_create(serv_obj)
        switch_name = switch_info['name']
        switch_port = switch_info['port']
        switch_mac = switch_info['mac']
        port_obj = self.create_bms_port(
            name=name,
            switch_name=switch_name,
            switch_port=switch_port,
            switch_mac=switch_mac,
            mac_address=mac_address,
            interface_name=interface_name,
            server_obj=serv_obj)
        port_obj.set_tag(tag_obj)
        self._vnc_lib.port_update(port_obj)
        node_profile_obj = self.create_end_system_profile(
            name=name,
            type='end-system',
            interface_name=interface_name,
            tag_value=tag_obj.get_tag_value())
        serv_obj.set_node_profile(node_profile_obj)
        self._vnc_lib.node_update(serv_obj)
        return serv_obj, port_obj, node_profile_obj

    def create_bms_port(self, name, switch_name, switch_port, switch_mac,
                        mac_address, interface_name, server_obj):
        localconnection = LocalLinkConnection(switch_info='router',
                                              port_index='xe-0/0/1',
                                              switch_id='0:00:b7:30:00:00')
        bmsportinfo = BaremetalPortInfo(local_link_connection=localconnection,
                                        address=mac_address)
        access_port = Port(name=interface_name,
                           parent_obj=server_obj,
                           bms_portinfo=bmsportinfo)
        port_obj = self._vnc_lib.port_create(access_port)
        return access_port

    def create_virtual_network(self, name, vlan_id, ip_prefix,
                               ip_prefix_length, gateway, tag_obj):
        vn_property = VirtualNetworkType(vxlan_network_identifier=vlan_id,
                                         forwarding_mode='l3')
        vn_obj = VirtualNetwork(
            fq_name=['default-domain', 'default-project', name],
            parent_type='project',
            virtual_network_category='infra',
            address_allocation_mode='flat-subnet-only',
            display_name=name)
        vn_obj.set_virtual_network_properties(vn_property)
        self._vnc_lib.virtual_network_create(vn_obj)
        net_ipam_obj, vn_subnet_obj = self.create_network_ipam(
            name + '-networkipam',
            ip_prefix, ip_prefix_length=24,
            ipam_subnet_method="flat-subnet",
            vlan_tag=vlan_id,
            default_gateway=gateway)
        vn_obj.add_network_ipam(net_ipam_obj, vn_subnet_obj)
        vn_obj.add_tag(tag_obj)
        self._vnc_lib.virtual_network_update(vn_obj)
        return vn_obj

    def create_end_system_profile(self, name, type, interface_name,
                                  tag_value):
        hardware_obj = Hardware(fq_name=['dell-bms'], name='dell-bms')
        self._vnc_lib.hardware_create(hardware_obj)
        port_info_obj = PortInfoType(name=interface_name, labels=[tag_value])
        interface_map_obj = InterfaceMapType(port_info=[port_info_obj])
        card_obj = Card(fq_name=['dell-bms-card'],
                        interface_map=interface_map_obj)
        self._vnc_lib.card_create(card_obj)
        hardware_obj.add_card(card_obj)
        self._vnc_lib.hardware_update(hardware_obj)
        node_profile_obj = NodeProfile(
            name='Dell-Infra-server',
            node_profile_type='end-system',
            node_profile_vendor='Dell',
            parent_type='global-system-config',
            display_name='Dell-Infra-server')
        self._vnc_lib.node_profile_create(node_profile_obj)
        node_profile_obj.add_hardware(hardware_obj)
        self._vnc_lib.node_profile_update(node_profile_obj)
        return node_profile_obj