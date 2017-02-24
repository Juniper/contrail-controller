#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import uuid
import logging
import coverage

import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import json

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

# create users specified as array of tuples (name, password, role)
# assumes admin user and tenant exists

def normalize_uuid(id):
    return id.replace('-','')

class User(object):
   def __init__(self, apis_ip, apis_port, kc, name, password, role, project):
       self.name = name
       self.password = password
       self.role = role
       self.project = project
       self.project_uuid = None
       self.project_obj = None

       # create user/role/tenant in keystone as needed
       kc_users = set([user.name for user in kc.users.list()])
       kc_roles = set([user.name for user in kc.roles.list()])
       kc_tenants = set([tenant.name for tenant in kc.tenants.list()])

       if self.role not in kc_roles:
           logger.info('role %s missing from keystone ... creating' % self.role)
           kc.roles.create(self.role)

       if self.project not in kc_tenants:
           logger.info( 'tenant %s missing from keystone ... creating' % self.project)
           kc.tenants.create(self.project)

       for tenant in kc.tenants.list():
           if tenant.name == self.project:
                break
       self.project_uuid = tenant.id
       self.tenant = tenant

       if self.name not in kc_users:
           logger.info( 'user %s missing from keystone ... creating' % self.name)
           kc.users.create(self.name, self.password, '', tenant_id=tenant.id)

       role_dict = {role.name:role for role in kc.roles.list()}
       user_dict = {user.name:user for user in kc.users.list()}
       self.user = user_dict[self.name]

       # update tenant ID (needed if user entry already existed in keystone)
       self.user.tenant_id = tenant.id

       logger.info( 'Adding user %s with role %s to tenant %s' \
            % (name, role, project))
       try:
           kc.roles.add_user_role(user_dict[self.name], role_dict[self.role], tenant)
       except kc_exceptions.Conflict:
           pass

       self.vnc_lib = MyVncApi(username = self.name, password = self.password,
            tenant_name = self.project,
            api_server_host = apis_ip, api_server_port = apis_port)
   # end __init__

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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'multi_tenancy', False),
        ]
        super(TestRbacMtDisabled, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacMtDisabled, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacMtDisabled, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'multi_tenancy', True),
        ]
        super(TestRbacMtEnabled, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacMtEnabled, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacMtEnabled, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeRbac, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacAaaModeRbac, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacAaaModeRbac, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'cloud-admin'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeAdminOnly, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacAaaModeAdminOnly, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacAaaModeAdminOnly, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeNoAuth, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacAaaModeNoAuth, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacAaaModeNoAuth, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
        self.assertEquals(rv["aaa-mode"], "no-auth")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], False)

    def tearDown(self):
        super(TestRbacAaaModeNoAuth, self).tearDown()
    # end tearDown

class TestRbacAaaModeInvalid(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'invalid-value'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeInvalid, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbacAaaModeInvalid, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbacAaaModeInvalid, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv = self._vnc_lib.get_aaa_mode()
        self.assertEquals(rv["aaa-mode"], "cloud-admin")

        rv = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeInvalid, self).tearDown()
    # end tearDown

class TestAuthModeInvalid(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'auth', 'no-auth'),
        ]
        super(TestAuthModeInvalid, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestAuthModeInvalid, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestAuthModeInvalid, self).setUp()

    def test_aaa_mode(self):
        rv = self._api_server.is_auth_disabled()
        self.assertEquals(rv, True)

    def tearDown(self):
        super(TestAuthModeInvalid, self).tearDown()
    # end tearDown

class TestRbac(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
        ]
        super(TestRbac, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRbac, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestRbac, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_aaa_mode(self):
        self.assertRaises(HttpError, self._vnc_lib.set_aaa_mode, "invalid-aaa-mode")

    def tearDown(self):
        super(TestRbac, self).tearDown()
    # end tearDown
