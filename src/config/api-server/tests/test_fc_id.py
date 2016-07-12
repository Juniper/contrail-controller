#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import socket
import errno
import uuid
import logging
import coverage

import cgitb
cgitb.enable(format='text')

import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
import inspect
import pycassa
import kombu
import requests

from vnc_api.vnc_api import *
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

class TestForwardingClassId(test_case.ApiServerTestCase):
    def __init__(self, *args, **kwargs):
        ch = logging.StreamHandler()
        ch.setLevel(logging.DEBUG)
        logger.addHandler(ch)
        super(TestForwardingClassId, self).__init__(*args, **kwargs)

    def test_requested_fc_id(self):
        fc1 = ForwardingClass(name = "fc1",
                              forwarding_class_id = 1)
        fc1_id = self._vnc_lib.forwarding_class_create(fc1)
        logger.debug('Created Forwarding class with ID 1')

        test_fc = ForwardingClass(name = "test_fc",
                                  forwarding_class_id = 1)
        try:
            self._vnc_lib.forwarding_class_create(test_fc)
            self.assertTrue(False, 'Forwarding class updated'
                            'with duplicate ID... Test failed!!')
            logger.debug(' Test failed! got id 1')
        except:
            logger.debug(' Test passed for FC id 1')


        fc2 = ForwardingClass(name = "fc2",
                              forwarding_class_id = 2)
        fc2_id = self._vnc_lib.forwarding_class_create(fc2)
        logger.debug('Created Forwarding class with ID 2')

        fc1.forwarding_class_id = 2
        try:
            self._vnc_lib.forwarding_class_update(fc1);
            self.assertTrue(False, 'Forwarding class updated'
                            'with duplicate ID... Test failed!!')
        except:
            logger.debug(' Test passed for FC id 2')

        gevent.sleep(0.1)
        #cleanup
        logger.debug('Cleaning up FC test case')
        self._vnc_lib.forwarding_class_delete(id=fc1_id)
        self._vnc_lib.forwarding_class_delete(id=fc2_id)
        logger.debug('Cleaned up FC test case')
    #end

#end class TestForwardingClass

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)
    unittest.main()
