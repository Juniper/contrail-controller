#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import BgpRouter
from vnc_api.vnc_api import BgpRouterParams
from vnc_api.vnc_api import ControlNodeZone
from vnc_api.vnc_api import GlobalSystemConfig

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestBgpRouter(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestBgpRouter, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestBgpRouter, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_bgp_router_create_with_valid_2_byte_asn(self):
        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)
        self.api.bgp_router_delete(id=bgp_router_uuid)

    def test_bgp_router_create_with_valid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=700000,
                                            local_autonomous_system=700001)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)

        # Now delete the bgp router object and disable 4 byte ASN flag
        self.api.bgp_router_delete(id=bgp_router_uuid)
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgp_router_create_with_invalid_2_byte_asn(self):
        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=70000,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        self.assertRaises(BadRequest, self.api.bgp_router_create,
                          bgp_router_obj)

    def test_bgp_router_create_with_invalid_2_byte_local_asn(self):
        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=700000)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        self.assertRaises(BadRequest, self.api.bgp_router_create,
                          bgp_router_obj)

    def test_bgp_router_create_with_invalid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=0x1FFFFFFFF,
                                            local_autonomous_system=700000)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        self.assertRaises(BadRequest, self.api.bgp_router_create,
                          bgp_router_obj)

        # Now disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgp_router_create_with_invalid_4_byte_local_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(
            router_type='control-node',
            autonomous_system=700000,
            local_autonomous_system=0x1FFFFFFFF)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        self.assertRaises(BadRequest, self.api.bgp_router_create,
                          bgp_router_obj)

        # Now disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgp_router_update_with_valid_2_byte_asn(self):
        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)

        # Read back the bgp_router object
        bgp_router_obj = self.api.bgp_router_read(id=bgp_router_uuid)

        # update asn numbers to valid 2 byte value
        new_bgp_router_params = BgpRouterParams(
            router_type='control-node',
            autonomous_system=1234,
            local_autonomous_system=4321)
        bgp_router_obj.set_bgp_router_parameters(new_bgp_router_params)
        self.api.bgp_router_update(bgp_router_obj)

        # Delete the bgp router
        self.api.bgp_router_delete(id=bgp_router_uuid)

    def test_bgp_router_update_with_valid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)

        # Read back the bgp_router object
        bgp_router_obj = self.api.bgp_router_read(id=bgp_router_uuid)

        # update asn numbers to valid 4 byte value
        new_bgp_router_params = BgpRouterParams(
            router_type='control-node',
            autonomous_system=0xFFFFF,
            local_autonomous_system=0xFFFFF)
        bgp_router_obj.set_bgp_router_parameters(new_bgp_router_params)
        self.api.bgp_router_update(bgp_router_obj)

        # Now delete the bgp router object and disable 4 byte ASN flag
        self.api.bgp_router_delete(id=bgp_router_uuid)
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgp_router_update_with_invalid_2_byte_asn(self):
        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)

        # Read back the bgp_router object
        bgp_router_obj = self.api.bgp_router_read(id=bgp_router_uuid)

        # update asn numbers to invalid 2 byte value
        new_bgp_router_params = BgpRouterParams(
            router_type='control-node',
            autonomous_system=0x1FFFF,
            local_autonomous_system=0x1FFFF)
        bgp_router_obj.set_bgp_router_parameters(new_bgp_router_params)

        self.assertRaises(BadRequest, self.api.bgp_router_update,
                          bgp_router_obj)

        # Delete the bgp router
        self.api.bgp_router_delete(id=bgp_router_uuid)

    def test_bgp_router_update_with_invalid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node',
                                            autonomous_system=64512,
                                            local_autonomous_system=64500)
        bgp_router_obj = BgpRouter(name='bgprouter1', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)

        # Read back the bgp_router object
        bgp_router_obj = self.api.bgp_router_read(id=bgp_router_uuid)

        # update asn numbers to invalid 4 byte value
        new_bgp_router_params = BgpRouterParams(
            router_type='control-node',
            autonomous_system=0x1FFFFFFFFF,
            local_autonomous_system=0x1FFFFFFFFF)
        bgp_router_obj.set_bgp_router_parameters(new_bgp_router_params)

        self.assertRaises(BadRequest, self.api.bgp_router_update,
                          bgp_router_obj)

        # Now delete the bgp router object and disable 4 byte ASN flag
        self.api.bgp_router_delete(id=bgp_router_uuid)
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_control_node_zone(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        cnz = []
        for i in range(2):
            cnz_name = "cnz-" + str(i)
            cnz.append(ControlNodeZone(name=cnz_name, parent_obj=gsc))
            self.api.control_node_zone_create(cnz[i])

        rt_inst_obj = self.api.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        bgp_router_params = BgpRouterParams(router_type='control-node')

        # create bgp_rotuer with two control-node-zones
        bgp_router_obj = BgpRouter(name='bgprouter', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_obj.add_control_node_zone(cnz[0])
        bgp_router_obj.add_control_node_zone(cnz[1])
        try:
            self.api.bgp_router_create(bgp_router_obj)
        except BadRequest:
            pass

        # update bgp_router with two control-node-zones
        bgp_router_obj = BgpRouter(name='bgprouter', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)
        bgp_router_obj.add_control_node_zone(cnz[0])
        bgp_router_obj.add_control_node_zone(cnz[1])
        try:
            self.api.bgp_router_update(bgp_router_obj)
        except BadRequest:
            self.api.bgp_router_delete(id=bgp_router_uuid)

        # update bgp_router with same control-node-zone
        bgp_router_obj = BgpRouter(name='bgprouter', parent_obj=rt_inst_obj,
                                   bgp_router_parameters=bgp_router_params)
        bgp_router_obj.add_control_node_zone(cnz[0])
        bgp_router_uuid = self.api.bgp_router_create(bgp_router_obj)
        bgp_router_obj = self.api.bgp_router_read(id=bgp_router_uuid)
        bgp_router_obj.add_control_node_zone(cnz[0])
        self.api.bgp_router_update(bgp_router_obj)

        # update bgp_router with new control-node-zone
        bgp_router_obj.add_control_node_zone(cnz[1])
        try:
            self.api.bgp_router_update(bgp_router_obj)
        except BadRequest:
            self.api.bgp_router_delete(id=bgp_router_uuid)

        for i in range(2):
            self.api.control_node_zone_delete(fq_name=cnz[i].fq_name)
