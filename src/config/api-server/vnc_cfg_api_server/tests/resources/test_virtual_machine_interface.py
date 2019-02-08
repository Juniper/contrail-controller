#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import VirtualNetwork

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestVirtualMachineInterface(test_case.ApiServerTestCase):
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

        VMIPT = VirtualMachineInterfacePropertiesType
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
        vmi = VirtualMachineInterface('%s-vmi' % self.id(), parent_obj=project)
        vmi.set_virtual_network(vn)
        self.api.virtual_machine_interface_create(vmi)

        vmi.set_virtual_machine_interface_properties(None)
        self.api.virtual_machine_interface_update(vmi)

        vmi.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=None))
        self.api.virtual_machine_interface_update(vmi)

        vmi.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        self.assertRaises(BadRequest,
                          self.api.virtual_machine_interface_update,
                          vmi)
