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
import json
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

        init_sample = {
            "event": "init", "type": "virtual_network-virtual_machine_interface", "uuid": 123, "data": [{}], }
        flexmock(EventDispatcher).should_receive(
            'register_client').and_return('yes').once()
        flexmock(EventDispatcher).should_receive(
            'initialize').and_return(init_sample).twice()
        # flexmock(EventDispatcher).should_receive('initialize').and_return(init1_sample).once()
    # end SetUp

    def test_with_resource(self):
        param = {"resource_type": "virtual_network,virtual_machine_interface"}
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/watch' % (listen_ip, listen_port)
        stream_response = requests.get(url, params=param)
        self.assertThat(stream_response.status_code, Equals(200))
        # self.assertThat(stream_response.content, Equals("00"))
        #client = sseclient.SSEClient(stream_response)


    # def test_invalid_request(self):
    #    try:
    #        response = requests.get(
    #        self.url, headers={'Content-type': 'application/json; charset="UTF-8"'})
    #    except BadRequest as e:
    #        self.assertTrue(False, 'resource_type required in request')
    #    self.assertThat(response.status_code, Equals(400))

    if __name__ == '__main__':
        ch = logging.StreamHandler()
        ch.setLevel(logging.DEBUG)
        logger.addHandler(ch)

        unittest.main()
