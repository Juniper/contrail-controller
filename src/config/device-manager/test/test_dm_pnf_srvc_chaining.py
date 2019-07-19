#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
import sys
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsiblePNFSrvcChainingDM(TestAnsibleCommonDM):
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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Change srx's physical role from pnf to spine
        srx.physical_router_role = "spine"
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()
        srx_device_abstract_config = srx_abstract_config.get(
            "device_abstract_config"
        )

        # srx abstract config will not have security zones now
        security_policies = srx_device_abstract_config.get("security_policies")
        self.assertIsNone(security_policies)

        # Adding it back should generate the full service chaining config
        srx.physical_router_role = "pnf"
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()
        srx_device_abstract_config = srx_abstract_config.get(
            "device_abstract_config"
        )
        security_policies = srx_device_abstract_config.get("security_policies")
        self.assertIsNotNone(security_policies)

        # Remove PNF as a rb role from qfx
        qfx.routing_bridging_roles = RoutingBridgingRolesType(
            rb_roles=["CRB-MCAST-Gateway"]
        )
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()
        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config"
        )

        # There should be no vlans key in the abstract config now
        vlans = qfx_device_abstract_config.get("vlans")
        self.assertIsNone(vlans)

        # Adding the pnf role back should bring it back
        qfx.routing_bridging_roles = RoutingBridgingRolesType(
            rb_roles=["CRB-MCAST-Gateway", "PNF-Servicechain"]
        )
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()
        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config"
        )

        # There should be no vlans key in the abstract config now
        vlans = qfx_device_abstract_config.get("vlans")
        self.assertIsNotNone(vlans)

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

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
            "device_abstract_config"
        )
        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances"
        )

        # When lr-type is not set to vxlan-routing, there will
        # be no internal vn's created
        internal_vn_identifier = '"virtual_network_is_internal": true'
        self.assertTrue(
            internal_vn_identifier not in str(qfx_routing_instances)
        )

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

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

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config"
        )

        # Physical Interfaces now should not contain interfaces of type service
        phy_intfs = str(qfx_device_abstract_config.get("physical_interfaces"))
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
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

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

        srx_device_abstract_config = srx_abstract_config.get(
            "device_abstract_config"
        )
        physical_interfaces = srx_device_abstract_config.get(
            "physical_interfaces"
        )
        self.assertIsNone(physical_interfaces)

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

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
            "device_abstract_config"
        )

        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances"
        )

        self.assertFalse(
            ("2024" in str(qfx_routing_instances))
            and ("2042" in str(qfx_routing_instances))
        )

        llr.set_virtual_machine_interface(vmi_1)
        self._vnc_lib.logical_router_update(llr)

        rlr.set_virtual_machine_interface(vmi_2)
        self._vnc_lib.logical_router_update(rlr)

        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config"
        )

        qfx_routing_instances = qfx_device_abstract_config.get(
            "routing_instances"
        )

        self.assertTrue(
            ("2024" in str(qfx_routing_instances))
            and ("2042" in str(qfx_routing_instances))
        )

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

        gevent.sleep(1)

        # Add loopback ip to force abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)

        gevent.sleep(0.1)
        srx_abstract_config = self.check_dm_ansible_config_push()
        srx_device_abstract_config = srx_abstract_config.get(
            "device_abstract_config"
        )

        srx_routing_instances = srx_device_abstract_config.get(
            "routing_instances"
        )

        self.assertIsNone(srx_routing_instances)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        vlan_id_1 = 24
        vlan_id_2 = 42

        vn1 = self.create_vn(str(vlan_id_1), "7.7.7.0")
        vn2 = self.create_vn(str(vlan_id_2), "8.8.8.0")

        # Create VMI
        vmi_1 = self.create_vmi(str(vlan_id_1), vn1)
        vmi_2 = self.create_vmi(str(vlan_id_2), vn2)

        # Create Left Logical Router
        llr = LogicalRouter("left_lr")
        llr.set_logical_router_type("vxlan-routing")
        llr.set_physical_router(qfx)
        llr.set_virtual_machine_interface(vmi_1)
        llr_uuid = self._vnc_lib.logical_router_create(llr)
        llr = self._vnc_lib.logical_router_read(id=llr_uuid)

        # Create Right Logical Router
        rlr = LogicalRouter("right_lr")
        rlr.set_logical_router_type("vxlan-routing")
        rlr.set_physical_router(qfx)
        rlr.set_virtual_machine_interface(vmi_2)
        rlr_uuid = self._vnc_lib.logical_router_create(rlr)
        rlr = self._vnc_lib.logical_router_read(id=rlr_uuid)

        # Create Service Objects
        (
            sas_obj,
            st_obj,
            sa_obj,
            si_obj,
            pt_obj,
        ) = self.create_service_objects()

        # Add srx loopback ip to force srx abstract config generation
        srx.set_physical_router_loopback_ip("5.5.0.1")
        self._vnc_lib.physical_router_update(srx)
        gevent.sleep(1)
        srx_abstract_config = self.check_dm_ansible_config_push()

        srx_device_abstract_config = srx_abstract_config.get(
            "device_abstract_config"
        )

        # Verify security policies
        security_policies = srx_device_abstract_config.get("security_policies")

        sc_pol_1_to = security_policies[0].get("to_zone")
        self.assertEqual(sc_pol_1_to, "si_right")
        sc_pol_1_from = security_policies[0].get("from_zone")
        self.assertEqual(sc_pol_1_from, "si_left")

        sc_pol_2_to = security_policies[1].get("to_zone")
        self.assertEqual(sc_pol_2_to, "si_left")
        sc_pol_2_from = security_policies[1].get("from_zone")
        self.assertEqual(sc_pol_2_from, "si_right")

        # Verify security zones
        security_zones = srx_device_abstract_config.get("security_zones")

        security_zone_left = security_zones[0].get("interfaces")[0].get("name")
        self.assertEqual(security_zone_left, "xe-1/0/0.1000")

        security_zone_right = (
            security_zones[1].get("interfaces")[0].get("name")
        )
        self.assertEqual(security_zone_right, "xe-1/0/1.1001")

        # Verify srx left and right interfaces
        srx_left = (
            srx_device_abstract_config.get("physical_interfaces")[1]
            .get("logical_interfaces")[0]
            .get("name")
        )
        self.assertEqual(srx_left, "xe-1/0/0.1000")

        srx_right = (
            srx_device_abstract_config.get("physical_interfaces")[2]
            .get("logical_interfaces")[0]
            .get("name")
        )
        self.assertEqual(srx_right, "xe-1/0/1.1001")

        # Verify loopback interfaces
        srx_lo0 = (
            srx_device_abstract_config.get("routing_instances")[0]
            .get("loopback_interfaces")[0]
            .get("name")
        )
        self.assertEqual(srx_lo0, "lo0.1000")

        # Verify bgp
        srx_bgp = (
            srx_device_abstract_config.get("routing_instances")[0]
            .get("protocols")
            .get("bgp")
        )

        left_bgp_grp_name = srx_bgp[0].get("name")
        self.assertEqual(left_bgp_grp_name, "si_left")

        left_bgp_grp_ip = srx_bgp[0].get("ip_address")
        self.assertTrue(left_bgp_grp_ip.startswith("1.1.1"))

        right_bgp_grp_name = srx_bgp[1].get("name")
        self.assertEqual(right_bgp_grp_name, "si_right")

        right_bgp_grp_ip = srx_bgp[1].get("ip_address")
        self.assertTrue(right_bgp_grp_ip.startswith("1.1.1"))

        # Verify pim interfaces and rp
        srx_pim = (
            srx_device_abstract_config.get("routing_instances")[0]
            .get("protocols")
            .get("pim")[0]
        )

        pim_interface_1 = (
            srx_pim.get("pim_interfaces")[0].get("interface").get("name")
        )
        self.assertEqual(pim_interface_1, "xe-1/0/0.1000")

        pim_interface_2 = (
            srx_pim.get("pim_interfaces")[1].get("interface").get("name")
        )
        self.assertEqual(pim_interface_2, "xe-1/0/1.1001")

        pim_rp = srx_pim.get("rp").get("ip_address")
        self.assertTrue(pim_rp.startswith("3.3.3"))

        # Add qfx loopback ip to force qfx abstract config generation
        qfx.set_physical_router_loopback_ip("6.6.0.1")
        self._vnc_lib.physical_router_update(qfx)
        gevent.sleep(1)
        qfx_abstract_config = self.check_dm_ansible_config_push()

        qfx_device_abstract_config = qfx_abstract_config.get(
            "device_abstract_config"
        )

        # Verify qfx vlans
        vlan1 = qfx_device_abstract_config.get("vlans")[0].get("vlan_id")
        self.assertEqual(vlan1, "1000")
        vlan2 = qfx_device_abstract_config.get("vlans")[1].get("vlan_id")
        self.assertEqual(vlan2, "1001")

        # Verify that srx is a BGP peer of qfx
        bpg_peer_ip = (
            qfx_device_abstract_config.get("bgp")[0]
            .get("peers")[0]
            .get("ip_address")
        )
        self.assertEqual(bpg_peer_ip, "2.2.2.2")

        # Verify qfx left and right interfaces
        qfx_right = (
            qfx_device_abstract_config.get("physical_interfaces")[2]
            .get("logical_interfaces")[0]
            .get("name")
        )
        self.assertEqual(qfx_right, "xe-1/1/0.0")
        qfx_left = (
            qfx_device_abstract_config.get("physical_interfaces")[3]
            .get("logical_interfaces")[0]
            .get("name")
        )
        self.assertEqual(qfx_left, "xe-1/1/1.0")

        # Verify service asns
        qfx_ri1_bgp = (
            qfx_device_abstract_config.get("routing_instances")[0]
            .get("protocols")
            .get("bgp")[0]
        )

        qfx_ri1_bgp_asn = qfx_ri1_bgp.get("autonomous_system")
        qfx_ri1_type = (
            "left" if ("left" in qfx_ri1_bgp.get("name")) else "right"
        )

        qfx_ri2_bgp = (
            qfx_device_abstract_config.get("routing_instances")[1]
            .get("protocols")
            .get("bgp")[0]
        )
        qfx_ri2_bgp_asn = qfx_ri2_bgp.get("autonomous_system")

        if qfx_ri1_type is "left":
            self.assertEqual(qfx_ri1_bgp_asn, "66001")
            self.assertEqual(qfx_ri2_bgp_asn, "66002")
        else:
            self.assertEqual(qfx_ri2_bgp_asn, "66001")
            self.assertEqual(qfx_ri1_bgp_asn, "66002")

        # Verify qfx pim rp
        pim1_rp = (
            qfx_device_abstract_config.get("routing_instances")[0]
            .get("protocols")
            .get("pim")[0]
            .get("rp")
            .get("ip_address")
        )
        self.assertTrue(pim1_rp.startswith("3.3.3"))

        pim2_rp = (
            qfx_device_abstract_config.get("routing_instances")[0]
            .get("protocols")
            .get("pim")[0]
            .get("rp")
            .get("ip_address")
        )
        self.assertTrue(pim2_rp.startswith("3.3.3"))

        # Verify routing instance vxlan ids
        vxlan_id_1 = qfx_device_abstract_config.get("routing_instances")[
            2
        ].get("vxlan_id")
        self.assertEqual(vxlan_id_1, "2024")

        vxlan_id_2 = qfx_device_abstract_config.get("routing_instances")[
            5
        ].get("vxlan_id")
        self.assertEqual(vxlan_id_2, "2042")

        # Destroy service objects
        self.delete_service_objects(sas_obj, st_obj, sa_obj, si_obj, pt_obj)

        # Destroy Logical Routers
        for lr in [llr, rlr]:
            self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        # Destroy vmis, vns and physical interfaces
        for vmi in [vmi_1, vmi_2]:
            self._vnc_lib.virtual_machine_interface_delete(
                fq_name=vmi.get_fq_name()
            )

        for vn in [vn1, vn2]:
            self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name()
            )

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
        tag_name = "label"

        # PNF Service Chain subnet
        tag_display_name = "label=fabric-pnf-servicechain-ip"
        tag_value = "fabric-pnf-servicechain-ip"

        pnf_tag = Tag(
            name=tag_name,
            display_name=tag_display_name,
            tag_value=tag_value,
            tag_type_name=tag_name,
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

        ipam1, fabric_vn1 = self.add_fabric_vn(
            fabric, "pnf-servicechain", pnf_sc_subnets, True
        )

        subnet_objects.append(pnf_tag)
        subnet_objects.append(namespace1)
        subnet_objects.append(ipam1)
        subnet_objects.append(fabric_vn1)

        # Loopback network
        tag_display_name = "label=fabric-loopback-ip"
        tag_value = "fabric-loopback-ip"

        lo_tag = Tag(
            name=tag_name,
            display_name=tag_display_name,
            tag_value=tag_value,
            tag_type_name=tag_name,
        )

        tag_uuid = self._vnc_lib.tag_create(lo_tag)
        lo_tag = self._vnc_lib.tag_read(id=tag_uuid)

        loopback_subnets = [{"cidr": "3.3.3.0/24"}]

        namespace2 = self.add_cidr_namespace(
            fabric, "loopback-subnets", loopback_subnets, tag_display_name
        )

        ipam2, fabric_vn2 = self.add_fabric_vn(
            fabric, "loopback", loopback_subnets, False
        )

        subnet_objects.append(lo_tag)
        subnet_objects.append(namespace2)
        subnet_objects.append(ipam2)
        subnet_objects.append(fabric_vn2)

        return subnet_objects

    # end create_fabric_subnet_objects

    def create_base_objects(self, subnets=True):
        jt = self.create_job_template("job-template-1")
        fabric = self.create_fabric("fabric-1")
        subnet_objects = []

        if subnets:
            subnet_objects = self.create_fabric_subnet_objects(fabric)

        np_srx, rc_srx = self.create_node_profile(
            "node-profile-1",
            device_family="junos",
            role_mappings=[
                AttrDict(
                    {"physical_role": "pnf", "rb_roles": ["PNF-Servicechain"]}
                )
            ],
            job_template=jt,
        )

        np_qfx, rc_qfx = self.create_node_profile(
            "node-profile-2",
            device_family="junos-qfx",
            role_mappings=[
                AttrDict(
                    {
                        "physical_role": "spine",
                        "rb_roles": ["CRB-MCAST-Gateway", "PNF-Servicechain"],
                    }
                )
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
                fq_name=namespace1.get_fq_name()
            )
            self._vnc_lib.fabric_namespace_delete(
                fq_name=namespace2.get_fq_name()
            )

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
                fq_name=fabric_vn1.get_fq_name()
            )
            self._vnc_lib.virtual_network_delete(
                fq_name=fabric_vn2.get_fq_name()
            )

            ipam1 = subnet_objects[2]
            ipam2 = subnet_objects[6]
            self._vnc_lib.network_ipam_delete(fq_name=ipam1.get_fq_name())
            self._vnc_lib.network_ipam_delete(fq_name=ipam2.get_fq_name())

        # Delete Job Template
        self._vnc_lib.job_template_delete(fq_name=job_template.get_fq_name())

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

    def create_service_appliance_set(self, name):
        sas_obj = ServiceApplianceSet(
            name=name,
            parent_type="global-system-config",
            fq_name=[self.GSC, name],
        )

        sas_obj.set_service_appliance_set_virtualization_type(
            "physical-device"
        )

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

        right_attachment_point = (
            "qfx_routertest."
            + file_name
            + "."
            + class_name
            + "."
            + unit_test_name
            + ":xe-1/1/0"
        )
        left_attachment_point = (
            "qfx_routertest."
            + file_name
            + "."
            + class_name
            + "."
            + unit_test_name
            + ":xe-1/1/1"
        )
        pnf_left_intf = (
            "srx_routertest."
            + file_name
            + "."
            + class_name
            + "."
            + unit_test_name
            + ":xe-1/0/0"
        )
        pnf_right_intf = (
            "srx_routertest."
            + file_name
            + "."
            + class_name
            + "."
            + unit_test_name
            + ":xe-1/0/1"
        )

        try:
            sas_obj = self._vnc_lib.service_appliance_set_read(
                fq_name=sas_fq_name
            )
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
            pnf_left_intf_obj = self._vnc_lib.physical_interface_read(
                fq_name=[
                    default_gsc_name,
                    pnf_left_intf.split(":")[0],
                    pnf_left_intf.split(":")[-1],
                ]
            )
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
                ]
            )
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

    def create_port_tuple(self, name, si_obj):
        si_fq_name = si_obj.get_fq_name()
        left_lr_name = "left_lr"
        right_lr_name = "right_lr"

        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
        except NoIdError:
            print("Service Instance Not found " + (si_name))
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
                fq_name=left_lr_fq_name
            )
            right_lr_obj = self._vnc_lib.logical_router_read(
                fq_name=right_lr_fq_name
            )
            pt_obj.add_logical_router(left_lr_obj)
            pt_obj.add_logical_router(right_lr_obj)
        except NoIdError as e:
            print("Error! LR not found " + (e.message))
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
            print("Warning: Some attributes of PT missing " + pt_name)

        pt_uuid = self._vnc_lib.port_tuple_create(pt_obj)
        pt_obj = self._vnc_lib.port_tuple_read(id=pt_uuid)
        return pt_obj

    # end create_port_tuple

    def create_service_objects(self):
        sas_obj = self.create_service_appliance_set(name="sas")
        st_obj = self.create_service_template(name="st", sas_obj=sas_obj)
        sa_obj = self.create_service_appliance(name="sa", sas_obj=sas_obj)
        si_obj = self.create_service_instance(name="si", st_obj=st_obj)
        pt_obj = self.create_port_tuple(name="pt", si_obj=si_obj)

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


# end TestAnsiblePNFSrvcChainingDM
