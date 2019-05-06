#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")

from test_case import DMTestCase
from test_common import retries
from test_common import retry_exc_handler
from vnc_api.vnc_api import *


class TestAnsibleCommonDM(DMTestCase):
    @classmethod
    def setUpClass(cls):
        dm_config_knobs = [
            ('DEFAULTS', 'push_mode', '1')
        ]
        super(TestAnsibleCommonDM, cls).setUpClass(
            dm_config_knobs=dm_config_knobs)
    # end setUpClass

    def create_features(self, features=[]):
        self.features = {}
        for feature in features:
            feature_obj = Feature(fq_name=[self.GSC, feature],
                                  parent_type='global-system-config',
                                  name=feature, display_name=feature)
            self._vnc_lib.feature_create(feature_obj)
            self.features[feature] = feature_obj
    # end create_features

    def create_physical_roles(self, physical_roles=[]):
        self.physical_roles = {}
        for physical_role in physical_roles:
            physical_role_obj = PhysicalRole(fq_name=[self.GSC, physical_role],
                                             parent_type='global-system-config',
                                             name=physical_role,
                                             display_name=physical_role)
            self._vnc_lib.physical_role_create(physical_role_obj)
            self.physical_roles[physical_role] = physical_role_obj
    # end create_physical_roles

    def create_overlay_roles(self, overlay_roles=[]):
        self.overlay_roles = {}
        for overlay_role in overlay_roles:
            overlay_role_obj = OverlayRole(fq_name=[self.GSC, overlay_role],
                                           parent_type='global-system-config',
                                           name=overlay_role,
                                           display_name=overlay_role)
            self._vnc_lib.overlay_role_create(overlay_role_obj)
            self.overlay_roles[overlay_role] = overlay_role_obj
    # end create_overlay_roles

    def create_role_definition(self, name, physical_role, overlay_role,
            features, feature_configs):
        role_definition = RoleDefinition(fq_name=[self.GSC, name], name=name,
                                         parent_type='global-system-config',
                                         display_name=name)
        role_definition.set_physical_role(self.physical_roles[physical_role])
        role_definition.set_overlay_role(self.overlay_roles[overlay_role])
        for feature in features:
            role_definition.add_feature(self.features[feature])
        self._vnc_lib.role_definition_create(role_definition)
        if feature_configs:
            for feature, config in feature_configs.items():
                kvps = [KeyValuePair(key=key, value=value) for key, value in config.items()]
                config = KeyValuePairs(key_value_pair=kvps)
                feature_config = FeatureConfig(name=feature,
                    parent_obj=role_definition,
                    feature_config_additional_params=config)
                self._vnc_lib.feature_config_create(feature_config)
                self.feature_configs.append(feature_config)
        return role_definition
    # end create_role_definition

    def create_role_definitions(self, role_definitions):
        self.role_definitions = {}
        self.feature_configs = []
        for rd in role_definitions:
            self.role_definitions[rd.name] = \
                self.create_role_definition(rd.name, rd.physical_role,
                                            rd.overlay_role, rd.features,
                                            rd.feature_configs)
    # end create_role_definitions

    def create_job_template(self, name):
        job_template = JobTemplate(fq_name=[self.GSC, name], name=name,
                                   parent_type='global-system-config',
                                   display_name=name)
        self._vnc_lib.job_template_create(job_template)
        return job_template
    # end create_job_template

    def create_node_profile(self, name, vendor='juniper', device_family=None,
                            role_mappings=[], job_template=None):
        node_profile_role_mappings = [NodeProfileRoleType(
                                            physical_role=r.physical_role,
                                            rb_roles=r.rb_roles)
                                        for r in role_mappings]
        node_profile_roles = NodeProfileRolesType(
                                role_mappings=node_profile_role_mappings)
        node_profile = NodeProfile(fq_name=[self.GSC, name], name=name,
                                   parent_type='global-system-config',
                                   display_name=name,
                                   node_profile_vendor=vendor,
                                   node_profile_device_family=device_family,
                                   node_profile_roles=node_profile_roles)
        self._vnc_lib.node_profile_create(node_profile)

        role_config = RoleConfig(fq_name=[self.GSC, name, 'basic'],
                                 parent_type='node-profile',
                                 name='basic',
                                 display_name='basic')
        role_config.set_job_template(job_template)
        self._vnc_lib.role_config_create(role_config)

        return node_profile, role_config
    # end create_node_profile

    def create_fabric(self, name):
        fabric = Fabric(fq_name=[self.GSC, name], name=name,
                        parent_type='global-system-config',
                        display_name=name, manage_underlay=False)
        fabric_uuid = self._vnc_lib.fabric_create(fabric)
        return self._vnc_lib.fabric_read(id=fabric_uuid)
    # end create_fabric

    def create_vn(self, vn_id, subnet):
        vn_name = 'vn' + vn_id + '-' + self.id()
        vn_obj = VirtualNetwork(vn_name)
        vn_obj_properties = VirtualNetworkType()
        vn_obj_properties.set_vxlan_network_identifier(2000 + int(vn_id))
        vn_obj_properties.set_forwarding_mode('l2_l3')
        vn_obj.set_virtual_network_properties(vn_obj_properties)

        ipam1_obj = NetworkIpam('ipam' + vn_id + '-' + self.id())
        ipam1_uuid = self._vnc_lib.network_ipam_create(ipam1_obj)
        ipam1_obj = self._vnc_lib.network_ipam_read(id=ipam1_uuid)
        vn_obj.add_network_ipam(ipam1_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(subnet, 24))]))

        vn_uuid = self._vnc_lib.virtual_network_create(vn_obj)
        return self._vnc_lib.virtual_network_read(id=vn_uuid)
    # end create_vn

    def create_vm(self, vm_name=None):
        vm = VirtualMachine(name=vm_name, display_name=vm_name,
            fq_name=[vm_name], server_type='baremetal-server')
        vm_uuid = self._vnc_lib.virtual_machine_create(vm)
        vm = self._vnc_lib.virtual_machine_read(id=vm_uuid)
        return vm_uuid

    def create_vmi(self, pr_pi={}, fabric=None, lag=True, vm_uuid=None,
                   vn=None):
        vmi_obj = VirtualMachineInterface(
            fq_name = ['default-domain', 'default-project', 'vmi' + vmi_id +
                       '-' + self.id()],
            parent_type="project",
            virtual_machine_interface_device_owner="baremetal:none",
            virtual_machine_interface_mac_addresses={
                "mac_address": ["08:00:27:97:86:68"]
            }
        )
        if lag is True and len(pr_pi) is 1:
            virtual_machine_interface_bindings = {
                "key_value_pair": [{
                    "key": "vnic_type",
                    "value": "baremetal"
                }, {
                    "key": "vif_type",
                    "value": "vrouter"
                }, {
                    "key": "profile",
                    "value": "{\"local_link_information\":[{"
                             "\"switch_id\":\"11:11:11:11:11:11\","
                             "\"port_id\":\"xe-0/0/2\","
                             "\"switch_info\":\"vqfx2\","
                             "\"fabric\":\"fab-lag-mh\"},"
                             "{\"switch_id\":\"11:11:11:11:11:11\","
                             "\"port_id\":\"xe-0/0/2\","
                             "\"switch_info\":\"vqfx3\","
                             "\"fabric\":\"fab-lag-mh\"}]}" % (pr_pi.get)
                }, {
                    "key": "host_id",
                    "value": vm_uuid
                }]
            }
        virtual_machine_interface_properties1 = {
            "sub_interface_vlan_tag": 100
        }
        vmi_obj.set_virtual_machine_interface_bindings(
            virtual_machine_interface_bindings)
        vmi_obj.set_virtual_machine_interface_properties(
            virtual_machine_interface_properties1)
        vmi_obj.add_virtual_network(vn)
        vmi_uuid = self._api.virtual_machine_interface_create(vmi_obj)




    def attach_vmi(self, vmi_id, pi_name, pr, vn, fabric):
        pi = PhysicalInterface(pi_name, parent_obj=pr)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi = self._vnc_lib.physical_interface_read(id=pi_uuid)

        vm = VirtualMachine(name='bms' + vmi_id, display_name='bms' + vmi_id,
            fq_name=['bms' + vmi_id], server_type='baremetal-server')
        vm_uuid = self._vnc_lib.virtual_machine_create(vm)
        vm = self._vnc_lib.virtual_machine_read(id=vm_uuid)

        mac_address = '08:00:27:af:94:0' + vmi_id
        fq_name = ['default-domain', 'default-project', 'vmi' + vmi_id + '-' + self.id()]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project',
            virtual_machine_interface_device_owner='baremetal:none',
            virtual_machine_interface_mac_addresses= {
                   'mac_address': [mac_address]
                })
        vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (pi_name, pr.get_fq_name()[-1], fabric.get_fq_name()[-1])
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
                "key": "host_id",
                "value": vm_uuid
            }]
        }
        vmi_properties = {
            "sub_interface_vlan_tag": 100 + int(vmi_id)
        }
        vmi.set_virtual_machine_interface_bindings(vmi_bindings)
        vmi.set_virtual_machine_interface_properties(vmi_properties)
        vmi.add_virtual_network(vn)
        vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi)
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)

        return vmi, vm, pi
    # end attach_vmi

    def set_encapsulation_priorities(self, priorities = []):
        fq_name=['default-global-system-config', 'default-global-vrouter-config']
        try:
            config = self._vnc_lib.global_vrouter_config_read(fq_name=fq_name)
        except NoIdError:
            config = GlobalVrouterConfig(fq_name=fq_name)
            self._vnc_lib.global_vrouter_config_create(config)
            config = self._vnc_lib.global_vrouter_config_read(fq_name=fq_name)
        config.set_encapsulation_priorities(EncapsulationPrioritiesType(encapsulation=priorities))
        self._vnc_lib.global_vrouter_config_update(config)
    # end set_encapsulation_priorities

    def delete_role_definitions(self):
        for fc in self.feature_configs:
            self._vnc_lib.feature_config_delete(fq_name=fc.get_fq_name())
        for rd in self.role_definitions.values():
            self._vnc_lib.role_definition_delete(fq_name=rd.get_fq_name())
    # end delete_role_definitions

    def delete_overlay_roles(self):
        for r in self.overlay_roles.values():
            self._vnc_lib.overlay_role_delete(fq_name=r.get_fq_name())
    # end delete_overlay_roles

    def delete_physical_roles(self):
        for r in self.physical_roles.values():
            self._vnc_lib.physical_role_delete(fq_name=r.get_fq_name())
    # end delete_physical_roles

    def delete_features(self):
        for f in self.features.values():
            self._vnc_lib.feature_delete(fq_name=f.get_fq_name())
    # end delete_features

    @retries(5, hook=retry_exc_handler)
    def wait_for_features_delete(self):
        for f in self.features.values():
            try:
                self._vnc_lib.feature_read(fq_name=f.get_fq_name())
                self.assertFalse(True)
            except NoIdError:
                pass
    # end wait_for_features_delete
# end TestAnsibleDM
