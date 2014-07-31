#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import os
import sys
import socket
import errno
import uuid

import cgitb
cgitb.enable(format='text')

import fixtures
import testtools
import unittest
import re
import json
import copy
from lxml import etree
import inspect
from collections import OrderedDict

import pycassa
import yaml

from vnc_api.vnc_api import *
from vnc_api.common import exceptions as vnc_exceptions
import vnc_cfg_api_server
import ifmap.client as ifmap_client
import ifmap.response as ifmap_response
sys.path.append("../common/tests") 
from test_utils import *
import test_common

import gen.vnc_api_test_gen


class Singleton(type):
    _instances = {}

    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(
                *args, **kwargs)
        return cls._instances[cls]


class ConfigParams (object):
    __metaclass__ = Singleton

    def set_cfg_file(self, cfg_file):
        self.cfg_file = cfg_file
        self.load_config()

    def load_config(self):
        with open(self.cfg_file, 'r') as f:
            cfg = yaml.load(f.read())
            self.__dict__.update(cfg)

    def set_defaults(self):
        self.api_server_ip = '10.84.7.3'
        self.api_server_port = 8082
        self.http_server_port = 0


class DemoFixture(fixtures.Fixture):

    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib
    # end __init__

    def setUp(self):
        super(DemoFixture, self).setUp()
        dom_fixt = self.useFixture(
            gen.vnc_api_test_gen.DomainTestFixtureGen(self._vnc_lib))
        proj_1_fixt = self.useFixture(
            gen.vnc_api_test_gen.ProjectTestFixtureGen(self._vnc_lib,
                                                       'proj-3', dom_fixt))
        proj_2_fixt = self.useFixture(
            gen.vnc_api_test_gen.ProjectTestFixtureGen(self._vnc_lib,
                                                       'proj-4', dom_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'front-end', proj_1_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'back-end', proj_1_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'public', proj_2_fixt))
    # end setUp

    def cleanUp(self):
        super(DemoFixture, self).cleanUp()
    # end cleanUp

# end class DemoFixture


class TestDemo(testtools.TestCase, fixtures.TestWithFixtures):

    def setUp(self):
        super(TestDemo, self).setUp()

        c = ConfigParams()
        api_server_ip = c.api_server_ip
        api_server_port = c.api_server_port
        http_server_port = c.http_server_port
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        super(TestDemo, self).tearDown()
        #gevent.kill(self._api_svr, gevent.GreenletExit)
        # gevent.joinall([self._api_svr])
    # end tearDown

    def test_demo(self):
        self.useFixture(DemoFixture(self._vnc_lib))
    # end test_demo

# end class TestDemo


if __name__ == '__main__':
    '''
       run from workspace root like this:
       PYTHONPATH=build/debug/cfgm/api-server:\
           build/debug/cfgm/api-server/vnc_cfg_api_server:\
           build/debug/api-lib:build/debug/api-lib/vnc_api\
           python  src/cfgm/api-server/tests/test_crud_basic_sys.py
    '''
    c = ConfigParams()
    c.set_defaults()
    try:
        c.set_cfg_file('/home/ted/sn001.yml')
    except Exception, e:
        print 'No config, trying default', e
    unittest.main()
