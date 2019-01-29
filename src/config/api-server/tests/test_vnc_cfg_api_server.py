#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import AllowedAddressPair
from vnc_api.vnc_api import AllowedAddressPairs
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualNetwork

import test_case


logger = logging.getLogger(__name__)


class TestVncCfgApiServerBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncCfgApiServerBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncCfgApiServerBase, cls).tearDownClass(
            *args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestVncCfgApiServer(TestVncCfgApiServerBase):
    def test_subnet_type_validation(self):
        test_suite = [
            (SubnetType('0', 0), ('0.0.0.0', 0)),
            (SubnetType('1.1.1.1'), BadRequest),
            (SubnetType('1.1.1.1', 24), ('1.1.1.0', 24)),
            (SubnetType('1.1.1.1', '24'), ('1.1.1.0', 24)),
            (SubnetType('1.1.1.1', 32), ('1.1.1.1', 32)),
            (SubnetType('1.1.1.e', 32), BadRequest),
            (SubnetType('1.1.1.1', '32e'), BadRequest),
            (SubnetType('1.1.1.0,2.2.2.0', 24), BadRequest),
            (SubnetType(''), BadRequest),
            (SubnetType('', 30), BadRequest),
            (SubnetType('::', 0), ('::', 0)),
            (SubnetType('::'), BadRequest),
            (SubnetType('dead::beef', 128), ('dead::beef', 128)),
            (SubnetType('dead::beef', '128'), ('dead::beef', 128)),
            (SubnetType('dead::beef', 96), ('dead::', 96)),
            (SubnetType('dead::beez', 96), BadRequest),
            (SubnetType('dead::beef', '96e'), BadRequest),
            (SubnetType('dead::,beef::', 64), BadRequest),
        ]

        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        vmi = VirtualMachineInterface('vmi-%s' % self.id(), parent_obj=project)
        vmi.set_virtual_network(vn)
        self.api.virtual_machine_interface_create(vmi)

        for subnet, expected_result in test_suite:
            aaps = AllowedAddressPairs(
                allowed_address_pair=[AllowedAddressPair(ip=subnet)])
            vmi.set_virtual_machine_interface_allowed_address_pairs(aaps)
            if (type(expected_result) == type and
                    issubclass(expected_result, Exception)):
                self.assertRaises(
                    expected_result,
                    self.api.virtual_machine_interface_update,
                    vmi)
            else:
                self.api.virtual_machine_interface_update(vmi)
                vmi = self.api.virtual_machine_interface_read(id=vmi.uuid)
                returned_apps = vmi.\
                    get_virtual_machine_interface_allowed_address_pairs().\
                    get_allowed_address_pair()
                self.assertEqual(len(returned_apps), 1)
                returned_subnet = returned_apps[0].get_ip()
                self.assertEqual(returned_subnet.ip_prefix,
                                 expected_result[0])
                self.assertEqual(returned_subnet.ip_prefix_len,
                                 expected_result[1])
