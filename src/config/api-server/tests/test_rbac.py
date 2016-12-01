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
import keystoneclient.exceptions as kc_exceptions
import keystoneclient.v2_0.client as keystone
from keystonemiddleware import auth_token
from cfgm_common import rest, utils
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

PERMS_NONE = 0
PERMS_X = 1
PERMS_W = 2
PERMS_R = 4
PERMS_WX = 3
PERMS_RX = 5
PERMS_RW = 6
PERMS_RWX = 7

from test_perms2 import vnc_read_obj, vnc_aal_del_rule

# create users specified as array of tuples (name, password, role)
# assumes admin user and tenant exists

def normalize_uuid(id):
    return id.replace('-','')

def token_from_user_info(user_name, tenant_name, domain_name, role_name,
        tenant_id = None):
    token_dict = {
        'X-User': user_name,
        'X-User-Name': user_name,
        'X-Project-Name': tenant_name,
        'X-Project-Id': tenant_id or '',
        'X-Domain-Name' : domain_name,
        'X-Role': role_name,
    }
    rval = json.dumps(token_dict)
    # logger.info( 'Generate token %s' % rval)
    return rval

class MyVncApi(VncApi):
    def __init__(self, username = None, password = None,
        tenant_name = None, api_server_host = None, api_server_port = None):
        self._username = username
        self._tenant_name = tenant_name
        self.auth_token = None
        self._kc = keystone.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')
        VncApi.__init__(self, username = username, password = password,
            tenant_name = tenant_name, api_server_host = api_server_host,
            api_server_port = api_server_port)

    def _authenticate(self, response=None, headers=None):
        role_name = self._kc.user_role(self._username, self._tenant_name)
        uobj = self._kc.users.get(self._username)
        rval = token_from_user_info(self._username, self._tenant_name,
            'default-domain', role_name, uobj.tenant_id)
        new_headers = headers or {}
        new_headers['X-AUTH-TOKEN'] = rval
        self.auth_token = rval
        return new_headers

    def get_token(self):
        return self.auth_token

# This is needed for VncApi._authenticate invocation from within Api server.
# We don't have access to user information so we hard code admin credentials.
def ks_admin_authenticate(self, response=None, headers=None):
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'cloud-admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

# aaa-mode is ignored if multi-tenancy is configured
class TestRbacMtDisabled(test_case.ApiServerTestCase):
    domain_name = 'default-domain'
    fqdn = [domain_name]
    vn_name='alice-vn'

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'multi_tenancy', False),
        ]
        super(TestRbacMtDisabled, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacMtDisabled, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertNotEquals(rv["aaa-mode"], "rbac")
        self.assertEquals(rv["aaa-mode"], "no-auth")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], False)

    def tearDown(self):
        super(TestRbacMtDisabled, self).tearDown()
    # end tearDown

# aaa-mode is ignored if multi-tenancy is configured
class TestRbacMtEnabled(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'multi_tenancy', True),
        ]
        super(TestRbacMtEnabled, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacMtEnabled, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertNotEquals(rv["aaa-mode"], "rbac")
        self.assertEquals(rv["aaa-mode"], "cloud-admin")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacMtEnabled, self).tearDown()
    # end tearDown

# aaa-mode is not ignored if multi-tenancy is not configured
class TestRbacAaaModeRbac(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeRbac, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacAaaModeRbac, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertEquals(rv["aaa-mode"], "rbac")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeRbac, self).tearDown()
    # end tearDown

# aaa-mode is not ignored if multi-tenancy is not configured
class TestRbacAaaModeAdminOnly(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'cloud-admin'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeAdminOnly, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacAaaModeAdminOnly, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertEquals(rv["aaa-mode"], "cloud-admin")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeAdminOnly, self).tearDown()
    # end tearDown

# aaa-mode is not ignored if multi-tenancy is not configured
class TestRbacAaaModeNoAuth(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeNoAuth, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacAaaModeNoAuth, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertEquals(rv["aaa-mode"], "no-auth")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], False)

    def tearDown(self):
        super(TestRbacAaaModeNoAuth, self).tearDown()
    # end tearDown

class TestRbacAaaModeInvalid(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'invalid-value'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeInvalid, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacAaaModeInvalid, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        self.assertEquals(rv["aaa-mode"], "cloud-admin")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeInvalid, self).tearDown()
    # end tearDown

class TestRbac(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
        ]
        super(TestRbac, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbac, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_aaa_mode(self):
        self.assertRaises(HttpError, self._vnc_lib.set_aaa_mode, "invalid-aaa-mode")

    def test_bug_1642464(self):
        global_rg = vnc_read_obj(self._vnc_lib, 'api-access-list',
            name = ['default-global-system-config', 'default-api-access-list'])
        num_default_rules = len(global_rg.api_access_list_entries.rbac_rule)

        vnc_aal_del_rule(self._vnc_lib, global_rg, "documentation *:R")
        global_rg = vnc_read_obj(self._vnc_lib, 'api-access-list',
            name = ['default-global-system-config', 'default-api-access-list'])
        self.assertEquals(len(global_rg.api_access_list_entries.rbac_rule), num_default_rules-1)

        self._api_server._create_default_rbac_rule()
        global_rg = vnc_read_obj(self._vnc_lib, 'api-access-list',
            name = ['default-global-system-config', 'default-api-access-list'])
        self.assertEquals(len(global_rg.api_access_list_entries.rbac_rule), num_default_rules)

    def tearDown(self):
        super(TestRbac, self).tearDown()
    # end tearDown
