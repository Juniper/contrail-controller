#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
import uuid
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *
from device_manager.dm_utils import DMUtils

class TestAnsibleSecurityGroupsDM(TestAnsibleCommonDM):

    def test_01_security_groups_add(self):
	#create SG and ACL
        sg1_obj = self.security_group_create('sg-1', ['default-domain',
                                                      'default-project'])
        self.wait_for_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        prefix = '6.6.6.0/24'
        rule1 = self.build_acl_rule(0, 65535, 'egress', prefix, 'icmp', 'IPv4')
        sg_rule1 = self._security_group_rule_build(rule1,
                                                   sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())

        # create objects
        self.create_feature_objects_and_params()     
        pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, _, _ = self.create_vpg_dependencies()
        vmi_obj = self.create_vpg_and_vmi(sg1_obj, pr1, fabric, pi_obj_1, vn_obj)

        # this should trigger reaction map so that PR
        # config changes and device abstract config is generated.
        # verify the generated device abstract config properties

        sg1_obj_fqname = sg1_obj.get_fq_name()
        sg1_obj_uuid = sg1_obj.get_uuid()

        acl_uuid = self.get_acl_uuid(sg1_obj)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()
        device_abstract_config = abstract_config.get('device_abstract_config')

        firewall = device_abstract_config.get(
            'firewall', {})
        print device_abstract_config
        if len(firewall) == 0:
            raise Exception("No firewalls configured")
        firewall_filters = firewall.get('firewall_filters', [])
        if len(firewall_filters) == 0:
            raise Exception("No firewall filters configured")

        firewall_filter = firewall_filters[0]
        self.assertEqual(firewall_filter.get('name'),
                         "sg-filter-"+ sg1_obj_fqname[-1] + "-" + acl_uuid)
        acls = firewall_filter.get('terms')
	acl = acls[-1]
        #match some basic acl params - IP & protocol
        destaddr = acl.get('fromxx', {}).get('destination_address', {})
        destprefix = destaddr[0].get('prefix')
        self.assertEqual(destprefix, prefix.split('/')[0])

        proto = acl.get('fromxx', {}).get('ip_protocol', {})
        self.assertEqual(int(proto), 1)

        # now update the sg object and check if
        # update causes device abstract config also to update

        prefix = '7.7.7.0/24'
        rule2 = self.build_acl_rule(0, 65535, 'egress', prefix, 'icmp', 'IPv4')
        sg_rule2 = self._security_group_rule_build(rule2,
                                                   sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())

        # delete workflow

        self.delete_objects()


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

    def build_acl_rule(self, pmin, pmax, direction, prefix, proto, etype):
        rule = {}
        rule['port_min'] = pmin
        rule['port_max'] = pmax
        rule['direction'] = direction
        rule['ip_prefix'] = prefix
        rule['protocol'] = proto
        rule['ether_type'] = etype
        return rule

    def get_acl_uuid(self, sg):
        uuid = None
        acls = self.get_sg_acls(sg)
        for acl in acls or []:
            if 'ingress-' in acl.name:
                continue
            uuid = acl.uuid
            return uuid
        return uuid

    def get_sg_acls(self, sg_obj):
        acls = sg_obj.get_access_control_lists()
        acl = None
        acl_list = []
        for acl_to in acls or []:
            acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
            acl_list.append(acl)
        return acl_list

    def create_feature_objects_and_params(self, role='erb-ucast-gateway'):
        self.create_features(['storm-control'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles([role])
        self.create_role_definitions([
            AttrDict({
                'name': 'storm-control-role',
                'physical_role': 'leaf',
                'overlay_role': role,
                'features': ['storm-control'],
                'feature_configs': None
            })
        ])

    def create_vpg_dependencies(self, enterprise_style=True,
                                role='erb-ucast-gateway', mh=False):

        pr2 = None
        pi_obj_1_pr2 = None
        jt = self.create_job_template('job-template-sc' + self.id())

        fabric = self.create_fabric('fab-sc' + self.id())
        fabric.set_fabric_enterprise_style(enterprise_style)
        self._vnc_lib.fabric_update(fabric)

        np, rc = self.create_node_profile('node-profile-sc' + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {'physical_role': 'leaf',
                    'rb_roles': [role]}
                )],
            job_template=jt)

        bgp_router1, pr1 = self.create_router('device-sc' + self.id(), '3.3.3.3',
        product='qfx5110-48s-4c', family='junos-qfx',
        role='leaf', rb_roles=[role],
        physical_role=self.physical_roles['leaf'],
        overlay_role=self.overlay_roles[
                                   role], fabric=fabric,
        node_profile=np, ignore_bgp=False)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        pi_name = "xe-0/0/0"
        pi_obj_1 = PhysicalInterface(pi_name, parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi_obj_1)

        pi_name = "xe-0/0/1"
        pi_obj_2 = PhysicalInterface(pi_name, parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi_obj_2)

        if mh:
            bgp_router2, pr2 = self.create_router('device-sc-2' + self.id(), '3.3.3.2',
                                                  product='qfx5110-48s-4c', family='junos-qfx',
                                                  role='leaf', rb_roles=[role],
                                                  physical_role=self.physical_roles['leaf'],
                                                  overlay_role=self.overlay_roles[
                                                      role], fabric=fabric,
                                                  node_profile=np, ignore_bgp=True)
            pr2.set_physical_router_loopback_ip('30.30.0.1')
            self._vnc_lib.physical_router_update(pr2)

            pi_name = "xe-0/0/0"
            pi_obj_1_pr2 = PhysicalInterface(pi_name, parent_obj=pr2)
            self._vnc_lib.physical_interface_create(pi_obj_1_pr2)

        vn_obj = self.create_vn(str(3), '3.3.3.0')

        return pr1, fabric, pi_obj_1, pi_obj_2, vn_obj, pr2, pi_obj_1_pr2


    def create_vpg_and_vmi(self, sg_obj, pr1, fabric, pi_obj,
                           vn_obj, pp_obj_2=None, pr2=None, pi_obj2=None, vpg_nm=1):

        device_name = pr1.get_fq_name()[-1]
        fabric_name = fabric.get_fq_name()[-1]
        phy_int_name = pi_obj.get_fq_name()[-1]

        # first create a VMI

        vpg_name = "vpg-sc-" + str(vpg_nm) + self.id()
        vlan_tag = 10

        vmi_obj_1 = VirtualMachineInterface(vpg_name + "-tagged-" + str(vlan_tag),
                                          parent_type='project',
                                          fq_name = ["default-domain", "default-project",
                                                     vpg_name + "-tagged-" + str(vlan_tag)])

        if pr2:
            # if pr2 is not none, it should also have a pi_obj2
            phy_int_name_2 = pi_obj2.get_fq_name()[-1]
            device_name_2 = pr2.get_fq_name()[-1]
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
                phy_int_name, phy_int_name, device_name, fabric_name, phy_int_name_2, phy_int_name_2,
                device_name_2, fabric_name)
        elif pi_obj2:
            phy_int_name_2 = pi_obj2.get_fq_name()[-1]
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
            phy_int_name, phy_int_name, device_name, fabric_name, phy_int_name_2, phy_int_name_2,
            device_name, fabric_name)
        else:
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (phy_int_name,
                                                                                                                                             phy_int_name,
                                                                                                                                             device_name,
                                                                                                                                             fabric_name)

        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "profile",
                "value": vmi_profile
            }]
        }


        vmi_obj_1.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
        vmi_obj_1.set_virtual_machine_interface_properties(vmi_properties)

        vmi_obj_1.set_virtual_network(vn_obj)


        vmi_obj_2 = VirtualMachineInterface(vpg_name + "-untagged-" + str(vlan_tag),
                                            parent_type='project',
                                            fq_name = ["default-domain", "default-project",
                                                       vpg_name + "-untagged-" + str(vlan_tag)])

        vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (phy_int_name,
                                                                                                                                             phy_int_name,
                                                                                                                                             device_name,
                                                                                                                                             fabric_name)

        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "profile",
                "value": vmi_profile
            }, {
                "key": "tor_port_vlan_id",
                "value": str(vlan_tag)
            }]
        }


        vmi_obj_2.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_obj_2.set_virtual_network(vn_obj)

        # now create a VPG
        vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
        if pi_obj2:
            vpg_obj.set_physical_interface_list([
                pi_obj2.get_fq_name(),
                pi_obj.get_fq_name()],
                [{'ae_num': 0},
                 {'ae_num': 0}])
        else:
            vpg_obj.set_physical_interface(pi_obj)
        self._vnc_lib.virtual_port_group_create(vpg_obj)

        #attach security group
        vmi_obj_1.set_security_group(sg_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi_obj_1)

        li1 = LogicalInterface('li1.0', parent_obj = pi_obj)
        li1.set_logical_interface_vlan_tag(100)
        li1.set_virtual_machine_interface(vmi_obj_1)
        li1_id = self._vnc_lib.logical_interface_create(li1)

#        vmi_obj_1.add_security_group(sg_obj)
#        self._vnc_lib.virtual_machine_interface_update(vmi_obj_1)

        vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()}])

        if pp_obj_2:
            vmi_obj_2.set_port_profile(pp_obj_2)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj_2)
            vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()},
                                                        {'uuid': vmi_obj_2.get_uuid()}])
        self._vnc_lib.virtual_port_group_update(vpg_obj)


        return vmi_obj_1

    def delete_objects(self):

        vpg_list = self._vnc_lib.virtual_port_groups_list().get('virtual-port-groups')
        for vpg in vpg_list:
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg['uuid'])
            vpg_obj.set_virtual_machine_interface_list([])
            vpg_obj.set_physical_interface_list([])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

        li_list = self._vnc_lib.logical_interfaces_list().get(
            'logical-interfaces')
        for li in li_list:
            self._vnc_lib.logical_interface_delete(id=li['uuid'])

        vmi_list = self._vnc_lib.virtual_machine_interfaces_list().get(
            'virtual-machine-interfaces')
        for vmi in vmi_list:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])

        sg_list = self._vnc_lib.security_groups_list().get('security-groups')
        for sg in sg_list:
            self._vnc_lib.security_group_delete(id=sg['uuid'])
        
        pi_list = self._vnc_lib.physical_interfaces_list().get('physical-interfaces')
        for pi in pi_list:
            self._vnc_lib.physical_interface_delete(id=pi['uuid'])

        for vpg in vpg_list:
            self._vnc_lib.virtual_port_group_delete(id=vpg['uuid'])

        pr_list = self._vnc_lib.physical_routers_list().get('physical-routers')
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])

        vn_list = self._vnc_lib.virtual_networks_list().get('virtual-networks')
        for vn in vn_list:
            self._vnc_lib.virtual_network_delete(id=vn['uuid'])

        rc_list = self._vnc_lib.role_configs_list().get('role-configs')
        for rc in rc_list:
            self._vnc_lib.role_config_delete(id=rc['uuid'])

        np_list = self._vnc_lib.node_profiles_list().get('node-profiles')
        for np in np_list:
            self._vnc_lib.node_profile_delete(id=np['uuid'])

        fab_list = self._vnc_lib.fabrics_list().get('fabrics')
        for fab in fab_list:
            self._vnc_lib.fabric_delete(id=fab['uuid'])

        jt_list = self._vnc_lib.job_templates_list().get('job-templates')
        for jt in jt_list:
            self._vnc_lib.job_template_delete(id=jt['uuid'])

        pp_list = self._vnc_lib.port_profiles_list().get('port-profiles')
        for pp in pp_list:
            self._vnc_lib.port_profile_delete(id=pp['uuid'])

        sc_list = self._vnc_lib.storm_control_profiles_list().get('storm-control-profiles')
        for sc in sc_list:
            self._vnc_lib.storm_control_profile_delete(id=sc['uuid'])

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
