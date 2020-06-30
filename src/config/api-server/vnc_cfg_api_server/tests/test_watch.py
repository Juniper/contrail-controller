from __future__ import absolute_import
#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import logging
import requests
import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import bottle
from flexmock import flexmock
import sseclient
from vnc_cfg_api_server.event_dispatcher import EventDispatcher
from . import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


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
    # end setUp

    def test_valid_params(self):
        param = {"resource_type": "virtual_network,virtual_machine_interface"}

        init_sample = {
            "event": "init", "data": [{"type": "virtual_network"}], }
        self.mock.should_receive('register_client').and_return().once()
        self.mock.should_receive('initialize').and_return(init_sample).twice()

        stream_response = requests.get(self.url, params=param, stream=True)
        self.assertThat(stream_response.status_code, Equals(200))

        client = sseclient.SSEClient(stream_response)
        count = 0
        for event in client.events():
            event.event = event.event.encode('utf-8', 'ignore')
            event.data = event.data.encode('utf-8', 'ignore')
            if(event.event == 'init'):
                count += 1
                self.assertThat(event.data, Equals(
                    "[{'type': 'virtual_network'}]"))
        self.assertThat(count, Equals(2))
    # end test_with_resource

    def test_invalid_request(self):
        param = {"resource_type": "virtual_network,virtual_machine_interface"}
        url = 'http://%s:%s/watch' % (self.listen_ip, self.listen_port)
        response = requests.get(self.url, stream=True)
        self.assertThat(response.content, Equals(
            'resource_type required in request'))
        self.assertEqual(response.status_code, 400)
    # end test_invalid_request


if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
