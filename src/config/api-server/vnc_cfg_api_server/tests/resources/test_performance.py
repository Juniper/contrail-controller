#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import json
import logging
import requests

from vnc_cfg_api_server.tests import test_case

from vnc_api.vnc_api import VirtualNetwork

logger = logging.getLogger(__name__)


class TestAPIPerformance(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestAPIPerformance, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestAPIPerformance, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def _start_perf(self):
        start_url = 'http://{}:{}/start-profile'.format(
            TestAPIPerformance._api_server_ip,
            TestAPIPerformance._api_server_port)
        logger.debug('START PERFORMANCE REQUEST: "{}"'.format(start_url))
        requests.post(start_url)

    def _stop_perf_and_save_result(self):
        stop_url = 'http://{}:{}/stop-profile'.format(
            TestAPIPerformance._api_server_ip,
            TestAPIPerformance._api_server_port)
        requests.post(stop_url)
        logger.debug('STOP PERFORMANCE REQUEST: "{}"'.format(stop_url))

    def test_create_virtual_network_performance(self):
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])

        vn_obj = VirtualNetwork('vn-perftest-{}'.format(self.id()),
                                parent_obj=proj)

        self._start_perf()
        self.api.virtual_network_create(vn_obj)
        self._stop_perf_and_save_result()

        self.api.virtual_network_delete(id=vn_obj.uuid)
