#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import PermissionDenied
from vnc_api.gen.resource_client import LogicalInterface
from vnc_api.gen.resource_client import PhysicalInterface
from vnc_api.gen.resource_client import PhysicalRouter
from vnc_api.gen.resource_client import VirtualMachine
from vnc_api.gen.resource_client import VirtualMachineInterface
from vnc_api.gen.resource_client import VirtualNetwork

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestPhysicalInterface(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPhysicalInterface, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPhysicalInterface, cls).tearDownClass(*args, **kwargs)

    def _create_vmi(self, vmi_name, vn_obj, mac='00:11:22:33:44:55'):
        vm_name = vmi_name + '_test_vm'
        vm = VirtualMachine(vm_name)
        vm_uuid = self._vnc_lib.virtual_machine_create(vm)
        vm_obj = self._vnc_lib.virtual_machine_read(id=vm_uuid)

        vmi = VirtualMachineInterface(
            name=vmi_name, parent_obj=vm_obj,
            virtual_machine_interface_mac_address=mac)
        vmi.add_virtual_network(vn_obj)
        vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi)
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)
        return vmi_obj

    def test_physical_interface_esi(self):
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

        li1_name = pi1_name + '_li1'
        li1 = LogicalInterface(name=li1_name,
                               parent_obj=pi1_obj,
                               logical_interface_vlan_tag=2)

        vn_name = self.id() + '_test_vn'
        vn = VirtualNetwork(vn_name)
        vn_uuid = self._vnc_lib.virtual_network_create(vn)
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_uuid)

        vmi_name = self.id() + '_common_vmi'
        vmi_obj = self._create_vmi(vmi_name, vn_obj)

        li1.add_virtual_machine_interface(vmi_obj)
        li1_uuid = self._vnc_lib.logical_interface_create(li1)
        self._vnc_lib.logical_interface_read(id=li1_uuid)

        pi2_name = self.id() + '_physical_interface2'
        pi2 = PhysicalInterface(name=pi2_name,
                                parent_obj=pr_obj,
                                ethernet_segment_identifier=esi_id)
        pi2_uuid = self._vnc_lib.physical_interface_create(pi2)
        pi2_obj = self._vnc_lib.physical_interface_read(id=pi2_uuid)

        li2_name = pi2_name + '_li2'
        li2 = LogicalInterface(name=li2_name,
                               parent_obj=pi2_obj,
                               logical_interface_vlan_tag=2)

        second_vmi_name = self.id() + '_2nd_vmi'
        second_vmi_obj = self._create_vmi(second_vmi_name, vn_obj,
                                          'AA:AA:AA:AA:AA:AA')
        li2.add_virtual_machine_interface(second_vmi_obj)

        self.assertRaises(
            PermissionDenied, self._vnc_lib.logical_interface_create, li2)

        li2 = LogicalInterface(name=li2_name,
                               parent_obj=pi2_obj,
                               logical_interface_vlan_tag=2)
        li2_uuid = self._vnc_lib.logical_interface_create(li2)
        li2_obj = self._vnc_lib.logical_interface_read(id=li2_uuid)
        li2_obj.add_virtual_machine_interface(second_vmi_obj)

        self.assertRaises(
            PermissionDenied, self._vnc_lib.logical_interface_update, li2_obj)

        pi3_name = self.id() + '_physical_interface3'
        pi3 = PhysicalInterface(
            name=pi3_name,
            parent_obj=pr_obj,
            ethernet_segment_identifier='00:00:00:00:00:00:00:00:00:AA')
        pi3_uuid = self._vnc_lib.physical_interface_create(pi3)
        pi3_obj = self._vnc_lib.physical_interface_read(id=pi3_uuid)

        li3_name = pi3_name + '_li3'
        li3 = LogicalInterface(name=li3_name,
                               parent_obj=pi3_obj,
                               logical_interface_vlan_tag=2)

        li3.add_virtual_machine_interface(vmi_obj)
        self._vnc_lib.logical_interface_create(li3)
        pi3_obj.set_ethernet_segment_identifier(esi_id)
        self._vnc_lib.physical_interface_update(pi3_obj)

    def test_physical_interface_ae_id_alloc_and_free(self):
        AE_ID_TEST_CASES = [3, 127]

        mock_zk = self._api_server._db_conn._zk_db

        pr_name = self.id() + '_physical_router'
        pr = PhysicalRouter(pr_name)
        pr_uuid = self._vnc_lib.physical_router_create(pr)
        pr_obj = self._vnc_lib.physical_router_read(id=pr_uuid)

        for ae_id in AE_ID_TEST_CASES:
            pi_name = 'ae' + str(ae_id)
            pi = PhysicalInterface(name=pi_name, parent_obj=pr_obj)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            self.assertEqual(
                pi_name, mock_zk.ae_id_is_occupied(pr_name, ae_id))
            self._vnc_lib.physical_interface_delete(id=pi_uuid)
            self.assertFalse(mock_zk.ae_id_is_occupied(pr_name, ae_id))

        self._vnc_lib.physical_router_delete(id=pr_uuid)

    def test_physical_interface_ae_id_is_busy(self):
        AE_ID = 3
        NAME_OF_OBJECT_OCCUPING_AE_ID = 'VPG_1'

        mock_zk = self._api_server._db_conn._zk_db

        pr_name = self.id() + '_physical_router'
        pr = PhysicalRouter(pr_name)
        pr_uuid = self._vnc_lib.physical_router_create(pr)
        pr_obj = self._vnc_lib.physical_router_read(id=pr_uuid)

        pi_name = 'ae' + str(AE_ID)
        mock_zk.alloc_ae_id(pr_name, NAME_OF_OBJECT_OCCUPING_AE_ID, AE_ID)
        self.assertEqual(NAME_OF_OBJECT_OCCUPING_AE_ID,
                         mock_zk.ae_id_is_occupied(pr_name, AE_ID))

        pi = PhysicalInterface(name=pi_name, parent_obj=pr_obj)
        self.assertRaises(
            PermissionDenied, self._vnc_lib.physical_interface_create, pi)
        self.assertEqual(NAME_OF_OBJECT_OCCUPING_AE_ID,
                         mock_zk.ae_id_is_occupied(pr_name, AE_ID))

        self._vnc_lib.physical_router_delete(id=pr_uuid)
        mock_zk.free_ae_id(pr_name, AE_ID, NAME_OF_OBJECT_OCCUPING_AE_ID)
