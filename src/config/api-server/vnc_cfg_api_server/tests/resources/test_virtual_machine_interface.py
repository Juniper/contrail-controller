#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
import six
from vnc_api.gen.resource_xsd import MacAddressesType
from vnc_api.vnc_api import AllowedAddressPair
from vnc_api.vnc_api import AllowedAddressPairs
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import VirtualNetwork

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)
VMIPT = VirtualMachineInterfacePropertiesType


class TestVirtualMachineInterface(test_case.ApiServerTestCase):
    def setUp(self):
        super(TestVirtualMachineInterface, self).setUp()
        if six.PY3:
            self.assertItemsEqual = self.assertCountEqual

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVirtualMachineInterface, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVirtualMachineInterface, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_valid_sub_interface_vlan_tag_id(self):
        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        test_suite = [
            (None, None),
            (VMIPT(None), None),
            (VMIPT(sub_interface_vlan_tag=None), None),
            (VMIPT(sub_interface_vlan_tag=-42), BadRequest),
            (VMIPT(sub_interface_vlan_tag=4095), BadRequest),
            (VMIPT(sub_interface_vlan_tag='fo'), BadRequest),
            (VMIPT(sub_interface_vlan_tag='42'), None),
            (VMIPT(sub_interface_vlan_tag=42), None),
        ]

        for (vmipt, result) in test_suite:
            vmi = VirtualMachineInterface('%s-vmi' % self.id(),
                                          parent_obj=project)
            vmi.set_virtual_network(vn)
            vmi.set_virtual_machine_interface_properties(vmipt)
            if result and issubclass(result, Exception):
                self.assertRaises(result,
                                  self.api.virtual_machine_interface_create,
                                  vmi)
            else:
                self.api.virtual_machine_interface_create(vmi)
                self.api.virtual_machine_interface_delete(id=vmi.uuid)

    def test_cannot_update_sub_interface_vlan_tag(self):
        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        parent_vmi = VirtualMachineInterface(
            '%s-parent-vmi' % self.id(), parent_obj=project)
        parent_vmi.set_virtual_network(vn)
        self.api.virtual_machine_interface_create(parent_vmi)

        vmi = VirtualMachineInterface('%s-vmi' % self.id(), parent_obj=project)
        vmi.set_virtual_network(vn)
        vmi.set_virtual_machine_interface(parent_vmi)
        self.api.virtual_machine_interface_create(vmi)
        vmi42 = VirtualMachineInterface('%s-vmi42' % self.id(),
                                        parent_obj=project)
        vmi42.set_virtual_machine_interface_properties(
            VMIPT(sub_interface_vlan_tag=42))
        vmi42.set_virtual_network(vn)
        vmi42.set_virtual_machine_interface(parent_vmi)
        self.api.virtual_machine_interface_create(vmi42)

        # if we don't touch VMI props, we can update the VMI with or without
        # VLAN ID
        vmi.set_display_name('new vmi name')
        self.api.virtual_machine_interface_update(vmi)
        vmi42.set_display_name('new vmi42 name')
        self.api.virtual_machine_interface_update(vmi42)

        # if we change VMI props without specifying anything, we can update the
        # VMI if VLAN ID is not set or 0
        vmi.set_virtual_machine_interface_properties(None)
        self.api.virtual_machine_interface_update(vmi)
        vmi.set_virtual_machine_interface_properties(
            VMIPT(sub_interface_vlan_tag=None))
        self.api.virtual_machine_interface_update(vmi)

        # if we change VMI props without specifying anything, we cannot update
        # the VMI if VLAN ID is not 0
        vmi42.set_virtual_machine_interface_properties(None)
        self.assertRaises(BadRequest,
                          self.api.virtual_machine_interface_update,
                          vmi42)
        vmi42.set_virtual_machine_interface_properties(
            VMIPT(sub_interface_vlan_tag=None))
        self.assertRaises(BadRequest,
                          self.api.virtual_machine_interface_update,
                          vmi42)

        # if we update VMI VLAN ID to the same VLAN ID, no error raised
        vmi.set_virtual_machine_interface_properties(
            VMIPT(sub_interface_vlan_tag=0))
        self.api.virtual_machine_interface_update(vmi)
        vmi42.set_virtual_machine_interface_properties(
            VMIPT(sub_interface_vlan_tag=42))
        self.api.virtual_machine_interface_update(vmi42)

    def test_port_security_and_allowed_address_pairs(self):
        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self._vnc_lib.virtual_network_create(vn)
        addr_pair = AllowedAddressPairs(
            allowed_address_pair=[
                AllowedAddressPair(ip=SubnetType('1.1.1.0', 24),
                                   mac='02:ce:1b:d7:a6:e7')])
        msg = (r"^Allowed address pairs are not allowed when port security is "
               "disabled$")

        vmi = VirtualMachineInterface(
            'vmi-%s' % self.id(),
            parent_obj=project,
            port_security_enabled=False,
            virtual_machine_interface_allowed_address_pairs=addr_pair)
        vmi.set_virtual_network(vn)
        with self.assertRaisesRegexp(BadRequest, msg):
            self._vnc_lib.virtual_machine_interface_create(vmi)

        vmi = VirtualMachineInterface('vmi-%s' % self.id(), parent_obj=project,
                                      port_security_enabled=False)
        vmi.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # updating a port with allowed address pair should throw an exception
        # when port security enabled is set to false
        vmi.virtual_machine_interface_allowed_address_pairs = addr_pair
        with self.assertRaisesRegexp(BadRequest, msg):
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def test_disable_port_security_with_empty_allowed_address_pair_list(self):
        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self._vnc_lib.virtual_network_create(vn)
        addr_pair = AllowedAddressPairs()

        vmi1 = VirtualMachineInterface(
            'vmi1-%s' % self.id(),
            parent_obj=project,
            port_security_enabled=False,
            virtual_machine_interface_allowed_address_pairs=addr_pair)
        vmi1.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        addr_pair = AllowedAddressPairs(
            allowed_address_pair=[
                AllowedAddressPair(ip=SubnetType('1.1.1.0', 24),
                                   mac='02:ce:1b:d7:a6:e7')])
        vmi2 = VirtualMachineInterface(
            'vmi2-%s' % self.id(),
            parent_obj=project,
            port_security_enabled=True,
            virtual_machine_interface_allowed_address_pairs=addr_pair)
        vmi2.set_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi2)

        addr_pair = AllowedAddressPairs()
        vmi2.set_virtual_machine_interface_allowed_address_pairs(addr_pair)
        self._vnc_lib.virtual_machine_interface_update(vmi2)

        vmi2.set_port_security_enabled(False)
        self._vnc_lib.virtual_machine_interface_update(vmi2)

    def test_mac_address_always_allocated(self):
        project = Project(name='p-{}'.format(self.id()))
        p_uuid = self.api.project_create(project)
        project.set_uuid(p_uuid)

        vn = VirtualNetwork(name='vn-{}'.format(self.id()), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)
        vn.set_uuid(vn_uuid)

        mac_addr_test_cases = [
            ['02:ce:1b:d7:a6:e7'],
            ['02-ce-1b-d7-a6-e8'],
            ['02:ce:1b:d7:a6:e9', '02-ce-1b-d7-a6-f1', '02:ce:1b:d7:a6:f2'],
            [],
            None,
        ]

        for i, macs_test_case in enumerate(mac_addr_test_cases):
            vmi = VirtualMachineInterface(name='vmi{}-{}'.format(i, self.id()),
                                          parent_obj=project)
            vmi.set_virtual_network(vn)
            vmi.set_virtual_machine_interface_mac_addresses(
                MacAddressesType(macs_test_case))

            vmi_uuid = self.api.virtual_machine_interface_create(vmi)
            vmi = self.api.virtual_machine_interface_read(id=vmi_uuid)

            vmi_macs = vmi.get_virtual_machine_interface_mac_addresses()\
                .get_mac_address()
            if macs_test_case:
                # check if vmi_macs len is the same as input len
                self.assertItemsEqual(vmi_macs, [mac.replace('-', ':')
                                                 for mac in macs_test_case])
            else:
                # if input was empty or None, check if vmi_macs has been alloc
                self.assertEqual(len(vmi_macs), 1)

            for m in vmi_macs:
                # check if any of mac is not zero
                self.assertNotEqual(m, '00:00:00:00:00:00')
