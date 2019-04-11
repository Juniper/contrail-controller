#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import BadRequest
from vnc_api.gen.resource_client import GlobalSystemConfig
from vnc_api.gen.resource_client import ServiceApplianceSet
from vnc_api.gen.resource_client import ServiceInstance
from vnc_api.gen.resource_client import ServiceTemplate
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.gen.resource_xsd import ServiceInstanceType
from vnc_api.gen.resource_xsd import ServiceTemplateInterfaceType
from vnc_api.gen.resource_xsd import ServiceTemplateType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)
# logger.setLevel(logging.DEBUG)


class TestServiceInstanceBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestServiceInstanceBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestServiceInstanceBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestPnfServiceInstance(TestServiceInstanceBase):
    def setUp(self):
        super(TestPnfServiceInstance, self).setUp()
        logger.debug("setUp called")
        # Create Global system config object
        gsc_obj = self.api.global_system_config_read(
            GlobalSystemConfig().fq_name)
        # Create Service Appliance Set object
        sas_obj = ServiceApplianceSet('sas-' + self.id(), gsc_obj)
        sas_obj.set_service_appliance_set_virtualization_type(
            'physical-device')
        self.sas_uuid = self.api.service_appliance_set_create(sas_obj)
        # Create Service template object
        self.st_obj = ServiceTemplate(name='st-' + self.id())
        self.st_obj.set_service_appliance_set(sas_obj)
        svc_properties = ServiceTemplateType()
        svc_properties.set_service_virtualization_type('physical-device')
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('left')
        svc_properties.add_interface_type(if_type)
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('right')
        svc_properties.add_interface_type(if_type)
        self.st_obj.set_service_template_properties(svc_properties)
        self.st_uuid = self.api.service_template_create(self.st_obj)

    def tearDown(self):
        super(TestPnfServiceInstance, self).tearDown()
        logger.debug("TearDown called")
        self.api.service_template_delete(id=self.st_uuid)
        self.api.service_appliance_set_delete(id=self.sas_uuid)

    def test_valid_si(self):
        si_fqn = ['default-domain', 'default-project', 'si-' + self.id()]
        si_obj = ServiceInstance(fq_name=si_fqn)
        si_obj.fq_name = si_fqn
        si_obj.add_service_template(self.st_obj)
        kvp_array = []
        kvp = KeyValuePair("left-svc-vlan", "100")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-vlan", "101")
        kvp_array.append(kvp)
        kvp = KeyValuePair("left-svc-asns", "66000,66001")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-asns", "66000,66002")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        si_obj.set_annotations(kvps)
        props = ServiceInstanceType()
        props.set_service_virtualization_type('physical-device')
        props.set_ha_mode("active-standby")
        si_obj.set_service_instance_properties(props)

        si_uuid = self.api.service_instance_create(si_obj)
        si_obj = self.api.service_instance_read(id=si_uuid)
        # Expected - [key = left-svc-unit, value = 5, key = right-svc-unit,
        # value = 6]
        self.assertEqual(
            len(si_obj.service_instance_bindings.key_value_pair), 2)

        # cleanup
        self.api.service_instance_delete(id=si_uuid)

    def test_si_without_valid_annotations(self):
        si_fqn = ['default-domain', 'default-project', 'si-' + self.id()]
        si_obj = ServiceInstance(fq_name=si_fqn)
        si_obj.fq_name = si_fqn
        si_obj.add_service_template(self.st_obj)
        kvp_array = []
        kvp = KeyValuePair("left-svc-vlan", "100")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-vlan", "101")
        kvp_array.append(kvp)
        kvp = KeyValuePair("left-svc-asns", "66000,66001")
        kvp_array.append(kvp)
        # The below line is intentionllly commented out..
        # kvp = KeyValuePair("right-svc-asns", "66000,66002")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        si_obj.set_annotations(kvps)
        props = ServiceInstanceType()
        props.set_service_virtualization_type('physical-device')
        props.set_ha_mode("active-standby")
        si_obj.set_service_instance_properties(props)

        # right-svc-asns missing, raise error
        self.assertRaises(BadRequest, self.api.service_instance_create, si_obj)
