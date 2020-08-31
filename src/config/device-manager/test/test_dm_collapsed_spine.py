#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import uuid
import copy
from attrdict import AttrDict

from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *

from .test_dm_ansible_common import TestAnsibleCommonDM


class TestAnsibleCollapsedSpineDM(TestAnsibleCommonDM):

    def create_feature_objects_and_params(self):
        self.create_features(['overlay-bgp', 'l2-gateway', 'l3-gateway',
                              'vn-interconnect', 'firewall', 'port-profile'])
        self.create_physical_roles(['spine'])
        self.create_overlay_roles(['collapsed-spine'])

        self.create_role_definitions([
            AttrDict({
                'name': 'collapsed-spine@spine',
                'physical_role': 'spine',
                'overlay_role': 'collapsed-spine',
                'features': ['overlay-bgp', 'l2-gateway', 'l3-gateway',
                             'vn-interconnect', 'firewall', 'port-profile'],
                'feature_configs': {}
            })
        ])

    @retries(5, hook=retry_exc_handler)
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

    def test_dm_collapsed_spine(self):
        self.create_feature_objects_and_params()

        name = "test_collapsed_spine"
        jt = self.create_job_template('job-template-' + name + self.id())
        fabric = self.create_fabric('fab-' + name + self.id())

        role = 'spine'
        spine_rb_role = 'collapsed-spine'
        np, rc = self.create_node_profile(
            'node-profile-' + name + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {
                        'physical_role': role,
                        'rb_roles': [spine_rb_role]
                    }
                )],
            job_template=jt)

        # create physical router
        br, pr = self.create_router(
            'device-1' + self.id(), '7.7.7.7', product='qfx10002',
            family='junos-qfx', role='spine',
            rb_roles=[spine_rb_role],
            physical_role=self.physical_roles[role],
            overlay_role=self.overlay_roles[spine_rb_role],
            fabric=fabric, node_profile=np)
        pr.set_physical_router_loopback_ip('30.30.0.22')
        self._vnc_lib.physical_router_update(pr)
        gevent.sleep(2)

        # create physical interface
        pi = PhysicalInterface(name='xe-0/0/1', parent_obj=pr)
        self._vnc_lib.physical_interface_create(pi)
        self._vnc_lib.physical_interface_update(pi)

        # create storm control profile and port profile
        sc_name = 'storm_ctrl'
        bw_percent = 90
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        # Port Parameters
        port_desc = "sample port desc"
        port_mtu = 340
        port_disable = False
        flow_control = True
        lacp_enable = True
        lacp_interval = "fast"
        lacp_mode = "active"
        bpdu_loop_protection = False
        qos_cos = True
        sc_obj = self.create_storm_control_profile(
            sc_name, bw_percent, traffic_type, actions, recovery_timeout=None)
        pp_params = PortProfileParameters(
            bpdu_loop_protection=bpdu_loop_protection,
            flow_control=flow_control,
            lacp_params=LacpParams(
                lacp_enable=lacp_enable,
                lacp_interval=lacp_interval,
                lacp_mode=lacp_mode
            ),
            port_cos_untrust=qos_cos,
            port_params=PortParameters(
                port_disable=port_disable,
                port_mtu=port_mtu,
                port_description=port_desc
            )
        )
        pp = self.create_port_profile('port_profile_vmi', sc_obj, pp_params)

        # create security group
        sg = self.create_sg("sg1")

        # create virtual network, virtual machine interface, virtual port group and logical router
        vn1_obj = self.create_vn('1', '1.1.1.0')
        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing')
        lr.add_physical_router(pr)

        # create vmi
        vmi_name = 'vmi-lr-to-vn' + vn1_obj.get_display_name()
        vpg_name = 'vpg-' + vn1_obj.get_display_name()
        vmi = VirtualMachineInterface(fq_name=['default-domain', 'default-project', vmi_name],
                                      parent_type='project')
        device_name = pr.get_fq_name()[-1]
        fabric_name = fabric.get_fq_name()[-1]
        phy_int_name = pi.get_fq_name()[-1]
        vmi_profile = \
            "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % \
            (phy_int_name, phy_int_name, device_name, fabric_name)
        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "vpg",
                "value": vpg_name
            }, {
                "key": "profile",
                "value": vmi_profile
            }]
        }
        vmi.set_virtual_machine_interface_bindings(vmi_bindings)
        vmi_properties = {"sub_interface_vlan_tag": 100}
        vmi.set_virtual_machine_interface_properties(vmi_properties)
        vmi.set_virtual_network(vn1_obj)

        # create vpg
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric)

        # set port profile and security group to the vpg
        vpg.set_port_profile(pp)
        vpg.set_security_group(sg)
        self._vnc_lib.virtual_port_group_create(vpg)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        self._vnc_lib.virtual_machine_interface_update(vmi)
        vpg.set_virtual_machine_interface_list([{'uuid': vmi.get_uuid()}])
        self._vnc_lib.virtual_port_group_update(vpg)
        self._vnc_lib.physical_router_update(pr)

        lr.add_virtual_machine_interface(vmi)
        lr.set_logical_router_type('vxlan-routing')
        lr.set_physical_router(pr)
        self._vnc_lib.logical_router_create(lr)
        self._vnc_lib.logical_router_update(lr)

        # create logical interface
        li = LogicalInterface('li', parent_obj=pi)
        li.vlan_tag = 100
        li.set_virtual_machine_interface(vmi)
        self._vnc_lib.logical_interface_create(li)
        self._vnc_lib.physical_interface_update(pi)
        self._vnc_lib.physical_router_update(pr)
        gevent.sleep(4)

        self.validate_abstract_config()

    @retries(5, hook=retry_exc_handler)
    def validate_abstract_config(self):
        ac = self.check_dm_ansible_config_push()
        device_abstract_config = ac.get('device_abstract_config')

        ri_l2 = device_abstract_config.get('features').get('l2-gateway').get('routing_instances', [])[0]
        self.assertEqual(ri_l2.get('vxlan_id'), '2001')

        phy_int_l2 = device_abstract_config.get('features').get('l2-gateway').get('physical_interfaces', [])[0]
        self.assertEqual(phy_int_l2.get('name'), 'xe-0/0/1')
        self.assertEqual(phy_int_l2.get('logical_interfaces')[0].get('vlan_tag'), '100')

        ri_l3 = device_abstract_config.get('features').get('l3-gateway').get('routing_instances', [])[0]
        self.assertEqual(ri_l3.get('prefixes', [])[0].get('prefix'), '1.1.1.0')
        self.assertEqual(ri_l3.get('routing_instance_type'), 'vrf')
        self.assertEqual(ri_l3.get('vxlan_id'), '2001')

        vlan = device_abstract_config.get('features').get('l3-gateway').get('vlans', [])[0]
        self.assertEqual(vlan.get('interfaces', [])[0].get('name'), 'irb.5')

        phy_int_l3 = device_abstract_config.get('features').get('l3-gateway').get('physical_interfaces', [])[0]
        self.assertIsNotNone(phy_int_l3.get('logical_interfaces', [])[0].get('ip_addresses', [])[0].get('address'))

        ri_l2 = device_abstract_config.get('features').get('l2-gateway').get('routing_instances', [])[0]
        self.assertEqual(ri_l2.get('vxlan_id'), '2001')

        pp_phy_int = device_abstract_config.get('features').get('port-profile').get('physical_interfaces', [])[0]
        self.assertEqual(pp_phy_int.get('port_profile'), 'port_profile_vmi-default-project')
        self.assertEqual(pp_phy_int.get('name'), 'xe-0/0/1')

        pp_port_profile = device_abstract_config.get('features').get('port-profile').get('port_profile', [])[0]
        self.assertEqual(pp_port_profile.get('name'), 'port_profile_vmi-default-project')
        self.assertEqual(pp_port_profile.get('flow_control'), True)
        self.assertEqual(pp_port_profile.get('bpdu_loop_protection'), False)

        st_profile = pp_port_profile.get('storm_control_profile')
        self.assertEqual(st_profile.get('name'), 'storm_ctrl-default-project')
        self.assertEqual(st_profile.get('bandwidth_percent'), 90)
        self.assertEqual(st_profile.get('actions')[0], 'interface-shutdown')

        port_params = pp_port_profile.get('port_params')
        self.assertEqual(port_params.get('port_disable'), False)
        self.assertEqual(port_params.get('port_mtu'), 340)

        lacp_params = pp_port_profile.get('lacp_params')
        self.assertEqual(lacp_params.get('lacp_mode'), 'active')
        self.assertEqual(lacp_params.get('lacp_interval'), 'fast')
        self.assertEqual(lacp_params.get('lacp_enable'), True)

        fw = device_abstract_config.get('features').get('firewall')
        fw_phy_int = fw.get('physical_interfaces')[0]
        self.assertEqual(fw_phy_int.get('name'), 'xe-0/0/1')
        self.assertEqual(fw_phy_int.get('logical_interfaces')[0].get('vlan_tag'), '100')
        self.assertEqual(fw_phy_int.get('logical_interfaces')[0].get('is_tagged'), True)

        fw_fromxx = fw.get('firewall').get('firewall_filters')[0].get('terms')[2].get('fromxx')
        self.assertEqual(fw_fromxx.get('ether_type'), 'IPv4')
        self.assertEqual(fw_fromxx.get('destination_address')[0].get('prefix'), '5.5.5.0')
        self.assertEqual(fw_fromxx.get('destination_address')[0].get('prefix_len'), 24)

    def create_sg(self, name):
        # create sg and associate egress rule
        sg_obj = self.security_group_create('sg-1', ['default-domain',
                                                     'default-project'])
        self.wait_for_get_sg_id(sg_obj.get_fq_name())

        sg_obj = self._vnc_lib.security_group_read(sg_obj.get_fq_name())
        rule1 = self.build_rule(0, 65535, 'egress', '5.5.5.0/24', 'any', 'IPv4')
        sg_rule1 = self.security_group_rule_build(rule1,
                                                  sg_obj.get_fq_name_str())
        self.security_group_rule_append(sg_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg_obj)
        return sg_obj

    def security_group_create(self, sg_name, project_fq_name):
        project_obj = self._vnc_lib.project_read(project_fq_name)
        sg_obj = SecurityGroup(name=sg_name, parent_obj=project_obj)
        self._vnc_lib.security_group_create(sg_obj)
        gevent.sleep(2)
        return sg_obj

    def security_group_rule_append(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            rules = PolicyEntriesType([sg_rule])
        else:
            for sgr in rules.get_policy_rule() or []:
                sgr_copy = copy.copy(sgr)
                sgr_copy.rule_uuid = sg_rule.rule_uuid
                if sg_rule == sgr_copy:
                    raise Exception('SecurityGroupRuleExists %s' % sgr.rule_uuid)
            rules.add_policy_rule(sg_rule)

        sg_obj.set_security_group_entries(rules)

    def security_group_rule_build(self, rule_info, sg_fq_name_str):
        protocol = rule_info['protocol']
        port_min = rule_info['port_min'] or 0
        port_max = rule_info['port_max'] or 65535
        direction = rule_info['direction'] or 'egress'
        ip_prefix = rule_info['ip_prefix']
        ether_type = rule_info['ether_type']

        if ip_prefix:
            cidr = ip_prefix.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            endpt = [AddressType(subnet=SubnetType(pfx, pfx_len))]
        else:
            endpt = [AddressType(security_group=sg_fq_name_str)]

        local = None
        remote = None
        if direction == 'ingress':
            dir = '>'
            local = endpt
            remote = [AddressType(security_group='local')]
        else:
            dir = '>'
            remote = endpt
            local = [AddressType(security_group='local')]

        if not protocol:
            protocol = 'any'
        if protocol.isdigit():
            protocol = int(protocol)
            if protocol < 0 or protocol > 255:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)
        else:
            if protocol not in ['any', 'tcp', 'udp', 'icmp']:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)

        if not ip_prefix and not sg_fq_name_str:
            if not ether_type:
                ether_type = 'IPv4'

        sgr_uuid = str(uuid.uuid4())
        rule = PolicyRuleType(rule_uuid=sgr_uuid, direction=dir,
                                  protocol=protocol,
                                  src_addresses=local,
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=remote,
                                  dst_ports=[PortType(port_min, port_max)],
                                  ethertype=ether_type)
        return rule

    def build_rule(self, pmin, pmax, direction, ip_prefix, proto, etype):
        rule = dict()
        rule['port_min'] = pmin
        rule['port_max'] = pmax
        rule['direction'] = direction
        rule['ip_prefix'] = ip_prefix
        rule['protocol'] = proto
        rule['ether_type'] = etype
        return rule

    @retries(5)
    def wait_for_get_sg_id(self, sg_fq_name):
        sg_obj = self._vnc_lib.security_group_read(sg_fq_name)
        if sg_obj.get_security_group_id() is None:
            raise Exception('Security Group Id is none %s' % str(sg_fq_name))
