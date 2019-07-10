#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from testtools import ExpectedException
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface
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

    # end test_virtual_port_group_crud_and_vmi_association
