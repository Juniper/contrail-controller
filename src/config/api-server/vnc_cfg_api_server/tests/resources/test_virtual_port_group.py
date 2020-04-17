#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import range
from builtins import str
import json
import logging

from cfgm_common.exceptions import BadRequest
from testtools import ExpectedException
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import PhysicalInterface
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VirtualPortGroup

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestVirtualPortGroupBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVirtualPortGroupBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVirtualPortGroupBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestVirtualPortGroup(TestVirtualPortGroupBase):

    VMI_NUM = 2

    def test_virtual_port_group_name_with_internal_negative(self):
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        fabric_obj = Fabric('%s-fabric' % (self.id()))
        self.api.fabric_create(fabric_obj)

        vn = VirtualNetwork('vn-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn)

        vpg_name_err = "vpg-internal-" + self.id()
        vpg_obj_err = VirtualPortGroup(vpg_name_err, parent_obj=fabric_obj)

        # Make sure that api server throws an error if
        # VPG is created externally with prefix vpg-internal in the name.
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_create(vpg_obj_err)

        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_virtual_port_group_delete(self):
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        fabric_obj = Fabric('%s-fabric' % (self.id()))
        self.api.fabric_create(fabric_obj)

        vn = VirtualNetwork('vn-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn)

        vpg_name = "vpg-" + self.id()
        vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        self.api.virtual_port_group_create(vpg_obj)

        vmi_id_list = []
        for i in range(self.VMI_NUM):
            vmi_obj = VirtualMachineInterface(self.id() + str(i),
                                              parent_obj=proj_obj)
            vmi_obj.set_virtual_network(vn)
            vmi_id_list.append(
                self.api.virtual_machine_interface_create(vmi_obj))
            vpg_obj.add_virtual_machine_interface(vmi_obj)
            self.api.virtual_port_group_update(vpg_obj)
            self.api.ref_relax_for_delete(vpg_obj.uuid, vmi_id_list[i])

        # Make sure when VPG doesn't get deleted, since associated VMIs
        # still refers it.
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_delete(id=vpg_obj.uuid)

        # Cleanup
        for i in range(self.VMI_NUM):
            self.api.virtual_machine_interface_delete(id=vmi_id_list[i])

        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    # end test_virtual_port_group_delete

    def _create_prerequisites(self, enterprise_style_flag=True,
                              create_second_pr=False):
        # Create project first
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        # Create Fabric with enterprise style flag set to false
        fabric_obj = Fabric('%s-fabric' % (self.id()))
        fabric_obj.set_fabric_enterprise_style(enterprise_style_flag)
        fabric_uuid = self.api.fabric_create(fabric_obj)
        fabric_obj = self.api.fabric_read(id=fabric_uuid)

        # Create physical router
        pr_name = self.id() + '_physical_router'
        pr = PhysicalRouter(pr_name)
        pr_uuid = self._vnc_lib.physical_router_create(pr)
        pr_obj = self._vnc_lib.physical_router_read(id=pr_uuid)

        if create_second_pr:
            pr_name_2 = self.id() + '2_physical_router'
            pr = PhysicalRouter(pr_name_2)
            pr_uuid_2 = self._vnc_lib.physical_router_create(pr)
            pr_obj_2 = self._vnc_lib.physical_router_read(id=pr_uuid_2)
            return proj_obj, fabric_obj, [pr_obj, pr_obj_2]

        return proj_obj, fabric_obj, pr_obj

    def _create_kv_pairs(self, pi_fq_name, fabric_name, vpg_name,
                         tor_port_vlan_id=0):
        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name[2],
             'switch_id': pi_fq_name[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name[1]}]}

        if tor_port_vlan_id != 0:
            kv_pairs = KeyValuePairs(
                [KeyValuePair(key='vpg', value=vpg_name[-1]),
                 KeyValuePair(key='vif_type', value='vrouter'),
                 KeyValuePair(key='tor_port_vlan_id', value=tor_port_vlan_id),
                 KeyValuePair(key='vnic_type', value='baremetal'),
                 KeyValuePair(key='profile',
                              value=json.dumps(binding_profile))])
        else:
            kv_pairs = KeyValuePairs(
                [KeyValuePair(key='vpg', value=vpg_name[-1]),
                 KeyValuePair(key='vif_type', value='vrouter'),
                 KeyValuePair(key='vnic_type', value='baremetal'),
                 KeyValuePair(key='profile',
                              value=json.dumps(binding_profile))])

        return kv_pairs

    def _validate_untagged_vmis(self, fabric_obj, proj_obj, pi_obj):
        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id='4094')

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second untagged VMI.
        # This should fail as there can be only one untagged VMI in a VPG
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id='4092')

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)

    def test_single_untagged_vmi_for_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)
        self._validate_untagged_vmis(fabric_obj, proj_obj, pi_obj)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_untagged_vmi_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)
        self._validate_untagged_vmis(fabric_obj, proj_obj, pi_obj)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_same_vn_in_same_vpg_for_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI.
        # This should fail as a VN can be attached to a VPG only once
        # in a Enterprise style fabric
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))
        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_same_vn_with_same_vlan_across_vpg_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn1
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj_2.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_same_vn_with_different_vlan_across_vpg_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn1
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        # Unlike test_same_vn_with_same_vlan_across_vpg_in_enterprise, we
        # set a different vlan_tag and it should fail
        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_different_vn_on_same_vlan_across_vpgs_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create VN-1
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create VN-2
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn2, but with same vlan_tag=42, this should fail
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_vn_in_vpg_with_same_vlan_twice_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI.
        # This should fail as a VN can only be attached to a VPG with different
        # VLAN in a service provider style fabric
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_vn_in_vpg_with_different_vlan_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI with same VN, but with a different
        # VLAN. This should pass
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_vmi_update(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            enterprise_style_flag=False, create_second_pr=True)

        pr_obj_1 = pr_objs[0]
        pr_obj_2 = pr_objs[1]

        # Create first PI
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi_1 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_1,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_1 = self._vnc_lib.physical_interface_create(pi_1)
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj_1.get_fq_name()

        # Create second PI
        pi_name = self.id() + '_physical_interface2'
        pi_2 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_2,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi_2)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj_2.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]},
            {'port_id': pi_fq_name_2[2],
             'switch_id': pi_fq_name_2[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_2[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        # Read physical interface type, it should be set to access
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, 'access')
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, 'access')
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, remove one of the local_link_information
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi_obj)

        # Read physical interface type again, pi_2's should be set to None
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, 'access')
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, None)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        # Read physical interface type again, it should be set to None
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, None)
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, None)
        self.api.physical_interface_delete(id=pi_uuid_1)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj_1.uuid)
        self.api.physical_router_delete(id=pr_obj_2.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_ae_id_deallocated_for_vpg_multihoming_interfaces(self):
        mock_zk = self._api_server._db_conn._zk_db
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            enterprise_style_flag=False, create_second_pr=True)

        pr_obj_1 = pr_objs[0]
        pr_obj_2 = pr_objs[1]

        # Create first PI
        pi_name = self.id() + '_physical_interface1'
        pi_1 = PhysicalInterface(name=pi_name, parent_obj=pr_obj_1)
        pi_uuid_1 = self._vnc_lib.physical_interface_create(pi_1)
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_fq_name_1 = pi_obj_1.get_fq_name()

        # Create second PI
        pi_name = self.id() + '_physical_interface2'
        pi_2 = PhysicalInterface(name=pi_name, parent_obj=pr_obj_2)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi_2)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        pi_fq_name_2 = pi_obj_2.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        fabric_name = fabric_obj.get_fq_name()
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_fq_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)
        vn1_obj = self.api.virtual_network_read(id=vn1.uuid)
        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1", parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1_obj)

        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]},
            {'port_id': pi_fq_name_2[2],
             'switch_id': pi_fq_name_2[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_2[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_fq_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_create(vmi_obj)

        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)
        vpg_boj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # get phy_rtr_name and ae_id pairs
        pi_refs = vpg_boj.physical_interface_refs
        pr_ae_id_pairs = []
        for pi_ref in pi_refs:
            ae_id = pi_ref['attr'].__dict__.get('ae_num')
            pi_fq_name = pi_ref['to']
            pi = self.api.physical_interface_read(fq_name=pi_fq_name)
            pr = self.api.physical_router_read(id=pi.parent_uuid)
            pr_ae_id_pairs.append((pr.name, ae_id))

        # test ae-id is allocated
        for phy_rtr_name, ae_id in pr_ae_id_pairs:
            self.assertTrue(mock_zk.ae_id_is_occupied(phy_rtr_name, ae_id))

        # delete VPG
        self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)

        # test ae-id is deallocated
        for phy_rtr_name, ae_id in pr_ae_id_pairs:
            self.assertFalse(mock_zk.ae_id_is_occupied(phy_rtr_name, ae_id))

        # cleanup
        self.api.physical_interface_delete(id=pi_uuid_1)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj_1.uuid)
        self.api.physical_router_delete(id=pr_obj_2.uuid)
        self.api.virtual_network_read(id=vn1_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
