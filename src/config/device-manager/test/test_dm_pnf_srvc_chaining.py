#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import sys
import logging

from attrdict import AttrDict
import gevent
from .test_dm_ansible_common import TestAnsibleCommonDM
from vnc_api.vnc_api import *

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestAnsiblePNFSrvcChainingDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsiblePNFSrvcChainingDM,
              self).setUp(extra_config_knobs=extra_config_knobs)
        self.console_handler = logging.StreamHandler()
        self.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(self.console_handler)

    def tearDown(self, extra_config_knobs=None):
        logger.removeHandler(self.console_handler)
        super(TestAnsiblePNFSrvcChainingDM, self).tearDown()

    def test_pnf_roles_availability(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Change srx's physical role from pnf to spine
        srx.set_physical_router_role("spine")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()
        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # srx abstract config will not have security zones now
        security_policies = srx_pnf_feature_config.get("security_policies")
        self.assertIsNone(security_policies)

        # Adding it back should generate the full service chaining config
        srx.set_physical_router_role("pnf")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()

        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")
        security_policies = srx_pnf_feature_config.get("security_policies")
        self.assertIsNotNone(security_policies)

        # Remove PNF as a rb role from qfx
        qfx.set_routing_bridging_roles(
            RoutingBridgingRolesType(rb_roles=["CRB-MCAST-Gateway"]))
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()
        qfx_pnf_feature_config = qfx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # There should be no vlans key in the abstract config now
        vlans = qfx_pnf_feature_config.get("vlans")
        self.assertIsNone(vlans)

        # Adding the pnf role back should bring it back
        qfx.set_routing_bridging_roles(
            RoutingBridgingRolesType(
                rb_roles=["CRB-MCAST-Gateway", "PNF-Servicechain"]))
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()
        qfx_pnf_feature_config = qfx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # There should be no vlans key in the abstract config now
        vlans = qfx_pnf_feature_config.get("vlans")
        self.assertIsNotNone(vlans)

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_pnf_roles_availability

    def test_lr_type(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Add loopback ip to force abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)

        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config")
        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances")

        # When lr-type is not set to vxlan-routing, there will
        # be no internal vn's created
        internal_vn_identifier = '"virtual_network_is_internal": true'
        self.assertTrue(
            internal_vn_identifier not in str(qfx_routing_instances))

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_lr_type

    def test_pt_lr_assoc(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Delete the logical routers from the port tuple
        pt_obj.del_logical_router(llr)
        pt_obj.del_logical_router(rlr)

        self._vnc_lib.port_tuple_update(pt_obj)

        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_pnf_feature_config = qfx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # Physical Interfaces now should not contain interfaces of type service
        phy_intfs = str(qfx_pnf_feature_config.get("physical_interfaces"))
        if_type_identifier = '"interface_type": "service"'
        self.assertTrue(if_type_identifier not in phy_intfs)

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_pt_lr_assoc

    def test_subnets_availability(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects(subnets=False)

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Add loopback ip to force abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()

        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        physical_interfaces = srx_pnf_feature_config.get("physical_interfaces")

        if physical_interfaces is not None:
            pi_types = [pi['interface_type'] for pi in physical_interfaces]
            self.assertTrue('service' not in pi_types)
        else:
            self.assertIsNone(physical_interfaces)

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_subnets_availability

    def test_vmi_availability(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, None)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, None)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Add loopback ip to force abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config")

        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances")

        self.assertFalse(
            (str(2000 + unique_vlan_id + 24) in str(qfx_routing_instances)) and
            (str(2000 + unique_vlan_id + 42) in str(qfx_routing_instances)))

        llr.set_virtual_machine_interface(vmi_1)
        self._vnc_lib.logical_router_update(llr)

        rlr.set_virtual_machine_interface(vmi_2)
        self._vnc_lib.logical_router_update(rlr)

        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config")

        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances")

        self.assertTrue(
            (str(2000 + unique_vlan_id + 24) in str(qfx_routing_instances)) and
            (str(2000 + unique_vlan_id + 42) in str(qfx_routing_instances)))

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_vmi_availability

    def test_service_objects_availability(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        gevent.sleep(1)

        # Add loopback ip to force abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)

        gevent.sleep(0.1)
        srx_abstract_config = self.check_dm_ansible_config_push()
        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        srx_routing_instances = srx_pnf_feature_config.get("routing_instances")

        self.assertIsNone(srx_routing_instances)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_service_objects_availability

    def test_multiple_si(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Create one more service instance and port tuple
        new_si = self.create_service_instance(name="new_si" + self.id(),
                                              st_obj=st_obj)
        new_pt = self.create_port_tuple(name="new_pt" + self.id(),
                                        left_lr_name=llr.display_name,
                                        right_lr_name=rlr.display_name,
                                        si_obj=new_si)

        # Add srx loopback ip to force srx abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()

        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # Verify both new_si and si are present
        srx_ri = str(srx_pnf_feature_config.get("routing_instances"))

        new_left = "new_si" + self.id() + "_left"
        left = "si" + self.id() + "_left"
        new_right = "new_si" + self.id() + "_right"
        right = "si" + self.id() + "_right"

        self.assertTrue((new_left in srx_ri) and (left in srx_ri))
        self.assertTrue((new_right in srx_ri) and (right in srx_ri))

        # Add qfx loopback ip to force qfx abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_pnf_feature_config = qfx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        # Verify both new_si and si are present
        qfx_ri = str(qfx_pnf_feature_config.get("routing_instances"))

        internal_vn_identifier = "'virtual_network_is_internal': True"

        self.assertEqual(qfx_ri.count(internal_vn_identifier), 2)

        self.assertTrue((new_left in qfx_ri) and (left in qfx_ri))
        self.assertTrue((new_right in qfx_ri) and (right in qfx_ri))

        # Destroy service objects
        self._vnc_lib.port_tuple_delete(fq_name=new_pt.get_fq_name())
        self._vnc_lib.service_instance_delete(fq_name=new_si.get_fq_name())
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_multiple_si

    def test_multi_si_multi_lr(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Create Two Virtual Networks
        vlan_id_3 = 45
        vlan_id_4 = 54

        vn3 = self.create_vn(str(vlan_id_3), "9.9.9.0")
        vn4 = self.create_vn(str(vlan_id_4), "10.10.10.0")

        # Create VMI
        vmi_3 = self.create_vmi(str(vlan_id_3), vn3)
        vmi_4 = self.create_vmi(str(vlan_id_4), vn4)

        # Create New Left LR
        new_llr = LogicalRouter("new_left_lr" + self.id())
        new_llr.set_logical_router_type("vxlan-routing")
        new_llr.set_physical_router(qfx)
        new_llr.set_virtual_machine_interface(vmi_3)
        new_llr_uuid = self._vnc_lib.logical_router_create(new_llr)
        new_llr = self._vnc_lib.logical_router_read(id=new_llr_uuid)

        # Create New Right LR
        new_rlr = LogicalRouter("new_right_lr" + self.id())
        new_rlr.set_logical_router_type("vxlan-routing")
        new_rlr.set_physical_router(qfx)
        new_rlr.set_virtual_machine_interface(vmi_4)
        new_rlr_uuid = self._vnc_lib.logical_router_create(new_rlr)
        new_rlr = self._vnc_lib.logical_router_read(id=new_rlr_uuid)

        # Create one more service instance and port tuple
        new_si = self.create_service_instance(name="new_si" + self.id(),
                                              st_obj=st_obj)
        new_pt = self.create_port_tuple(
            name="new_pt" + self.id(),
            si_obj=new_si,
            left_lr_name="new_left_lr" + self.id(),
            right_lr_name="new_right_lr" + self.id(),
        )

        # Add srx loopback ip to force srx abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()

        srx_pnf_feature_config = srx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        srx_ri = str(srx_pnf_feature_config.get("routing_instances"))

        # Verify both new_si and si are present

        new_left = "new_si" + self.id() + "_left"
        left = "si" + self.id() + "_left"
        new_right = "new_si" + self.id() + "_right"
        right = "si" + self.id() + "_right"

        self.assertTrue((new_left in srx_ri) and (left in srx_ri))
        self.assertTrue((new_right in srx_ri) and (right in srx_ri))

        # Add qfx loopback ip to force qfx abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_pnf_feature_config = qfx_abstract_config.get(
            "device_abstract_config").get("features").get(
                "pnf-service-chaining")

        qfx_ri = str(qfx_pnf_feature_config.get("routing_instances"))

        # Verify that there are 4 internal vn's created
        internal_vn_identifier = "'virtual_network_is_internal': True"
        self.assertEqual(qfx_ri.count(internal_vn_identifier), 4)

        # Verify both new_si and si are present
        self.assertTrue((new_left in qfx_ri) and (left in qfx_ri))
        self.assertTrue((new_right in qfx_ri) and (right in qfx_ri))

        # Destroy service objects
        self._vnc_lib.port_tuple_delete(fq_name=new_pt.get_fq_name())
        self._vnc_lib.service_instance_delete(fq_name=new_si.get_fq_name())
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr, new_llr, new_rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2, vmi_3, vmi_4]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2, vn3, vn4]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_multi_si_multi_lr

    def test_config(self):
        # Create base objects
        (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        ) = self.create_base_objects()

        # Get Individual Physical Routers
        srx = physical_routers[0]
        qfx = physical_routers[1]

        # Create PIs for SRX
        pi_list = []
        srx_pis = ["lo0", "xe-1/0/0", "xe-1/0/1"]
        for idx in range(len(srx_pis)):
            pi = PhysicalInterface(srx_pis[idx], parent_obj=srx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create PIs for QFX
        qfx_pis = ["xe-1/1/0", "xe-1/1/1"]
        for idx in range(len(qfx_pis)):
            pi = PhysicalInterface(qfx_pis[idx], parent_obj=qfx)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi = self._vnc_lib.physical_interface_read(id=pi_uuid)
            pi_list.append(pi)

        # Create Two Virtual Networks
        unique_vlan_id = int(int(fabric.get_uuid().split('-')[0], 16) / 1000)
        vlan_id_1 = 24 + unique_vlan_id
        vlan_id_2 = 42 + unique_vlan_id

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = self.create_lr_with_vmi("left_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_1)

        # Create Right Logical Router
        rlr = self.create_lr_with_vmi("right_lr" + self.id(), "vxlan-routing",
                                      qfx, vmi_2)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        srx_device_abstract_config, security_policies = (
            self.check_config_with_retry(srx,
                                         config_key=[
                                             "features",
                                             "pnf-service-chaining",
                                             "security_policies"
                                         ]))

        srx_pnf_feature_config = srx_device_abstract_config.get(
            "features").get("pnf-service-chaining")

        self.assertIsNotNone(security_policies)

        # Verify security zones
        security_zones = str(srx_pnf_feature_config.get("security_zones"))

        zone_1 = "xe-1/0/0.1000"
        zone_2 = "xe-1/0/1.1001"

        self.assertTrue((zone_1 in security_zones) and
                        (zone_2 in security_zones))

        # Verify srx left and right interfaces
        srx_physical_interfaces = str(
            srx_pnf_feature_config.get("physical_interfaces"))
        left_interface = "xe-1/0/0.1000"
        right_interface = "xe-1/0/1.1001"

        self.assertTrue((left_interface in srx_physical_interfaces) and
                        (right_interface in srx_physical_interfaces))

        # Verify loopback interfaces
        logical_interface = "lo0.1000"
        self.assertTrue(logical_interface in srx_physical_interfaces)

        # Verify bgp
        srx_bgp = str(
            srx_pnf_feature_config.get("routing_instances")[0].get("protocols")
            [0].get("bgp"))

        left_bgp_grp = "si" + self.id() + "_left"
        right_bgp_grp = "si" + self.id() + "_right"
        ip_prefix = "1.1.1"
        bgp_identifiers = [left_bgp_grp, right_bgp_grp, ip_prefix]

        self.assertTrue(all(item in srx_bgp for item in bgp_identifiers))

        # Verify pim interfaces and rp
        srx_pim = str(
            srx_pnf_feature_config.get("routing_instances")[0].get("protocols")
            [0].get("pim"))

        pim_interface_1 = "xe-1/0/0.1000"
        pim_interface_2 = "xe-1/0/1.1001"
        pim_rp_ip_prefix = "3.3.3"
        pim_objects = [pim_interface_1, pim_interface_2, pim_rp_ip_prefix]

        self.assertTrue(all(item in srx_pim for item in pim_objects))

        # Add qfx loopback ip to force qfx abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_dac = qfx_abstract_config.get("device_abstract_config")

        qfx_pnf_feature_config = qfx_dac.get("features").get(
            "pnf-service-chaining")

        # Verify qfx vlans
        vlans = str(qfx_pnf_feature_config.get("vlans"))

        vlan1 = "1000"
        vlan2 = "1001"

        self.assertTrue((vlan1 in vlans) and (vlan2 in vlans))

        # Verify that srx is a BGP peer of qfx
        bpg_peer_ip = (qfx_dac.get("features").get("overlay-bgp").get("bgp")
                       [0].get("peers")[0].get("ip_address"))
        self.assertEqual(bpg_peer_ip, "2.2.2.2")

        # Verify qfx left and right interfaces
        qfx_pis = str(qfx_pnf_feature_config.get("physical_interfaces"))

        qfx_left = "xe-1/1/1.0"
        qfx_right = "xe-1/1/0.0"

        self.assertTrue((qfx_left in qfx_pis) and (qfx_right in qfx_pis))

        # Verify service asns
        qfx_ris = str(qfx_pnf_feature_config.get("routing_instances"))

        asn1 = "66001"
        asn2 = "66002"

        self.assertTrue((asn1 in qfx_ris) and (asn2 in qfx_ris))

        # Verify qfx pim rp
        self.assertEqual(qfx_ris.count(pim_rp_ip_prefix), 2)

        # Verify routing instance vxlan ids
        vxlan_id_1 = str(2000 + unique_vlan_id + 24)
        vxlan_id_2 = str(2000 + unique_vlan_id + 42)
        qfx_ris = str(qfx_dac.get("routing_instances"))

        self.assertTrue((vxlan_id_1 in qfx_ris) and (vxlan_id_2 in qfx_ris))

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name())

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        # Destroy Base Objects
        self.destroy_base_objects(
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end test_config

    def create_fabric_subnet_objects(self, fabric):
        subnet_objects = []
        tag_name = "fabric-pnf-servicechain-ip" + self.id()

        # PNF Service Chain subnet
        tag_display_name = "label=" + tag_name

        pnf_tag = Tag(
            name=tag_name,
            display_name=tag_display_name,
            tag_value=tag_name,
            tag_type_name='label',
        )

        tag_uuid = self._vnc_lib.tag_create(pnf_tag)
        pnf_tag = self._vnc_lib.tag_read(id=tag_uuid)

        pnf_servicechain_subnets = [{"cidr": "1.1.1.0/24"}]

        namespace1 = self.add_cidr_namespace(
            fabric,
            "pnf-servicechain-subnets",
            pnf_servicechain_subnets,
            tag_display_name,
        )

        pnf_sc_subnets = self.carve_out_subnets(pnf_servicechain_subnets, 29)

        ipam1, fabric_vn1 = self.add_fabric_vn(fabric, "pnf-servicechain",
                                               pnf_sc_subnets, True)

        subnet_objects.append(pnf_tag)
        subnet_objects.append(namespace1)
        subnet_objects.append(ipam1)
        subnet_objects.append(fabric_vn1)

        # Loopback network
        tag_name = "fabric-loopback-ip" + self.id()
        tag_display_name = "label=" + tag_name

        lo_tag = Tag(
            name=tag_name,
            display_name=tag_display_name,
            tag_value=tag_name,
            tag_type_name='label',
        )

        tag_uuid = self._vnc_lib.tag_create(lo_tag)
        lo_tag = self._vnc_lib.tag_read(id=tag_uuid)

        loopback_subnets = [{"cidr": "3.3.3.0/24"}]

        namespace2 = self.add_cidr_namespace(fabric, "loopback-subnets",
                                             loopback_subnets,
                                             tag_display_name)

        ipam2, fabric_vn2 = self.add_fabric_vn(fabric, "loopback",
                                               loopback_subnets, False)

        subnet_objects.append(lo_tag)
        subnet_objects.append(namespace2)
        subnet_objects.append(ipam2)
        subnet_objects.append(fabric_vn2)

        return subnet_objects

    # end create_fabric_subnet_objects

    def create_base_objects(self, subnets=True):
        self.create_physical_roles(["pnf", "spine"])
        self.create_overlay_roles(["pnf-servicechain"])
        self.create_features(['overlay-bgp', 'pnf-service-chaining'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-mcast-gateway@spine',
                'physical_role': 'spine',
                'overlay_role': 'pnf-servicechain',
                'features': ['overlay-bgp'],
                'feature_configs': {}
            }),
            AttrDict({
                "name": "pnf-servicechain-pnf",
                "physical_role": "pnf",
                "overlay_role": "pnf-servicechain",
                "features": ["pnf-service-chaining"],
                "feature_configs": None,
            }),
            AttrDict({
                "name": "pnf-servicechain-spine",
                "physical_role": "spine",
                "overlay_role": "pnf-servicechain",
                "features": ["pnf-service-chaining"],
                "feature_configs": None,
            }),
        ])

        jt = self.create_job_template("job-template-1" + self.id())
        fabric = self.create_fabric("fabric-1" + self.id())
        subnet_objects = []

        if subnets:
            subnet_objects = self.create_fabric_subnet_objects(fabric)

        np_srx, rc_srx = self.create_node_profile(
            "node-profile-1" + self.id(),
            device_family="junos",
            role_mappings=[
                AttrDict({
                    "physical_role": "pnf",
                    "rb_roles": ["PNF-Servicechain"]
                })
            ],
            job_template=jt,
        )

        np_qfx, rc_qfx = self.create_node_profile(
            "node-profile-2" + self.id(),
            device_family="junos-qfx",
            role_mappings=[
                AttrDict({
                    "physical_role": "spine",
                    "rb_roles": ["CRB-MCAST-Gateway", "PNF-Servicechain"],
                })
            ],
            job_template=jt,
        )

        bgp_srx, pr_srx = self.create_router(
            "srx_router" + self.id(),
            "2.2.2.2",
            product="srx5400",
            family="junos-es",
            role="pnf",
            rb_roles=["PNF-Servicechain"],
            physical_role=self.physical_roles["pnf"],
            overlay_role=self.overlay_roles["pnf-servicechain"],
            fabric=fabric,
            node_profile=np_srx,
        )

        bgp_qfx, pr_qfx = self.create_router(
            "qfx_router" + self.id(),
            "4.4.4.4",
            product="qfx5110",
            family="junos-qfx",
            role="spine",
            rb_roles=["CRB-MCAST-Gateway", "PNF-Servicechain"],
            physical_role=self.physical_roles["spine"],
            overlay_role=self.overlay_roles["pnf-servicechain"],
            fabric=fabric,
            node_profile=np_qfx,
        )

        node_profiles = [np_srx, np_qfx]
        role_configs = [rc_srx, rc_qfx]
        physical_routers = [pr_srx, pr_qfx]
        bgp_routers = [bgp_srx, bgp_qfx]

        return (
            jt,
            fabric,
            node_profiles,
            role_configs,
            physical_routers,
            bgp_routers,
            subnet_objects,
        )

    # end create_base_objects

    def destroy_base_objects(
        self,
        job_template,
        fabric,
        node_profiles,
        role_configs,
        physical_routers,
        bgp_routers,
        subnet_objects,
    ):
        # Delete BGP Routers, Physical Routers
        for bgpr, pr in zip(bgp_routers, physical_routers):
            self.delete_routers(bgpr, pr)
            self.wait_for_routers_delete(bgpr.get_fq_name(), pr.get_fq_name())

        # Delete Role Configs
        for rc in role_configs:
            self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())

        # Delete Node Profiles:
        for np in node_profiles:
            self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())

        # Delete fabric subnet objects
        if subnet_objects:
            namespace1 = subnet_objects[1]
            namespace2 = subnet_objects[5]
            self._vnc_lib.fabric_namespace_delete(
                fq_name=namespace1.get_fq_name())
            self._vnc_lib.fabric_namespace_delete(
                fq_name=namespace2.get_fq_name())

            pnf_tag = subnet_objects[0]
            lo_tag = subnet_objects[4]
            self._vnc_lib.tag_delete(fq_name=pnf_tag.get_fq_name())
            self._vnc_lib.tag_delete(fq_name=lo_tag.get_fq_name())

        # Delete Fabric
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())

        if subnet_objects:
            fabric_vn1 = subnet_objects[3]
            fabric_vn2 = subnet_objects[7]
            self._vnc_lib.virtual_network_delete(
                fq_name=fabric_vn1.get_fq_name())
            self._vnc_lib.virtual_network_delete(
                fq_name=fabric_vn2.get_fq_name())

            ipam1 = subnet_objects[2]
            ipam2 = subnet_objects[6]
            self._vnc_lib.network_ipam_delete(fq_name=ipam1.get_fq_name())
            self._vnc_lib.network_ipam_delete(fq_name=ipam2.get_fq_name())

        # Delete Job Template
        self._vnc_lib.job_template_delete(fq_name=job_template.get_fq_name())

        self.delete_role_definitions()
        self.delete_features()
        self.wait_for_features_delete()
        self.delete_overlay_roles()
        self.delete_physical_roles()

    # end destroy_base_objects

    def create_vmi(self, vmi_id, vn):
        mac_address = "08:00:27:af:94:0" + vmi_id
        fq_name = [
            "default-domain",
            "default-project",
            "vmi" + vmi_id + "-" + self.id(),
        ]
        vmi = VirtualMachineInterface(
            fq_name=fq_name,
            parent_type="project",
            virtual_machine_interface_device_owner="baremetal:none",
            virtual_machine_interface_mac_addresses={
                "mac_address": [mac_address]
            },
        )
        vmi.add_virtual_network(vn)
        vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi)
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)
        return vmi

    # end create_vmi

    def create_lr_with_vmi(self, name, type, pr, vmi):
        lr = LogicalRouter(name)
        lr.set_logical_router_type(type)
        lr.set_physical_router(pr)
        if vmi:
            lr.set_virtual_machine_interface(vmi)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)
        return lr

    # end create_lr_with_vmi

    def create_service_appliance_set(self, name):
        sas_obj = ServiceApplianceSet(
            name=name,
            parent_type="global-system-config",
            fq_name=[self.GSC, name],
        )

        sas_obj.set_service_appliance_set_virtualization_type(
            "physical-device")

        sas_uuid = self._vnc_lib.service_appliance_set_create(sas_obj)
        sas_obj = self._vnc_lib.service_appliance_set_read(id=sas_uuid)

        return sas_obj

    # end create_service_appliance_set

    def create_service_template(self, name, sas_obj):

        st_obj = ServiceTemplate(name=name)

        st_uuid = self._vnc_lib.service_template_create(st_obj)
        st_obj.set_service_appliance_set(sas_obj)

        try:
            svc_properties = ServiceTemplateType()
            svc_properties.set_service_virtualization_type("physical-device")
            if_type = ServiceTemplateInterfaceType()
            if_type.set_service_interface_type("left")
            svc_properties.add_interface_type(if_type)
            if_type = ServiceTemplateInterfaceType()
            if_type.set_service_interface_type("right")
            svc_properties.add_interface_type(if_type)
        except AttributeError:
            print("Warning: Service template could not be fully updated ")
        else:
            st_obj.set_service_template_properties(svc_properties)
            self._vnc_lib.service_template_update(st_obj)
            st_obj = self._vnc_lib.service_template_read(id=st_uuid)
            return st_obj

    # end create_service_template

    def create_service_appliance(self, name, sas_obj):
        default_gsc_name = "default-global-system-config"
        sas_fq_name = sas_obj.get_fq_name()

        unit_test_name = sys._getframe(2).f_code.co_name
        class_name = "TestAnsiblePNFSrvcChainingDM"
        file_name = "test_dm_pnf_srvc_chaining"

        right_attachment_point = ("qfx_routertest." + file_name + "." +
                                  class_name + "." + unit_test_name +
                                  ":xe-1/1/0")
        left_attachment_point = ("qfx_routertest." + file_name + "." +
                                 class_name + "." + unit_test_name +
                                 ":xe-1/1/1")
        pnf_left_intf = ("srx_routertest." + file_name + "." + class_name +
                         "." + unit_test_name + ":xe-1/0/0")
        pnf_right_intf = ("srx_routertest." + file_name + "." + class_name +
                          "." + unit_test_name + ":xe-1/0/1")

        try:
            sas_obj = self._vnc_lib.service_appliance_set_read(
                fq_name=sas_fq_name)
        except NoIdError:
            print("Error: Service Appliance Set does not exist")
            sys.exit(-1)

        sa_obj = ServiceAppliance(name, sas_obj)

        try:
            kvp_array = []
            kvp = KeyValuePair(
                "left-attachment-point",
                default_gsc_name + ":" + left_attachment_point,
            )
            kvp_array.append(kvp)
            kvp = KeyValuePair(
                "right-attachment-point",
                default_gsc_name + ":" + right_attachment_point,
            )
            kvp_array.append(kvp)
            kvps = KeyValuePairs()
            kvps.set_key_value_pair(kvp_array)
            sa_obj.set_service_appliance_properties(kvps)
            sa_obj.set_service_appliance_virtualization_type("physical-device")
        except AttributeError:
            print("Warning: Some attributes of Service Appliance missing ")

        try:
            pnf_left_intf_obj = self._vnc_lib.physical_interface_read(fq_name=[
                default_gsc_name,
                pnf_left_intf.split(":")[0],
                pnf_left_intf.split(":")[-1],
            ])
            attr = ServiceApplianceInterfaceType(interface_type="left")
            sa_obj.add_physical_interface(pnf_left_intf_obj, attr)
        except NoIdError:
            print("Error: Left PNF interface does not exist")
            sys.exit(-1)
        except AttributeError:
            print("Error: Left PNF interface missing")
            sys.exit(-1)

        try:
            pnf_right_intf_obj = self._vnc_lib.physical_interface_read(
                fq_name=[
                    default_gsc_name,
                    pnf_right_intf.split(":")[0],
                    pnf_right_intf.split(":")[-1],
                ])
            attr = ServiceApplianceInterfaceType(interface_type="right")
            sa_obj.add_physical_interface(pnf_right_intf_obj, attr)
        except NoIdError:
            print("Error: Right PNF interface does not exist")
            sys.exit(-1)
        except AttributeError:
            print("Error: Right PNF interface missing")
            sys.exit(-1)

        sa_uuid = self._vnc_lib.service_appliance_create(sa_obj)
        sa_obj = self._vnc_lib.service_appliance_read(id=sa_uuid)

        return sa_obj

    # end create_service_appliance

    def create_service_instance(self, name, st_obj):
        si_obj = ServiceInstance(name=name)

        si_obj.add_service_template(st_obj)

        left_svc_vlan = "1000"
        right_svc_vlan = "1001"
        left_svc_asns = "66000,66001"
        right_svc_asns = "66000,66002"

        try:
            kvp_array = []
            kvp = KeyValuePair("left-svc-vlan", left_svc_vlan)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-svc-vlan", right_svc_vlan)
            kvp_array.append(kvp)
            kvp = KeyValuePair("left-svc-asns", left_svc_asns)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-svc-asns", right_svc_asns)
            kvp_array.append(kvp)
            kvps = KeyValuePairs()
            kvps.set_key_value_pair(kvp_array)
            si_obj.set_annotations(kvps)
            props = ServiceInstanceType()
            props.set_service_virtualization_type("physical-device")
            props.set_ha_mode("active-standby")
            si_obj.set_service_instance_properties(props)
        except AttributeError:
            print("Warning: Some attributes of Service Instance missing")

        si_uuid = self._vnc_lib.service_instance_create(si_obj)
        si_obj = self._vnc_lib.service_instance_read(id=si_uuid)

        return si_obj

    # end create_service_instance

    def create_port_tuple(self, name, si_obj, left_lr_name, right_lr_name):
        si_fq_name = si_obj.get_fq_name()

        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
        except NoIdError:
            print("Service Instance Not found " + (si_fq_name))
            sys.exit(-1)

        pt_obj = PortTuple(name, parent_obj=si_obj)

        try:
            left_lr_fq_name = [
                "default-domain",
                "default-project",
                left_lr_name,
            ]
            right_lr_fq_name = [
                "default-domain",
                "default-project",
                right_lr_name,
            ]
            left_lr_obj = self._vnc_lib.logical_router_read(
                fq_name=left_lr_fq_name)
            right_lr_obj = self._vnc_lib.logical_router_read(
                fq_name=right_lr_fq_name)
            pt_obj.add_logical_router(left_lr_obj)
            pt_obj.add_logical_router(right_lr_obj)
        except NoIdError as e:
            print("Error! LR not found " + str(e))
            sys.exit(-1)

        try:
            kvp_array = []
            kvp = KeyValuePair("left-lr", left_lr_obj.uuid)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-lr", right_lr_obj.uuid)
            kvp_array.append(kvp)
            kvps = KeyValuePairs()
            kvps.set_key_value_pair(kvp_array)
            pt_obj.set_annotations(kvps)
        except AttributeError:
            print("Warning: Some attributes of PT missing " + name)

        pt_uuid = self._vnc_lib.port_tuple_create(pt_obj)
        pt_obj = self._vnc_lib.port_tuple_read(id=pt_uuid)
        return pt_obj

    # end create_port_tuple

    def create_service_objects(self):
        sas_obj = self.create_service_appliance_set(name="sas" + self.id())
        st_obj = self.create_service_template(name="st" + self.id(),
                                              sas_obj=sas_obj)
        sa_obj = self.create_service_appliance(name="sa" + self.id(),
                                               sas_obj=sas_obj)
        si_obj = self.create_service_instance(name="si" + self.id(),
                                              st_obj=st_obj)
        pt_obj = self.create_port_tuple(name="pt" + self.id(),
                                        left_lr_name="left_lr" + self.id(),
                                        right_lr_name="right_lr" + self.id(),
                                        si_obj=si_obj)

        return (sas_obj, st_obj, sa_obj, si_obj, pt_obj)

    # end create_service_objects

    def delete_service_objects(self, sas, st, sa, si, pt):
        # Delete Port Tuple
        self._vnc_lib.port_tuple_delete(fq_name=pt.get_fq_name())
        # Delete Service Instance
        self._vnc_lib.service_instance_delete(fq_name=si.get_fq_name())
        # Delete Service Appliance
        self._vnc_lib.service_appliance_delete(fq_name=sa.get_fq_name())
        # Delete Service Template
        self._vnc_lib.service_template_delete(fq_name=st.get_fq_name())
        # Delete Service Appliance Set
        self._vnc_lib.service_appliance_set_delete(fq_name=sas.get_fq_name())

    # end delete_service_objects

    def check_config_with_retry(self, pr, config_key=None, retries=5):
        '''
        Checks if your device abstract config has been generated and
        retries if it is None. Number of retries is defaulted
        to 5 and can be changed by the 'retries' optional arg.

        Note: This function modifies your physical router's loopback ip
        to trigger a reaction map update and generate an abstract config.

        config_key, a list of strings, is an optional parameter
        that can be used to make sure a config portion in a deeper
        level of the device abstract config has been generated.
        If this is set, we return a tuple of the DAC and the deepest
        level config.

        Example usages:
            dac, _ = check_config_with_retry(qfx, None, retries=2)
            dac, sec_pol = check_config_with_retry(srx, ["security_policies"])
            dac, bgp = check_config_with_retry(
            qfx, ["features", "overlay-bgp", "bgp"])
        '''
        dac = None
        config = None

        while retries:
            # Set a lo0 ip to generate a config
            pr.set_physical_router_loopback_ip("5.5.0." + str(retries))
            self._vnc_lib.physical_router_update(pr)

            gevent.sleep(6 - retries)
            config = self.check_dm_ansible_config_push()

            dac = config.get("device_abstract_config")
            config = dac.copy()

            if config_key:
                for i in range(len(config_key)):
                    config = config.get(config_key[i])
            else:
                return dac, None

            if config is None:
                retries -= 1
                logger.debug("Could not find " + config_key[-1] + " in the " +
                             "abstract config. Will retry")
                logger.debug("Retries remaining: " + str(retries))
            else:
                break

        return dac, config

    # end check_config_with_retry


# end TestAnsiblePNFSrvcChainingDM
