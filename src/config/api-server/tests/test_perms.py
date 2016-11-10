#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import os
import sys
import socket
import errno
import uuid
import logging
import coverage

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
from lxml import etree
import inspect
import requests
import stevedore

from vnc_api.vnc_api import *
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestPermissions(test_case.ApiServerTestCase):
    def test_example(self):
        pass
    # end test_example
# class TestPermissions
