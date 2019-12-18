#
# Copyright (c) 2017 Juniper Infra, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
import sys
import uuid
from unittest import skip
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from gevent import monkey
monkey.patch_all()
sys.path.append('../common/cfgm_common/tests/mocked_libs')
from device_manager.db import DMCassandraDB
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_common import *
from .test_dm_utils import FakeDeviceConnect
from .test_dm_utils import FakeNetconfManager

#
# All QFX DM  Basic Test Cases should go here
#
class TestQfxBasicDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        self.product = "qfx5110"
        super(TestQfxBasicDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_switch_options_config(self, vn_obj, vn_mode, role):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, vn_mode)

        config = FakeDeviceConnect.get_xml_config()
        switch_opts = config.get_switch_options()
        self.assertIsNotNone(switch_opts)
        self.assertEqual(switch_opts.get_vtep_source_interface(), "lo0.0")
        import_name = DMUtils.make_import_name(vrf_name)
        imports = switch_opts.get_vrf_import() or []
        if (role =='spine' and vn_mode == 'l2' and '_contrail_lr_internal_vn' in vrf_name) or \
                       (role =='leaf' and vn_mode == 'l3'):
            self.assertNotIn(import_name, imports)
        else:
            self.assertIn(import_name, imports)
        export_name = DMUtils.make_export_name(vrf_name)
        exports = switch_opts.get_vrf_export() or []
        # export policy is applicable only for spine/l3
        if (role =='spine' and vn_mode == 'l2' and '_contrail_lr_internal_vn' in vrf_name) or \
                        (role =='leaf'):
            self.assertNotIn(export_name, exports)
        else:
            self.assertIn(export_name, exports)
    # end check_switch_options_config

    @retries(5, hook=retry_exc_handler)
    def check_switch_options_export_policy_config(self, should_present=True):
        config = FakeDeviceConnect.get_xml_config()
        switch_opts = config.get_switch_options()
        self.assertIsNotNone(switch_opts)
        exports = switch_opts.get_vrf_export() or []
        export_name = DMUtils.get_switch_export_policy_name()
        if not should_present:
            self.assertNotIn(export_name, exports)
        else:
            self.assertIn(export_name, exports)
    # end check_switch_options_config

    @retries(5, hook=retry_exc_handler)
    def check_policy_options_config(self, should_present=True):
        config = FakeDeviceConnect.get_xml_config()
        try:
            policy_opts = config.get_policy_options()
            self.assertIsNotNone(policy_opts)
            comms = policy_opts.get_community() or []
            self.assertIsNotNone(comms)
            comm_name = DMUtils.get_switch_export_community_name()
            if comm_name not in  [comm.name for comm in comms]:
                raise Exception("comm name : " + comm_name + " not found")
        except Exception as e:
            if not should_present:
                return
            raise Exception("Policy Options not found: " + str(e))
    # end check_policy_options_config

    @retries(5, hook=retry_exc_handler)
    def check_ri_config(self, vn_obj, vn_mode='l2', check=True):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                  vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, vrf_name)
        if not check and ris:
            raise Exception("Routing Instances Present for: " + vrf_name)
        if check and not ris:
            raise Exception("Routing Instances not Present for: " + vrf_name)
    # end check_ri_config

    @retries(5, hook=retry_exc_handler)
    def check_ri_vlans_config(self, vn_obj, vni, vn_mode='l3', check=True):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                  vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, vrf_name)
        ri = None
        vlans = None
        try:
            ri = ris[0]
            vlans = ri.get_vlans().get_vlan() or []
        except:
            if check:
                raise Exception("RI Vlan Config Not Found: %s"%(vrf_name))
        if not vlans and not check:
            return
        vlan_name = vrf_name[1:]
        for vlan in vlans:
            if vlan.get_name() == vlan_name and vlan.get_vlan_id() == str(vni):
                if not check:
                    raise Exception("RI Vlan Config Found: %s"%(vrf_name))
                return
        if check:
            raise Exception("RI Vlan Config Not Found: %s"%(vlan_name))

    @retries(5, hook=retry_exc_handler)
    def check_vlans_config(self, vn_obj, vni, vn_mode):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        vlans = config.get_vlans()
        self.assertIsNotNone(vlans)
        vlans = vlans.get_vlan() or []
        vlan_name = vrf_name[1:]
        for vlan in vlans:
            if vlan.get_name() == vlan_name and str(vlan.get_vxlan().get_vni()) == str(vni):
                return
        raise Exception("Vlan Config Not Found: %s"%(vlan_name))

    @retries(5, hook=retry_exc_handler)
    def check_lr_internal_vn_state(self, lr_obj):
        internal_vn_name = DMUtils.get_lr_internal_vn_name(lr_obj.uuid)
        vn_fq = lr_obj.get_fq_name()[:-1] + [internal_vn_name]
        vn_obj = None
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq)
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if not vn_obj_properties:
            raise Exception("LR Internal VN properties are not set")
        fwd_mode = vn_obj_properties.get_forwarding_mode()
        if fwd_mode != 'l3':
            raise Exception("LR Internal VN Forwarding mode is not set to L3")
        return vn_obj
    # end check_lr_internal_vn_state

    def create_global_vrouter_config(self):
        fq = ["default-global-system-config", "default-global-vrouter-config"]
        gv = None
        try:
            gv = self._vnc_lib.global_vrouter_config_read(fq_name=fq)
        except NoIdError:
            gv = GlobalVrouterConfig(fq_name=fq)
            self._vnc_lib.global_vrouter_config_create(gv)
        return gv
    # end create_global_vrouter_config

    @retries(5, hook=retry_exc_handler)
    def check_esi_config(self, pi_name, esi_value, should_present=True):
        config = FakeDeviceConnect.get_xml_config()
        if should_present:
            interfaces = self.get_interfaces(config, pi_name)
            if not interfaces:
                raise Exception("No Interface Config generated")
            found = False
            for intf in interfaces:
                if intf.get_esi() and intf.get_esi().get_all_active() is not None and \
                    intf.get_esi().get_identifier() == esi_value:
                    found = True
                    break
            if not found:
                raise Exception("no interface has esi config: " + pi_name)
        else:
            interfaces = self.get_interfaces(config, pi_name)
            if not interfaces:
                return
            intf = interfaces[0]
            if intf.get_esi():
                raise Exception("ESI Config still exist")
    # end check_esi_config

    @retries(5, hook=retry_exc_handler)
    def check_chassis_config(self):
        config = FakeDeviceConnect.get_xml_config()
        chassis = config.get_chassis()
        if not chassis:
            raise Exception("No Chassis Config generated")
        aggr_dv = chassis.get_aggregated_devices()
        eth = aggr_dv.get_ethernet()
        dv_count = eth.get_device_count()
        device_count =  DMUtils.get_max_ae_device_count()
        self.assertEqual(device_count, dv_count)
    # end check_chassis_config

    @retries(5, hook=retry_exc_handler)
    def check_lacp_config(self, ae_name, esi, pi_list):
        config = FakeDeviceConnect.get_xml_config()
        interfaces = self.get_interfaces(config, ae_name)
        if not interfaces:
            raise Exception("No AE Config generated")
        found = False
        for intf in interfaces:
            if not intf.get_aggregated_ether_options() or not \
                intf.get_aggregated_ether_options().get_lacp():
                continue
            lacp = intf.get_aggregated_ether_options().get_lacp()
            if lacp.get_active() is not None and lacp.get_admin_key() and \
                lacp.get_system_id() and lacp.get_system_priority():
                if esi[-17:] == lacp.get_system_id():
                    found = True
                    break
        if not found:
            raise Exception("AE interface config is not correct: " + esi)
        for pi in pi_list or []:
            interfaces = self.get_interfaces(config, pi)
            found = False
            for intf in interfaces or []:
                if intf.get_gigether_options() and \
                    intf.get_gigether_options().get_ieee_802_3ad():
                    found = True
                    break
            if not found:
                raise Exception("AE membership config not generated: " + pi)
    # end check_lacp_config

    @retries(5, hook=retry_exc_handler)
    def check_l2_evpn_config(self, ae_name, vlan_id):
        config = FakeDeviceConnect.get_xml_config()
        interfaces = self.get_interfaces(config, ae_name)
        if not interfaces:
            raise Exception("No AE Config generated")
        found = False
        for intf in interfaces:
            if intf.get_encapsulation() != "extended-vlan-bridge" or \
                                        not intf.get_unit():
                continue
            unit = intf.get_unit()[0]
            if not unit.get_vlan_id() or str(unit.get_vlan_id()) != str(vlan_id):
                continue
            found = True
            break
        if not found:
            raise Exception("l2 evpn config for ae intf not correct: " + ae_name)
    # end check_l2_evpn_config

    @retries(5, hook=retry_exc_handler)
    def check_l2_evpn_proto_config(self, vn_obj, vni):
        config = FakeDeviceConnect.get_xml_config()
        protocols = config.get_protocols()
        evpn_config = protocols.get_evpn()
        if not evpn_config or not evpn_config.get_vni_options():
            raise Exception("No Correct EVPN Config generated")
        vni_options = evpn_config.get_vni_options()
        for vni_op in vni_options.get_vni() or []:
            if vni_op.name == str(vni) and vni_op.vrf_target.community:
                return
        raise Exception("No Correct EVPN Config generated")
    # end check_l2_evpn_proto_config

    @retries(5, hook=retry_exc_handler)
    def check_l2_evpn_vrf_targets(self, target_id):
        config = FakeDeviceConnect.get_xml_config()
        protocols = config.get_protocols()
        evpn = protocols.get_evpn()
        options = evpn.get_vni_options()
        vni = options.get_vni()[0]
        self.assertEqual(vni.get_vrf_target().get_community(), target_id)
    # end check_l2_evpn_vrf_targets

    def check_spine_bogus_lo0_ip(self, int_vn):
        vrf_name = DMUtils.make_vrf_name(int_vn.fq_name[-1],
                                  int_vn.virtual_network_network_id, "l3")
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, vrf_name)
        if not ris:
            raise Exception("No RI Config found for internal vn: " + vrf_name)
        ri = ris[0]
        intfs = ri.get_interface()
        if not intfs:
            raise Exception("No interfaces Config found for internal vn: " + vrf_name)
        found = False
        ifl_num = str(1000 + int(int_vn.virtual_network_network_id))
        bogus_lo0 = "lo0." + ifl_num
        for intf in intfs:
            if intf.name == bogus_lo0:
                found = True
                break
        if not found:
            raise Exception("No lo0 interface Config found for internal vn: " + vrf_name)

        interfaces = self.get_interfaces(config)
        if not interfaces:
            raise Exception("Interface Config not lo0")
        found = False
        for intf in interfaces:
            if intf.name == "lo0" and intf.get_unit() and intf.get_unit()[0].get_name() == ifl_num:
                found = True
        if not found:
            raise Exception("Lo0 Interface unit not found")
    # end check_spine_bogus_lo0_ip

    @retries(5, hook=retry_exc_handler)
    def check_spine_evpn_type5_config(self, int_vn, lr_obj):
        vrf_name = DMUtils.make_vrf_name(int_vn.fq_name[-1],
                                  int_vn.virtual_network_network_id, "l3")
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, vrf_name)
        if not ris:
            raise Exception("No RI Config found for internal vn %s" %vrf_name)
        ri = ris[0]
        internal_vn_name = DMUtils.get_lr_internal_vn_name(lr_obj.uuid)
        vn_fq = lr_obj.get_fq_name()[:-1] + [internal_vn_name]
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq)
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        # check ri has protocol evpn
        if not ri.protocols:
            raise Exception("protocol not present in internal vn %s" %vrf_name)
        if not ri.protocols.evpn:
            raise Exception("evpn not present in ri_protocols for internal vn \
                                                                 %s" %vrf_name)
        # get ip_prefix config
        ip_pfx_obj = ri.protocols.evpn.ip_prefix_support
        vnc_vni = vn_obj_properties.vxlan_network_identifier
        if ip_pfx_obj.encapsulation != "vxlan":
            raise Exception("vxlan encaps not set in ip_prefix for int vn \
                                                             %s" %vrf_name)
        if int(ip_pfx_obj.vni) != 1111 or vnc_vni != 1111:
            raise Exception("vni mismatch for int vn '%s': in vnc %s, "
                            "confiugured %s" % (vrf_name, vnc_vni,
                                                ip_pfx_obj.vni))
    # end check_spine_evpn_type5_config

    @retries(5, hook=retry_exc_handler)
    def check_spine_irb_config(self, int_vn, vn_obj):
        vrf_name = DMUtils.make_vrf_name(int_vn.fq_name[-1],
                                  int_vn.virtual_network_network_id, "l3")
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, vrf_name)
        if not ris:
            raise Exception("No RI Config found for internal vn: " + vrf_name)
        ri = ris[0]
        irb_name = "irb." + str(vn_obj.virtual_network_network_id)
        # check if irb is attached to internal RI
        intfs = ri.get_interface()
        if not intfs:
            raise Exception("No interfaces Config found for internal vn: " + vrf_name)
        found = False
        for intf in intfs:
            if intf.name == irb_name:
                found = True
                break
        if not found:
            raise Exception("No IRB interface Config found for internal vn: " + vrf_name)
        # check client VNs, VLans Config
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                  vn_obj.virtual_network_network_id, "l2")
        vlans = config.get_vlans()
        self.assertIsNotNone(vlans)
        vlans = vlans.get_vlan() or []
        found = False
        vlan = None
        # check bridges (vlans) are created for each client VN, and placed irb interface
        for vl in vlans:
            if vl.name == vrf_name[1:]:
                found = True
                vlan = vl
                break
        if not vlan:
            raise Exception("No VLAN config found for vn: " + vrf_name)
        if vlan.get_l3_interface() != irb_name:
            raise Exception("No IRB config attached to VLAN for vn: " + vrf_name)
    # end check_spine_irb_config

    def security_group_create(self, sg_name, project_fq_name):
        project_obj = self._vnc_lib.project_read(project_fq_name)
        sg_obj = SecurityGroup(name=sg_name, parent_obj=project_obj)
        self._vnc_lib.security_group_create(sg_obj)
        return sg_obj
    #end security_group_create

    def _security_group_rule_append(self, sg_obj, sg_rule):
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
    #end _security_group_rule_append

    def _security_group_rule_build(self, rule_info, sg_fq_name_str):
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
    #end _security_group_rule_build

    @retries(5)
    def wait_for_get_sg_id(self, sg_fq_name):
        sg_obj = self._vnc_lib.security_group_read(sg_fq_name)
        if sg_obj.get_security_group_id() is None:
            raise Exception('Security Group Id is none %s' % str(sg_fq_name))

    def build_acl_rule(self, pmin, pmax, direction, proto, etype):
        rule = {}
        rule['port_min'] = pmin
        rule['port_max'] = pmax
        rule['direction'] = direction
        rule['ip_prefix'] = None
        rule['protocol'] = proto
        rule['ether_type'] = etype
        return rule

    def get_firewall_filters(self, sg):
        filter_names = []
        acls = self.get_sg_acls(sg)
        for acl in acls or []:
            if 'ingress-' in acl.name:
                continue
            entries = acl.get_access_control_list_entries()
            if not entries:
                continue
            rules = entries.get_acl_rule() or []
            if not rules:
                continue
            for rule in rules:
                match = rule.get_match_condition()
                if not match:
                    continue
                rule_uuid = rule.get_rule_uuid()
                ether_type_match = match.get_ethertype()
                if "ipv6" in ether_type_match.lower():
                    continue
                filter_name = DMUtils.make_sg_filter_name(sg.name, ether_type_match, rule_uuid)
                filter_names.append(filter_name)
        return filter_names
    # end get_firewall_filters

    def get_sg_acls(self, sg_obj):
        acls = sg_obj.get_access_control_lists()
        acl = None
        acl_list = []
        for acl_to in acls or []:
            acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
            acl_list.append(acl)
        return acl_list

    def test_acl_config(self):
        self.product = 'qfx5110'
        FakeNetconfManager.set_model(self.product)
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', \
                                            product=self.product, role='leaf')
        pr.set_physical_router_role("leaf")
        self._vnc_lib.physical_router_update(pr)
        pi1 = PhysicalInterface('pi1-esi', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi1)

        #create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1', ['default-domain',
                                                      'default-project'])
        self.wait_for_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = self.build_acl_rule(0, 65535, 'egress', 'icmp', 'IPv4')
        sg_rule1 = self._security_group_rule_build(rule1,
                                                   sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())

        # associate li, vmi
        vn1_name = 'vn-acl-' + self.id() + "-" + self.product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam-esi' + self.id() + "-" + self.product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2001)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1-acl' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        li1 = LogicalInterface('li1.0', parent_obj = pi1)
        li1.set_logical_interface_vlan_tag(100)
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)

        filters = self.get_firewall_filters(sg1_obj)
        self.check_firewall_config(filters, False)
        self.check_acl_config('li1', filters, False)

        vmi1.add_security_group(sg1_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi1)
        self.check_firewall_config(filters, True)
        self.check_acl_config('li1', filters, True)

        self._vnc_lib.logical_interface_delete(fq_name=li1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1.get_fq_name())
    # end test_acl_config

    @skip("stale tests")
    def test_esi_config(self):
        self.product = 'qfx5110'
        FakeNetconfManager.set_model(self.product)
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product=self.product, role='leaf')
        pr.set_physical_router_role("leaf")
        self._vnc_lib.physical_router_update(pr)
        pi1 = PhysicalInterface('pi1-esi', parent_obj = pr)
        esi_value = "33:33:33:33:33:33:33:33:33:33"
        pi1.set_ethernet_segment_identifier(esi_value)
        pi_id = self._vnc_lib.physical_interface_create(pi1)

        # associate li, vmi
        vn1_name = 'vn-esi-' + self.id() + "-" + self.product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam-esi' + self.id() + "-" + self.product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2002)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1-esi' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        li1 = LogicalInterface('li1.0', parent_obj = pi1)
        li1.set_logical_interface_vlan_tag(100)
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)


        self.check_esi_config('ae127', esi_value)
        self.check_esi_config('pi1-esi', esi_value, False)

        pi2 = PhysicalInterface('pi2-esi', parent_obj = pr)
        esi_value = "33:33:33:33:33:33:33:33:33:33"
        pi2.set_ethernet_segment_identifier(esi_value)
        pi_id = self._vnc_lib.physical_interface_create(pi2)

        li2 = LogicalInterface('li2.0', parent_obj = pi2)
        li2.set_logical_interface_vlan_tag(100)
        li2.set_virtual_machine_interface(vmi1)
        li2_id = self._vnc_lib.logical_interface_create(li2)

        self.check_esi_config('ae127', esi_value)
        self.check_esi_config('pi2-esi', esi_value, False)

        self.check_chassis_config()
        self.check_lacp_config("ae127", esi_value, ["pi1-esi", "pi2-esi"])
        self.check_l2_evpn_config("ae127", 100)

        pi3 = PhysicalInterface('pi3-esi', parent_obj = pr)
        esi_value = "44:44:44:44:44:44:44:44:44:44"
        pi3.set_ethernet_segment_identifier(esi_value)
        pi_id = self._vnc_lib.physical_interface_create(pi3)

        fq_name = ['default-domain', 'default-project', 'vmi3-esi' + self.id()]
        vmi3 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi3.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi3)

        li3 = LogicalInterface('li3.0', parent_obj = pi3)
        li3.logical_interface_vlan_tag = 200
        li3.set_virtual_machine_interface(vmi3)
        li3_id = self._vnc_lib.logical_interface_create(li3)

        self.check_esi_config('ae126', esi_value)
        self.check_esi_config('pi3-esi', esi_value, False)

        self.check_chassis_config()
        self.check_lacp_config("ae126", esi_value, ["pi3-esi"])
        self.check_l2_evpn_config("ae126", 200)

        # check again ae127 state, should not be changed
        esi_value = "33:33:33:33:33:33:33:33:33:33"
        self.check_lacp_config("ae127", esi_value, ["pi1-esi", "pi2-esi"])
        self.check_l2_evpn_config("ae127", 100)

        '''
        # changing  esi value on one interface is not allowed by api-server
        # need to disable interface, if we want to change esi.
        # DM/api server support will be implemented.
        # We should re-write this piece of validation code

        # unset esi value  on one interface, ae config should still be generated
        pi1.set_ethernet_segment_identifier(None)
        self._vnc_lib.physical_interface_update(pi1)
        self.check_esi_config('ae127', esi_value, True)

        # unset esi value  on both interfaces, ae config should not be generated
        pi2.set_ethernet_segment_identifier(None)
        self._vnc_lib.physical_interface_update(pi2)
        self.check_esi_config('ae127', esi_value, False)

        # set esi value back, run delete tests
        pi1.set_ethernet_segment_identifier(esi_value)
        self._vnc_lib.physical_interface_update(pi1)
        pi2.set_ethernet_segment_identifier(esi_value)
        self._vnc_lib.physical_interface_update(pi2)
        self.check_esi_config('ae127', esi_value, True)
        '''

        self._vnc_lib.logical_interface_delete(fq_name=li1.get_fq_name())
        self._vnc_lib.logical_interface_delete(fq_name=li2.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi2.get_fq_name())
        self.check_esi_config('ae127', esi_value, False)

    # end test_esi_config

    @retries(5, hook=retry_exc_handler)
    def check_l2_evpn_vlan_config(self, vn_obj, vn_mode, intf_name, intf_unit):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], \
                       vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        vlans = config.get_vlans()
        self.assertIsNotNone(vlans)
        vlans = vlans.get_vlan() or []
        vlan_name = vrf_name[1:]
        interfaces = self.get_interfaces(config, intf_name)
        if not interfaces:
            raise Exception("No Vlan Interface Config: " + intf_name)
        if_config = interfaces[0]
        for vlan in vlans:
            if vlan.get_name() == vlan_name:
                intf = vlan.get_interface()[0]
                ifl_name = intf_name + "." + intf_unit
                if intf.get_name() != ifl_name:
                    raise Exception("intf-vlan not correct:" + intf.get_name())
                else:
                    return
        raise Exception("vlan interface config is not correct")
    # end check_l2_evpn_vlan_config

    @retries(5, hook=retry_exc_handler)
    def check_firewall_config(self, fnames, check=True):
        config = FakeDeviceConnect.get_xml_config()
        fwalls = config.get_firewall()
        if not fwalls or not fwalls.get_family() or not fwalls.get_family().get_ethernet_switching():
            if check:
                raise Exception("No eth firewalls configured")
            else:
                return
        ff = fwalls.get_family().get_ethernet_switching().get_filter()
        if check:
            if not ff:
                raise Exception("No eth firewall filter configured")
        elif ff:
            raise Exception("firewall filters still configured")
        else:
            return
        conf_filters = [f.name for f in ff]
        for fname in fnames:
            if fname not in conf_filters:
                raise Exception("firewall filter %s configured"%(fname))
        if check:
            found = False
            for f in ff or []:
                for term in f.get_term() or []:
                    if term.name == 'allow-dns-dhcp':
                        found = True
                        break
            if not found:
                raise Exception("firewall filter default term: allow-dhcp-dns not found")
    # end check_firewall_config

    @retries(5, hook=retry_exc_handler)
    def check_acl_config(self, intf_name, input_list, check=True):
        config = FakeDeviceConnect.get_xml_config()
        interfaces = self.get_interfaces(config, intf_name)
        if not interfaces:
            raise Exception("Interface Config not generated : " + intf_name)
        if_config = interfaces[0]
        if not if_config.get_unit():
            raise Exception("No Units are configured : " + intf_name)
        unit = if_config.get_unit()[0]
        if check:
            if not unit.get_family() or not unit.get_family().get_ethernet_switching():
                raise Exception("No Ethernet Family configured : " + intf_name)
        elif unit.get_family() and unit.get_family().get_ethernet_switching() and eth.get_filter().get_input_list():
            raise Exception("Filters are still configured")
        else:
            return
        eth = unit.get_family().get_ethernet_switching()
        flist = eth.get_filter().get_input_list()
        for iput in input_list:
            if iput not in flist:
                raise Exception("filter %s not attached %s"%(iput, intf_name))
    #def check_acl_config

    @retries(5, hook=retry_exc_handler)
    def check_l2_evpn_native_vlan_config(self, vn_obj, vn_mode, intf_name):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        vlans = config.get_vlans()
        self.assertIsNotNone(vlans)
        vlans = vlans.get_vlan() or []
        vlan_name = vrf_name[1:]
        interfaces = self.get_interfaces(config, intf_name)
        if not interfaces:
            raise Exception("Interface Config not generated(native vlan check) : " + intf_name)
        if_config = interfaces[0]
        if if_config.get_unit()[0].get_name() != '0' or \
              if_config.get_unit()[0].get_vlan_id() != '4094' or \
              if_config.get_native_vlan_id() != '4094':
            raise Exception ("Native vlan config properly generated for intf: " + intf_name)
        for vlan in vlans:
            if vlan.get_name() == vlan_name:
                intf = vlan.get_interface()[0]
                ifl_name = intf_name + ".0"
                if intf.get_name() != ifl_name:
                    raise Exception ("interface-vlan membership not set")
                else:
                    return
        raise Exception ("native vlan interface config not generated")
    #def check_l2_evpn_native_vlan_config

    def test_native_vlan_config(self):
        self.product = 'qfx5110'
        FakeNetconfManager.set_model(self.product)
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product=self.product, role='leaf')
        pr.set_physical_router_role("leaf")
        self._vnc_lib.physical_router_update(pr)
        pi = PhysicalInterface('intf-native', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        # associate li, vmi
        vn1_name = 'vn-native-' + self.id() + "-" + self.product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam-native-' + self.id() + "-" + self.product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2003)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1-esi' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        li1 = LogicalInterface('intf-native.0', parent_obj = pi)
        li1.logical_interface_vlan_tag = 0
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)
        self.check_l2_evpn_native_vlan_config(vn1_obj, 'l2', 'intf-native')
    # end test_native_vlan_config

    def test_vlan_config(self):
        self.product = 'qfx5110'
        FakeNetconfManager.set_model(self.product)
        bgp_router, pr = self.create_router('router' + self.id(), \
                          '1.1.1.1', product=self.product, role='leaf')
        pr.set_physical_router_role("leaf")
        self._vnc_lib.physical_router_update(pr)
        pi = PhysicalInterface('intf-vlan', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        # associate li, vmi
        vn1_name = 'vn-vlan-' + self.id() + "-" + self.product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam-vlan-' + self.id() + "-" + self.product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2004)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1-esi' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        li1 = LogicalInterface('intf-vlan.50', parent_obj=pi)
        li1.logical_interface_vlan_tag = 100
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)
        self.check_l2_evpn_vlan_config(vn1_obj, 'l2', 'intf-vlan', '50')
    # end test_vlan_config

    @retries(5, hook=retry_exc_handler)
    def check_dm_state(self):
        self.assertIsNotNone(DMCassandraDB.get_instance())

    # test dm private cassandra data
    @skip("stale tests")
    def test_ae_id_alloc_cassandra(self):
        #wait for dm
        self.check_dm_state()
        dm_cs = DMCassandraDB.get_instance()
        esi = '10:10:10:10:10:10:10:10:10'
        pr_uuid = 'pr_uuid'
        key = pr_uuid + ':' + esi
        ae_id = 127
        dm_cs.add_ae_id(pr_uuid, esi, ae_id)
        dm_cs.init_pr_ae_map()
        self.assertEqual(dm_cs.get_ae_id(pr_uuid + ':' + esi), ae_id)
        check_db_ae_id = dm_cs.pr_ae_id_map[pr_uuid][esi]
        self.assertEqual(check_db_ae_id, ae_id)

        key_value = dm_cs.get_one_col(dm_cs._PR_AE_ID_CF, key, "index")
        if key_value != ae_id:
            self.assertTrue(False)

        # delete one column
        dm_cs.delete(dm_cs._PR_AE_ID_CF, key, ["index"])
        try:
            key_value = dm_cs.get_one_col(dm_cs._PR_AE_ID_CF, key, "index")
            if key_value is not None:
                self.assertTrue(False)
        except NoIdError:
            pass
        return
    # end

    # check qfx switch options
    def verify_dm_qfx_switch_options(self, product, role=None):
        # check basic valid vendor, product plugin
        FakeNetconfManager.set_model(product)

        # create global vrouter config object
        gv = self.create_global_vrouter_config()
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product=product, role=role)

        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        # create customer network, logical router and link to logical router
        vn1_name = 'vn1' + self.id() + "-" + product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1' + self.id() + "-" + product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2005)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1' + self.id() + "-" + product]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        fq_name = ['default-domain', 'default-project', 'lr1' + self.id() + "-" + product]
        lr = LogicalRouter(fq_name=fq_name, parent_type = 'project', vxlan_network_identifier='1111')
        lr.set_physical_router(pr)
        lr.set_logical_router_type('vxlan-routing')
        lr.add_virtual_machine_interface(vmi1)
        lr_uuid = self._vnc_lib.logical_router_create(lr)

        # make sure internal is created
        int_vn = self.check_lr_internal_vn_state(lr)


        # verify generated switch options config
        self.check_switch_options_config(vn1_obj, "l2", role)

        if role == 'spine':
            self.check_ri_config(vn1_obj, 'l2', False)
            self.check_ri_config(int_vn, 'l3', True)
            # check spine internal vn config
            self.check_spine_irb_config(int_vn, vn1_obj)
            # check bogus lo0 config
            self.check_spine_bogus_lo0_ip(int_vn)
            # check evpn type config
            self.check_spine_evpn_type5_config(int_vn, lr)
        else:
            self.check_ri_config(vn1_obj, 'l2', False)
            self.check_ri_config(vn1_obj, 'l3', False)

        # verify l2 evpns targets
        if 'qfx5' in product:
            ri = self._vnc_lib.routing_instance_read(fq_name=vn1_obj.get_fq_name() + [vn1_obj.name])
            self.check_l2_evpn_vrf_targets(ri.get_route_target_refs()[0]['to'][0])

        # verify internal vn's config
        if 'qfx5' not in product:
            self.check_switch_options_config(int_vn, "l3", role)

        # check vlans config
        if 'qfx5' in product:
            self.check_vlans_config(vn1_obj,
                 vn1_obj_properties.get_vxlan_network_identifier(), 'l2')

        if 'qfx10' in product:
            self.check_ri_vlans_config(int_vn,
                 vn1_obj_properties.get_vxlan_network_identifier(), 'l3', False)

        # check l2 evpn qfx5k leaf config
        if 'qfx5' in product:
            gv.set_vxlan_network_identifier_mode('configured')
            self._vnc_lib.global_vrouter_config_update(gv)
            self.check_l2_evpn_proto_config(vn1_obj, vn1_obj_properties.get_vxlan_network_identifier())

        # check global export policy
        if 'qfx5' in product:
            self.check_switch_options_export_policy_config(True)
        else:
            self.check_switch_options_export_policy_config(False)

        # check policy options
        if 'qfx5' in product:
            self.check_policy_options_config(True)
        else:
            self.check_policy_options_config(False)

        # cleanup
        pr = self._vnc_lib.physical_router_read(fq_name=pr.get_fq_name())
        lr = self._vnc_lib.logical_router_read(fq_name=lr.get_fq_name())
        lr.del_physical_router(pr)
        self._vnc_lib.logical_router_update(lr)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

        self._vnc_lib.logical_router_delete(id=lr_uuid)
        vmi1.del_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi1)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi1.uuid)
    # end verify_dm_qfx_switch_options

    def test_dm_qfx_switch_options(self):
        self.verify_dm_qfx_switch_options('qfx5110', role='leaf')
        self.verify_dm_qfx_switch_options('qfx10000', role='spine')
    # end test_dm_qfx_switch_options
