#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import BadRequest
from vnc_api.gen.resource_client import GlobalSystemConfig
from vnc_api.gen.resource_client import PhysicalInterface
from vnc_api.gen.resource_client import PhysicalRouter
from vnc_api.gen.resource_client import ServiceAppliance
from vnc_api.gen.resource_client import ServiceApplianceSet
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.gen.resource_xsd import ServiceApplianceInterfaceType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)
# logger.setLevel(logging.DEBUG)


class TestServiceApplianceBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestServiceApplianceBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestServiceApplianceBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestPnfServiceAppliance(TestServiceApplianceBase):
    def setUp(self):
        super(TestPnfServiceAppliance, self).setUp()
        logger.debug("setUp called")
        # Create Global system config object
        self.gsc_obj = self.api.global_system_config_read(
            GlobalSystemConfig().fq_name)
        self.default_gsc_name = 'default-global-system-config'
        # Create Service Appliance Set object
        self.sas_obj = ServiceApplianceSet('sas-' + self.id(), self.gsc_obj)
        self.sas_obj.set_service_appliance_set_virtualization_type(
            'physical-device')
        self.api.service_appliance_set_create(self.sas_obj)
        # Create PNF Physical Router object
        self.pnf_obj = PhysicalRouter('pnf-' + self.id(), self.gsc_obj)
        self.pnf_obj.set_physical_router_role('pnf')
        self.api.physical_router_create(self.pnf_obj)
        # Create spine Physical Router object
        self.spine_obj = PhysicalRouter('spine-' + self.id(), self.gsc_obj)
        self.api.physical_router_create(self.spine_obj)
        # create left, right PNF PI
        self.left_pnf_pi_obj = PhysicalInterface(
            'ge-0/0/1-' + self.id(), parent_obj=self.pnf_obj)
        self.right_pnf_pi_obj = PhysicalInterface(
            'ge-0/0/2-' + self.id(), parent_obj=self.pnf_obj)
        self.api.physical_interface_create(self.left_pnf_pi_obj)
        self.api.physical_interface_create(self.right_pnf_pi_obj)
        # create left, right spine PI
        self.left_spine_pi_obj = PhysicalInterface(
            'xe-0/0/1-' + self.id(), parent_obj=self.spine_obj)
        self.right_spine_pi_obj = PhysicalInterface(
            'xe-0/0/2-' + self.id(), parent_obj=self.spine_obj)
        self.api.physical_interface_create(self.left_spine_pi_obj)
        self.api.physical_interface_create(self.right_spine_pi_obj)

    def tearDown(self):
        super(TestPnfServiceAppliance, self).tearDown()
        logger.debug("TearDown called")
        # delete PNF PI
        self.api.physical_interface_delete(id=self.left_pnf_pi_obj.uuid)
        self.api.physical_interface_delete(id=self.right_pnf_pi_obj.uuid)
        # delete spine PI
        self.api.physical_interface_delete(id=self.left_spine_pi_obj.uuid)
        self.api.physical_interface_delete(id=self.right_spine_pi_obj.uuid)
        # delete PNF PR and spine PR
        self.api.physical_router_delete(id=self.spine_obj.uuid)
        self.api.physical_router_delete(id=self.pnf_obj.uuid)
        # delete sas
        self.api.service_appliance_set_delete(id=self.sas_obj.uuid)

    def test_valid_sa(self):
        sa_obj = ServiceAppliance('sa-' + self.id(), parent_obj=self.sas_obj)
        kvp_array = []
        kvp = KeyValuePair(
            "left-attachment-point",
            self.default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/1-' +
            self.id())
        kvp_array.append(kvp)
        kvp = KeyValuePair(
            "right-attachment-point",
            self.default_gsc_name +
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

        # Add PNF PI refs
        attr = ServiceApplianceInterfaceType(interface_type='left')
        sa_obj.add_physical_interface(self.left_pnf_pi_obj, attr)
        attr = ServiceApplianceInterfaceType(interface_type='right')
        sa_obj.add_physical_interface(self.right_pnf_pi_obj, attr)

        # Create SA
        self.api.service_appliance_create(sa_obj)
        self.left_pnf_pi_obj = self.api.physical_interface_read(
            id=self.left_pnf_pi_obj.uuid)
        self.right_pnf_pi_obj = self.api.physical_interface_read(
            id=self.right_pnf_pi_obj.uuid)

        # Check if spine PI <-> PNF PI link has been created
        self.assertEqual(
            self.left_pnf_pi_obj.physical_interface_refs[0].get('uuid'),
            self.left_spine_pi_obj.uuid)
        self.assertEqual(
            self.right_pnf_pi_obj.physical_interface_refs[0].get('uuid'),
            self.right_spine_pi_obj.uuid)

        # Delete service appliance
        self.api.service_appliance_delete(id=sa_obj.uuid)

        # Check if spine PI <-> PNF PI link got removed
        self.left_pnf_pi_obj = self.api.physical_interface_read(
            id=self.left_pnf_pi_obj.uuid)
        self.right_pnf_pi_obj = self.api.physical_interface_read(
            id=self.right_pnf_pi_obj.uuid)
        self.assertFalse(
            hasattr(
                self.left_pnf_pi_obj,
                "physical_interface_refs"))
        self.assertFalse(
            hasattr(
                self.right_pnf_pi_obj,
                "physical_interface_refs"))

    def test_sa_with_invalid_kvp(self):
        sa_obj = ServiceAppliance('sa-' + self.id(), parent_obj=self.sas_obj)
        kvp_array = []
        kvp = KeyValuePair(
            "left-attachment-point",
            self.default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/1-' +
            self.id())
        kvp_array.append(kvp)
        kvp = KeyValuePair(
            "right-attachment-point",
            self.default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/2-' +
            self.id())
        # The next line is intentionally commented out
        # kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        sa_obj.set_service_appliance_properties(kvps)
        sa_obj.set_service_appliance_virtualization_type('physical-device')

        # Add PNF PI refs
        attr = ServiceApplianceInterfaceType(interface_type='left')
        sa_obj.add_physical_interface(self.left_pnf_pi_obj, attr)
        attr = ServiceApplianceInterfaceType(interface_type='right')
        sa_obj.add_physical_interface(self.right_pnf_pi_obj, attr)

        # Create SA should raise exception
        self.assertRaises(
            BadRequest,
            self.api.service_appliance_create,
            sa_obj)

    def test_sa_with_invalid_pr_role(self):
        sa_obj = ServiceAppliance('sa-' + self.id(), parent_obj=self.sas_obj)
        kvp_array = []
        kvp = KeyValuePair(
            "left-attachment-point",
            self.default_gsc_name +
            ':' +
            'spine-' +
            self.id() +
            ':' +
            'xe-0/0/1-' +
            self.id())
        kvp_array.append(kvp)
        kvp = KeyValuePair(
            "right-attachment-point",
            self.default_gsc_name +
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

        # Add PNF PI refs
        attr = ServiceApplianceInterfaceType(interface_type='left')
        sa_obj.add_physical_interface(self.left_pnf_pi_obj, attr)
        attr = ServiceApplianceInterfaceType(interface_type='right')
        sa_obj.add_physical_interface(self.right_pnf_pi_obj, attr)

        # Overwrite the role as leaf instead of pnf
        self.pnf_obj.set_physical_router_role('leaf')
        self.api.physical_router_update(self.pnf_obj)
        # Create SA should raise exception
        self.assertRaises(BadRequest,
                          self.api.service_appliance_create,
                          sa_obj)
