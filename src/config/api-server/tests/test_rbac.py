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

import cgitb
cgitb.enable(format='text')

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

   def api_acl_name(self):
       rg_name = list(self.project_obj.get_fq_name())
       rg_name.append('default-api-access-list')
       return rg_name

   def check_perms(self, obj_uuid):
       query = 'token=%s&uuid=%s' % (self.vnc_lib.get_token(), obj_uuid)
       rv = self.vnc_lib._request_server(rest.OP_GET, "/obj-perms", data=query)
       rv = json.loads(rv)
       return rv['permissions']

# display resource id-perms
def print_perms(obj_perms):
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.share]
    return '%s/%d %d %s' \
        % (obj_perms.owner, obj_perms.owner_access,
           obj_perms.global_access, share_perms)
# end print_perms

# set id perms for object
def set_perms(obj, owner=None, owner_access=None, share=None, global_access=None):
    try:
        perms = obj.get_perms2()
    except AttributeError:
        logger.info( 'Unable to set perms2 in object %s' % obj.get_fq_name())
        sys.exit()
    logger.info( 'Current perms %s = %s' % (obj.get_fq_name(), print_perms(perms)))

    if owner:
        perms.owner = owner

    if owner_access:
        perms.owner_access = owner_access

    if share is not None:
        perms.share = [ShareType(obj_uuid, obj_crud) for (obj_uuid, obj_crud) in share]

    if global_access is not None:
        perms.global_access = global_access

    obj.set_perms2(perms)
    logger.info( 'New perms %s = %s' % (obj.get_fq_name(), print_perms(perms)))
# end set_perms

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, res_type, name = None, obj_uuid = None):
    if name is None and obj_uuid is None:
        logger.info('Need FQN or UUID to read object')
        return None
    method_name = res_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        if obj_uuid:
            if '-' not in obj_uuid:
                obj_uuid = str(uuid.UUID(obj_uuid))
            return method(id=obj_uuid)
        else:
            return method(fq_name=name)
    except NoIdError:
        logger.info( '%s %s not found!' % (res_type, name if name else obj_uuid))
        return None
    except PermissionDenied:
        logger.info( 'Permission denied reading %s %s' % (res_type, name))
        raise
# end

def show_rbac_rules(api_access_list_entries):
    if api_access_list_entries is None:
        logger.info( 'Empty RBAC group!')
        return

    # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
    rule_list = api_access_list_entries.get_rbac_rule()
    logger.info( 'Rules (%d):' % len(rule_list))
    logger.info( '----------')
    idx = 1
    for rule in rule_list:
            o = rule.rule_object
            f = rule.rule_field
            ps = ''
            for p in rule.rule_perms:
                ps += p.role_name + ':' + p.role_crud + ','
            o_f = "%s.%s" % (o,f) if f else o
            logger.info( '%2d %-32s   %s' % (idx, o_f, ps))
            idx += 1
    logger.info( '')

def build_rule(rule_str):
    r = rule_str.split(" ") if rule_str else []
    if len(r) < 2:
        return None

    # [0] is object.field, [1] is list of perms
    obj_field = r[0].split(".")
    perms = r[1].split(",")

    o = obj_field[0]
    f = obj_field[1] if len(obj_field) > 1 else None
    o_f = "%s.%s" % (o,f) if f else o

    # perms eg ['foo:CRU', 'bar:CR']
    rule_perms = []
    for perm in perms:
        p = perm.split(":")
        rule_perms.append(RbacPermType(role_name = p[0], role_crud = p[1]))

    # build rule
    rule = RbacRuleType(
              rule_object = o,
              rule_field = f,
              rule_perms = rule_perms)
    return rule
#end

def match_rule(rule_list, rule_str):
    extend_rule_list = True
    nr = build_rule(rule_str)
    for r in rule_list:
        if r.rule_object != nr.rule_object or r.rule_field != nr.rule_field:
            continue

        # object and field match - fix rule in place
        extend_rule_list = False

        for np in nr.rule_perms:
            extend_perms = True
            for op in r.rule_perms:
                if op.role_name == np.role_name:
                    # role found - merge incoming and existing crud in place
                    x = set(list(op.role_crud)) | set(list(np.role_crud))
                    op.role_crud = ''.join(x)
                    extend_perms = False
            if extend_perms:
                r.rule_perms.append(RbacPermType(role_name = np.role_name, role_crud = np.role_crud))

    if extend_rule_list:
        rule_list.append(nr)

# end match_rule

def vnc_fix_api_access_list(vnc_lib, pobj, rule_str = None):
    rg_name = list(pobj.get_fq_name())
    rg_name.append('default-api-access-list')

    rg = vnc_read_obj(vnc_lib, 'api-access-list', name = rg_name)

    create = False
    rule_list = []
    if rg == None:
        rg = ApiAccessList(
                 name = 'default-api-access-list',
                 parent_obj = pobj,
                 api_access_list_entries = None)
        create = True
    elif rule_str:
        api_access_list_entries = rg.get_api_access_list_entries()
        rule_list = api_access_list_entries.get_rbac_rule()

    if rule_str:
        rule = match_rule(rule_list, rule_str)

    rentry = RbacRuleEntriesType(rule_list)
    rg.set_api_access_list_entries(rentry)
    if create:
        logger.info( 'API access list empty. Creating with default rule')
        vnc_lib.api_access_list_create(rg)
    else:
        vnc_lib.api_access_list_update(rg)
    show_rbac_rules(rg.get_api_access_list_entries())

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
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertNotEquals(rv["aaa-mode"], "rbac")
        self.assertEquals(rv["aaa-mode"], "no-auth")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
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
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertNotEquals(rv["aaa-mode"], "rbac")
        self.assertEquals(rv["aaa-mode"], "admin-only")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacMtEnabled, self).tearDown()
    # end tearDown

# aaa-mode is ignored if multi-tenancy is configured
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
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertEquals(rv["aaa-mode"], "rbac")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeRbac, self).tearDown()
    # end tearDown

class TestRbacAaaModeAdminOnly(test_case.ApiServerTestCase):

    @classmethod
    def setUpClass(cls):
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'admin-only'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
        ]
        super(TestRbacAaaModeAdminOnly, cls).setUpClass(extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestRbacAaaModeAdminOnly, self).setUp()
        self._vnc_lib = VncApi('u', 'p', api_server_host=self._api_server_ip,
                               api_server_port=self._api_server_port)

    def test_rbac_config(self):
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertEquals(rv["aaa-mode"], "admin-only")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeAdminOnly, self).tearDown()
    # end tearDown

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
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertEquals(rv["aaa-mode"], "no-auth")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
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
        rv_json = self._vnc_lib._request(rest.OP_GET, '/aaa-mode')
        rv = json.loads(rv_json)
        self.assertEquals(rv["aaa-mode"], "admin-only")

        rv_json = self._vnc_lib._request(rest.OP_GET, '/multi-tenancy')
        rv = json.loads(rv_json)
        self.assertEquals(rv["enabled"], True)

    def tearDown(self):
        super(TestRbacAaaModeInvalid, self).tearDown()
    # end tearDown
