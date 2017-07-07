#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging
from testtools import ExpectedException

from cfgm_common import exceptions
from vnc_api.vnc_api import FirewallGroup
from vnc_api.vnc_api import FirewallPolicy
from vnc_api.vnc_api import FirewallPolicyDirectionType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface

import test_case

logger = logging.getLogger(__name__)


class TestFWBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFWBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFWBase, cls).tearDownClass(*args, **kwargs)

    def _create_vn_collection(self, count, proj_obj=None):
        return self._create_test_objects(count=count, proj_obj=proj_obj)

    def _create_vmi_collection(self, count, vn):
        proj = self._vnc_lib.project_read(id=vn.parent_uuid)
        vmis = []
        for i in range(count):
            vmi = VirtualMachineInterface('vmi-%s-%d' % (self.id(), i),
                                          parent_obj=proj)
            vmi.add_virtual_network(vn)
            self._vnc_lib.virtual_machine_interface_create(vmi)
            vmis.append(vmi)
        return vmis

    def _create_fg_collection(self, count, proj=None):
        fgs = []
        for i in range(count):
            fg = FirewallGroup('fg-%s-%d' % (self.id(), i), parent_obj=proj)
            self._vnc_lib.firewall_group_create(fg)
            fgs.append(fg)
        return fgs

    def _create_fp_collection(self, count, proj=None):
        fps = []
        for i in range(count):
            fp = FirewallPolicy('fp-%s-%d' % (self.id(), i), parent_obj=proj)
            self._vnc_lib.firewall_policy_create(fp)
            fps.append(fp)
        return fps


class TestFirewallGroup(TestFWBase):
    def test_create_vmi_with_two_fg_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = VirtualMachineInterface('vmi-%s' % self.id(), parent_obj=proj)
        vmi.add_virtual_network(vn)
        for fg in fgs:
            vmi.add_firewall_group(fg)

        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_create(vmi)

    def test_update_vmi_with_two_fg_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = self._create_vmi_collection(1, vn)[0]

        for fg in fgs:
            vmi.add_firewall_group(fg)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def test_update_vmi_and_exceeded_firewall_group_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = self._create_vmi_collection(1, vn)[0]

        # Associate the first fwg to vmi success
        vmi.add_firewall_group(fgs[0])
        self._vnc_lib.virtual_machine_interface_update(vmi)

        # Associate the fwg to the same vmi do nothing and success
        vmi.add_firewall_group(fgs[0])
        self._vnc_lib.virtual_machine_interface_update(vmi)

        # Add a second one fwg to a vmi fails
        vmi.add_firewall_group(fgs[1])
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def test_fp_association_both_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fg = self._create_fg_collection(1, proj)[0]
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=proj)

        # create
        fp.add_firewall_group(fg,
                              FirewallPolicyDirectionType(direction='both'))
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.firewall_policy_create(fp)

        # update
        fp = self._create_fp_collection(1, proj)[0]
        fp.add_firewall_group(fg,
                              FirewallPolicyDirectionType(direction='both'))
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.firewall_policy_update(fp)

    def test_create_fp_association_ingress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fp1 = self._create_fp_collection(1, proj)[0]
        for fg in fgs:
            fp1.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            self._vnc_lib.firewall_policy_update(fp1)

        for fg in fgs:
            fp2 = FirewallPolicy('fp-%s-1' % self.id(), parent_obj=proj)
            fp2.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_create(fp2)

    def test_update_fp_association_ingress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fps = self._create_fp_collection(2, proj)
        for fg in fgs:
            fps[0].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
        self._vnc_lib.firewall_policy_update(fps[0])

        for fg in fgs:
            fps[1].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_update(fps[1])

    def test_create_fp_association_egress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fp1 = self._create_fp_collection(1, proj)[0]
        for fg in fgs:
            fp1.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            self._vnc_lib.firewall_policy_update(fp1)

        for fg in fgs:
            fp2 = FirewallPolicy('fp-%s-1' % self.id(), parent_obj=proj)
            fp2.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_create(fp2)

    def test_update_fp_association_egress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fps = self._create_fp_collection(2, proj)
        for fg in fgs:
            fps[0].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
        self._vnc_lib.firewall_policy_update(fps[0])

        for fg in fgs:
            fps[1].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_update(fps[1])
