#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging

from vnc_api.vnc_api import AutonomousSystemsType
from vnc_api.vnc_api import DeviceChassis
from vnc_api.vnc_api import DeviceFunctionalGroup
from vnc_api.vnc_api import DnsmasqLeaseParameters
from vnc_api.vnc_api import ExecutableInfoListType, ExecutableInfoType
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import HardwareInventory
from vnc_api.vnc_api import JobTemplate
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutingBridgingRolesType
from vnc_api.vnc_api import StormControlParameters, StormControlProfile
from vnc_api.vnc_api import TelemetryProfile

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestFabricObjects(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFabricObjects, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFabricObjects, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestFabricObjects, self).setUp()
        gsc_fq_name = GlobalSystemConfig().fq_name
        self.gsc = self.api.global_system_config_read(gsc_fq_name)

    @property
    def api(self):
        return self._vnc_lib

    def test_job_template_basic_crud(self):
        jt = JobTemplate(parent_obj=self.gsc)
        uuid = self.api.job_template_create(jt)
        self.assertIsNotNone(uuid)
        jt.set_uuid(uuid)

        exe_info_list = ExecutableInfoListType()
        exe_info_list.set_executable_info([
            ExecutableInfoType(executable_path='/tmp/fake',
                               executable_args='a b c',
                               job_completion_weightage=10)])
        jt.set_job_template_executables(exe_info_list)
        self.api.job_template_update(jt)

        updated_jt = self.api.job_template_read(id=jt.uuid)

        ei1 = jt.job_template_executables.executable_info[0]
        ei2 = updated_jt.job_template_executables.executable_info[0]
        for attr in ['executable_path',
                     'executable_args',
                     'job_completion_weightage']:
            self.assertEqual(getattr(ei1, attr), getattr(ei2, attr))

        self.api.job_template_delete(id=jt.uuid)

    def test_fabric_basic_crud(self):
        f = Fabric(name='f-%s' % self.id(), parent_obj=self.gsc)
        f.set_fabric_os_version('junos')
        f.set_fabric_enterprise_style(False)
        uuid = self.api.fabric_create(f)
        f.set_uuid(uuid)

        f.set_fabric_os_version('prioprietary')
        self.api.fabric_update(f)
        f_updated = self.api.fabric_read(id=f.uuid)

        for attr in ['fabric_os_version', 'fabric_enterprise_style']:
            self.assertEqual(getattr(f, attr), getattr(f_updated, attr))

        self.api.fabric_delete(id=f.uuid)

    def test_device_chassis_basic_crud(self):
        dc = DeviceChassis(name='dc-%s' % self.id())
        dc.set_device_chassis_type('fake_chassis_type')
        uuid = self.api.device_chassis_create(dc)
        dc.set_uuid(uuid)
        dc.set_device_chassis_type('other_chassis_type')
        self.api.device_chassis_update(dc)

        dc_updated = self.api.device_chassis_read(id=dc.uuid)
        self.assertEqual(dc.device_chassis_type,
                         dc_updated.device_chassis_type)

        self.api.device_chassis_delete(id=dc.uuid)

    def test_device_functional_group(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        dfg = DeviceFunctionalGroup(parent_obj=project)
        dfg.set_device_functional_group_description('some description')
        dfg.set_device_functional_group_os_version('junos')

        bridging_roles = RoutingBridgingRolesType(rb_roles=['CRB', 'ERB'])
        dfg.set_device_functional_group_routing_bridging_roles(bridging_roles)

        uuid = self.api.device_functional_group_create(dfg)
        dfg.set_uuid(uuid)

        dfg.set_device_functional_group_os_version('proprietary')
        self.api.device_functional_group_update(dfg)
        dfg_updated = self.api.device_functional_group_read(id=dfg.uuid)

        self.assertEqual(dfg.device_functional_group_os_version,
                         dfg_updated.device_functional_group_os_version)

        rb1 = dfg\
            .get_device_functional_group_routing_bridging_roles()\
            .get_rb_roles()
        rb2 = dfg_updated\
            .get_device_functional_group_routing_bridging_roles()\
            .get_rb_roles()
        self.assertListEqual(rb1, rb2)

        self.api.device_functional_group_delete(id=dfg.uuid)

    def test_hardware_inventory_basic_crud(self):
        pr = PhysicalRouter(name='pr-%s' % self.id(), parent_obj=self.gsc)
        uuid = self.api.physical_router_create(pr)
        pr.set_uuid(uuid)

        hi = HardwareInventory(name='hi-%s' % self.id(), parent_obj=pr)
        hi.set_hardware_inventory_inventory_info('{"id": 123, "name": "fake"}')

        uuid = self.api.hardware_inventory_create(hi)
        hi.set_uuid(uuid)

        hi.set_hardware_inventory_inventory_info('{"id": 456, "name": "fake"}')
        self.api.hardware_inventory_update(hi)

        hi_updated = self.api.hardware_inventory_read(id=hi.uuid)

        self.assertEqual(hi.hardware_inventory_inventory_info,
                         hi_updated.hardware_inventory_inventory_info)

        self.api.hardware_inventory_delete(id=hi.uuid)
        self.api.physical_router_delete(id=pr.uuid)

    def test_physical_router_basic_crud(self):
        pr = PhysicalRouter(name='pr-%s' % self.id(), parent_obj=self.gsc)
        pr.set_physical_router_encryption_type('none')
        pr.set_physical_router_dhcp_parameters(DnsmasqLeaseParameters(
            lease_expiry_time=100,
            client_id='4d972916-3204-11ea-a6fa-d7a3d77d36a2'))
        # TODO: obj has no attribute error, but it is defined.
        # pr.set_physical_router_supplemental_config('dummy config')
        pr.set_physical_router_autonomous_system(
            AutonomousSystemsType(asn=[294967295]))

        uuid = self.api.physical_router_create(pr)
        pr.set_uuid(uuid)

        pr.set_physical_router_encryption_type('local')
        pr.set_physical_router_autonomous_system(
            AutonomousSystemsType(asn=[967295]))
        self.api.physical_router_update(pr)
        pr_updated = self.api.physical_router_read(id=pr.uuid)

        self.assertEqual(pr.physical_router_encryption_type,
                         pr_updated.physical_router_encryption_type)
        self.assertEqual(pr.physical_router_autonomous_system.asn,
                         pr_updated.physical_router_autonomous_system.asn)

        # TODO: obj has no attribute error, but it is defined.
        # self.assertEqual(pr.physical_router_supplemental_config,
        #                  pr_updated.physical_router_supplemental_config)

        self.api.physical_router_delete(id=pr.uuid)
