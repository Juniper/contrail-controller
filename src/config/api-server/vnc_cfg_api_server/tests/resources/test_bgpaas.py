#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import BGPaaSControlNodeZoneAttributes
from vnc_api.vnc_api import BgpAsAService
from vnc_api.vnc_api import ControlNodeZone
from vnc_api.vnc_api import GlobalSystemConfig

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestBgpaas(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestBgpaas, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestBgpaas, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_control_node_zone(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        cnz = []
        for i in range(2):
            cnz_name = "cnz-" + str(i)
            cnz.append(ControlNodeZone(name=cnz_name, parent_obj=gsc))
            self.api.control_node_zone_create(cnz[i])

        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        primary_attr = BGPaaSControlNodeZoneAttributes("primary")
        secondary_attr = BGPaaSControlNodeZoneAttributes("secondary")

        # create bgpaas with two control-node-zones as "primary"
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_create(bgpaas_obj)
        except BadRequest:
            pass

        # create bgpaas with two control-node-zones as "secondary"
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_create(bgpaas_obj)
        except BadRequest:
            pass

        # update bgpaas with two control-node-zones as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with two control-node-zones as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with new control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with new control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        for i in range(2):
            self.api.control_node_zone_delete(fq_name=cnz[i].fq_name)
