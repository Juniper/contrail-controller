from __future__ import absolute_import
#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import gevent
import logging
import requests
from testtools.matchers import Equals
import unittest
from flexmock import flexmock
import sseclient
from . import test_case

from cfgm_common import vnc_cgitb
from vnc_cfg_api_server.event_dispatcher import EventDispatcher

vnc_cgitb.enable(format='text')

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class CustomError(Exception):
    pass


class TestWatch(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestWatch, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestWatch, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestWatch, self).setUp()
        self.listen_ip = self._api_server_ip
        self.listen_port = self._api_server._args.listen_port
        self.url = 'http://%s:%s/watch' % (self.listen_ip, self.listen_port)
        self.mock = flexmock(EventDispatcher)
        self.stream_response = None
    # end setUp

    def test_subscribe_exception(self):
        param = {"resource_type": "virtual_network"}
        self.error = "value error occured"
        self.mock.should_receive('subscribe_client').and_raise(
            CustomError, self.error)

        response = requests.get(self.url, params=param, stream=True)
        self.response_error = "Client queue registration failed with exception %s" % (
            self.error)
        self.assertThat(response.status_code, Equals(500))
        self.assertThat(
            response.content.decode('utf-8'),
            Equals(
                self.response_error))
    # end test_subscribe_exception

    def test_valid_params(self):
        param = {"resource_type": "virtual_network,virtual_machine_interface"}
        self.error = "value error occured"
        init_sample = {
            "event": "init", "data": [{"type": "virtual_network"}], }
        self.mock.should_receive('subscribe_client').and_return().once()
        self.mock.should_receive('initialize').and_return(
            True, init_sample).twice()

        self.count = 0
        self.data = "[{'type': 'virtual_network'}]"

        def watch_client():
            self.stream_response = requests.get(
                self.url, params=param, stream=True)
            client = sseclient.SSEClient(self.stream_response)
            for event in client.events():
                if (event.event == 'init'):
                    self.count += 1
                    self.assertThat(event.data, Equals(self.data))

        gevent.spawn(watch_client)
        gevent.sleep(0.1)
        self.assertThat(self.stream_response.status_code, Equals(200))
        self.assertEqual(self.count, 2)
    # end test_valid_params

    def test_invalid_request(self):
        response = requests.get(self.url, stream=True)
        self.assertEqual(response.status_code, 400)
        self.assertThat(response.content.decode('utf-8'), Equals(
            'resource_type required in request'))
    # end test_invalid_request
# end TestWatch


class TestWatchIntegration(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestWatchIntegration, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestWatchIntegration, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestWatchIntegration, self).setUp()
        self.listen_ip = self._api_server_ip
        self.listen_port = self._api_server._args.listen_port
        self.url = 'http://%s:%s/watch' % (self.listen_ip, self.listen_port)
        self.stream_response = None
    # end setUp

    def test_watch(self):
        param = {
            "resource_type":
                "virtual_network,virtual_machine_interface,routing_instance"}
        self.count = 0

        def watch_client():
            self.stream_response = requests.get(
                self.url, params=param, stream=True)
            client = sseclient.SSEClient(self.stream_response)
            for event in client.events():
                logger.info('%s: %s' % (event.event, event.data))
                self.count += 1

        gevent.spawn(watch_client)
        num_vn = 1
        vn_objs, ri_objs, vmi_objs, x = self._create_vn_ri_vmi(num_vn)
        gevent.sleep(0.1)
        self.assertThat(self.stream_response.status_code, Equals(200))
        self.assertEqual(self.count, 8)
    # end test_watch
# end TestWatchIntegration


if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()