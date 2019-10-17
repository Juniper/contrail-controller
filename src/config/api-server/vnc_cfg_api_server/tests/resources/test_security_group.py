#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import json
import logging

from cfgm_common import SG_NO_RULE_FQ_NAME
from cfgm_common import SGID_MIN_ALLOC
from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import PermissionDenied
from testtools import ExpectedException
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import PhysicalInterface
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SecurityGroup
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VirtualPortGroup

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestSecurityGroup(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestSecurityGroup, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestSecurityGroup, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_allocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(sg_id))
        self.assertGreaterEqual(sg_id, SGID_MIN_ALLOC)

    def test_deallocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id

        self.api.security_group_delete(id=sg_obj.uuid)

        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

    def test_not_deallocate_sg_id_if_fq_name_does_not_correspond(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id

        fake_fq_name = "fake fq_name"
        mock_zk._sg_id_allocator.delete(sg_id - SGID_MIN_ALLOC)
        mock_zk._sg_id_allocator.reserve(sg_id - SGID_MIN_ALLOC, fake_fq_name)
        self.api.security_group_delete(id=sg_obj.uuid)

        self.assertIsNotNone(mock_zk.get_sg_from_id(sg_id))
        self.assertEqual(fake_fq_name, mock_zk.get_sg_from_id(sg_id))

    def test_cannot_set_sg_id(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())

        sg_obj.set_security_group_id(8000042)
        with ExpectedException(PermissionDenied):
            self.api.security_group_create(sg_obj)

    def test_cannot_update_sg_id(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)

        sg_obj.set_security_group_id(8000042)
        with ExpectedException(PermissionDenied):
            self.api.security_group_update(sg_obj)

        # test can update with same value, needed internally
        # TODO(ethuleau): not sure why it's needed
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_obj.set_security_group_id(sg_obj.security_group_id)
        self.api.security_group_update(sg_obj)

    def test_create_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        sg_obj.set_configured_security_group_id(42)

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

    def test_update_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertEqual(configured_sg_id, 0)

        sg_obj.set_configured_security_group_id(42)
        self.api.security_group_update(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

        sg_obj.set_configured_security_group_id(0)
        self.api.security_group_update(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertEqual(configured_sg_id, 0)

    def test_create_sg_with_configured_id_is_limited(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())

        sg_obj.set_configured_security_group_id(8000000)
        with ExpectedException(BadRequest):
            self.api.security_group_create(sg_obj)

        sg_obj.set_configured_security_group_id(-1)
        with ExpectedException(BadRequest):
            self.api.security_group_create(sg_obj)

    def test_update_sg_with_configured_id_is_limited(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)

        sg_obj.set_configured_security_group_id(8000000)
        with ExpectedException(BadRequest):
            self.api.security_group_update(sg_obj)

        sg_obj.set_configured_security_group_id(-1)
        with ExpectedException(BadRequest):
            self.api.security_group_update(sg_obj)

    def test_singleton_no_rule_sg_for_openstack_created(self):
        try:
            no_rule_sg = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        self.assertIsNotNone(no_rule_sg.security_group_id)
        self.assertIsInstance(no_rule_sg.security_group_id, int)
        self.assertGreater(no_rule_sg.security_group_id, 0)

    def test_singleton_no_rule_sg(self):
        try:
            no_rule_sg = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        sg_obj = SecurityGroup(name=SG_NO_RULE_FQ_NAME[-1])
        self._api_server.create_singleton_entry(sg_obj)
        try:
            no_rule_sg_2 = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))
        self.assertEqual(no_rule_sg.security_group_id,
                         no_rule_sg_2.security_group_id)

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

    def test_job_transaction(self):
        # Create project first
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        # Create Fabric with enterprise style flag set to false
        fabric_obj = Fabric('%s-fabric' % (self.id()))
        fabric_obj.set_fabric_enterprise_style(True)
        fabric_uuid = self.api.fabric_create(fabric_obj)
        fabric_obj = self.api.fabric_read(id=fabric_uuid)

        # Create physical router
        pr_name = self.id() + '_physical_router'
        pr = PhysicalRouter(pr_name)
        pr_uuid = self._vnc_lib.physical_router_create(pr)
        pr_obj = self._vnc_lib.physical_router_read(id=pr_uuid)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi1_name = self.id() + '_physical_interface1'
        pi1 = PhysicalInterface(name=pi1_name,
                                parent_obj=pr_obj,
                                ethernet_segment_identifier=esi_id)
        pi1_uuid = self._vnc_lib.physical_interface_create(pi1)
        pi1_obj = self._vnc_lib.physical_interface_read(id=pi1_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi1_fq_name = pi1_obj.get_fq_name()

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
        kv_pairs = self._create_kv_pairs(pi1_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))

        # Create security group and attach to VMI
        sg_name = '%s-sg' % self.id()
        sg_obj = SecurityGroup(sg_name)
        sg_uuid_1 = self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_uuid_1)
        vmi_obj.add_security_group(sg_obj)

        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)

        sg_obj = self.api.security_group_read(
            id=sg_uuid_1,
            fields=['virtual_machine_interface_back_refs'])
        sg_obj.set_configured_security_group_id(5)
        self.api.security_group_update(sg_obj)
        self.assertEqual("Security Group '%s' Update" % sg_name,
                         self._get_job_transaction_descr(pr_obj.uuid))

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.security_group_delete(id=sg_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi1_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
