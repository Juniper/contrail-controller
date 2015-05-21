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
from vnc_api.common import exceptions as vnc_exceptions
import keystoneclient.apiclient.exceptions as kc_exceptions
import keystoneclient.v2_0.client as keystone
from keystoneclient.middleware import auth_token
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
            print 'role %s missing from keystone ... creating' % self.role
            kc.roles.create(self.role)
        
        if self.project not in kc_tenants:
            print 'tenant %s missing from keystone ... creating' % self.project
            kc.tenants.create(name = self.project)
        
        for tenant in kc.tenants.list():
            if tenant.name == self.project:
                 break
        self.project_uuid = tenant.id
         
        if self.name not in kc_users:
            print 'user %s missing from keystone ... creating' % self.name
            user = kc.users.create(self.name, self.password, '', tenant_id=tenant.id)
        
        role_dict = {role.name:role for role in kc.roles.list()}
        user_dict = {user.name:user for user in kc.users.list()}
        
        print 'Adding user %s with role %s to tenant %s' \
             % (name, role, project)
        try:
            kc.roles.add_user_role(user_dict[self.name], role_dict[self.role], tenant)
        except kc_exceptions.Conflict:
            pass
        
        self.vnc_lib = MyVncApi(username = self.name, password = self.password,
            tenant_name = self.project,
            api_server_host = apis_ip, api_server_port = apis_port)

        # create object in API server
        #project_obj = Project(project)
        #project_obj.uuid = tenant.id
        #self.vnc_lib.project_create(project_obj)
    # end __init__
         
    def api_acl_name(self):
        rg_name = list(self.project_obj.get_fq_name())
        rg_name.append('default-api-access-list')
        return rg_name
# end class User

# display resource id-perms
def print_perms(obj_perms):
    share_perms = ['%s:%d' % (x.tenant, x.tenant_access) for x in obj_perms.permissions.share]
    return '%s/%d %d %s' \
        % (obj_perms.permissions.owner, obj_perms.permissions.owner_access,
           obj_perms.permissions.globally_shared, share_perms)
# end print_perms

# set id perms for object
def set_perms(obj, owner=None, owner_access=None, share=None, globally_shared=None):
    perms = obj.get_id_perms2()
    print 'Current perms %s = %s' % (obj.get_fq_name(), print_perms(perms))

    if owner:
        perms.permissions.owner = owner

    if owner_access:
        perms.permissions.owner_access = owner_access

    if share is not None:
        perms.permissions.share = [ShareType(obj_uuid, obj_crud) for (obj_uuid, obj_crud) in share]

    if globally_shared is not None:
        perms.permissions.globally_shared = globally_shared

    obj.set_id_perms2(perms)
    print 'New perms %s = %s' % (obj.get_fq_name(), print_perms(perms))
# end set_perms

# Read VNC object. Return None if object doesn't exists
def vnc_read_obj(vnc, obj_type, name = None, obj_uuid = None):
    if name is None and obj_uuid is None:
        print 'Need FQN or UUID to read object'
        return None
    method_name = obj_type.replace('-', '_')
    method = getattr(vnc, "%s_read" % (method_name))
    try:
        if obj_uuid:
            if '-' not in obj_uuid:
                obj_uuid = str(uuid.UUID(obj_uuid))
            return method(id=obj_uuid)
        else:
            return method(fq_name=name)
    except NoIdError:
        print '** %s %s not found!' % (obj_type, name if name else obj_uuid)
        return None
# end

def show_rbac_rules(api_access_list_entries):
    if api_access_list_entries is None:
        print 'Empty RBAC group!'
        return

    # {u'rbac_rule': [{u'rule_object': u'*', u'rule_perms': [{u'role_crud': u'CRUD', u'role_name': u'admin'}], u'rule_field': None}]}
    rule_list = api_access_list_entries.get_rbac_rule()
    print 'Rules (%d):' % len(rule_list)
    print '----------'
    idx = 1
    for rule in rule_list:
            o = rule.rule_object
            f = rule.rule_field
            ps = ''
            for p in rule.rule_perms:
                ps += p.role_name + ':' + p.role_crud + ','
            o_f = "%s.%s" % (o,f) if f else o
            print '%2d %-32s   %s' % (idx, o_f, ps)
            idx += 1
    print ''

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

def vnc_fix_api_access_list(vnc_lib, pobj, rule_str = None):
    rg_name = list(pobj.get_fq_name())
    rg_name.append('default-api-access-list')
    rg = vnc_read_obj(vnc_lib, 'api-access-list', name = rg_name)

    create = False
    if rg == None:
        rg = ApiAccessList(
                 name = 'default-api-access-list',
                 parent_obj = pobj,
                 api_access_list_entries = None)
        create = True
    rule_list = []
    if rule_str:
        rule = build_rule(rule_str)
        rule_list.append(rule)
    rentry = RbacRuleEntriesType(rule_list)
    rg.set_api_access_list_entries(rentry)
    if create:
        print 'API access list empty. Creating with default rule'
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
    # print '**** Generate token %s ****' % rval
    return rval

class MyVncApi(VncApi):
    def __init__(self, username = None, password = None,
        tenant_name = None, api_server_host = None, api_server_port = None):
        self._username = username
        self._tenant_name = tenant_name
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
        return new_headers

# This is needed for VncApi._authenticate invocation from within Api server.
# We don't have access to user information so we hard code admin credentials.
def ks_admin_authenticate(response=None, headers=None):
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

class TestPermissions(test_case.ApiServerTestCase):
    domain_name = 'default-domain'
    fqdn = [domain_name]
    vn_name='alice-vn'

    def setUp(self):
        extra_mocks = [(keystone.Client,
                            '__new__', test_utils.get_keystone_client),
                       (vnc_api.vnc_api.VncApi,
                            '_authenticate',  ks_admin_authenticate),
                       (auth_token.AuthProtocol,
                            '__new__', test_utils.get_keystone_auth_protocol)]
        super(TestPermissions, self).setUp(extra_mocks=extra_mocks)

        ip = self._api_server_ip
        port = self._api_server_port
        # kc = test_utils.get_keystone_client()
        kc = keystone.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')

        # prepare token before vnc api invokes keystone
        alice = User(ip, port, kc, 'alice', 'alice123', 'alice-role', 'alice-proj')
        bob =   User(ip, port, kc, 'bob', 'bob123', 'bob-role', 'bob-proj')
        admin = User(ip, port, kc, 'admin', 'contrail123', 'admin', 'admin')

        self.alice = alice
        self.bob   = bob
        self.admin = admin
        self.users = [self.alice, self.bob]

        """
        1. create project in API server
        2. read objects back and pupolate locally
        3. reassign ownership of projects to user from admin
        """
        for user in [admin, alice, bob]:
            project_obj = Project(user.project)
            project_obj.uuid = user.project_uuid
            print 'Creating Project object for %s, uuid %s' \
                % (user.project, user.project_uuid)
            admin.vnc_lib.project_create(project_obj)

            # read projects back
            user.project_obj = vnc_read_obj(admin.vnc_lib,
                'project', obj_uuid = user.project_uuid)

            print 'Change owner of project %s to %s' % (user.project, user.project_uuid)
            set_perms(user.project_obj, owner=user.project_uuid, share = [])
            admin.vnc_lib.project_update(user.project_obj)

        # delete test VN if it exists
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = vnc_read_obj(admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        if vn:
            print '%s exists ... deleting to start fresh' % vn_fq_name
            admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # allow permission to create objects
        for user in self.users:
            print "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role)
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj,
                rule_str = '* %s:CRUD' % user.role)

    # delete api-access-list for alice and bob and disallow api access to their projects
    # then try to create VN in the project. This should fail
    def test_api_access_create(self):
        for user in self.users:
            print "Delete api-acl for project %s" % user.project
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj, rule_str = None)

        print 'alice: trying to create VN in her project'
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            print '*** Created virtual network %s ... test failed!' % vn.get_fq_name()
            testfail = True
        except cfgm_common.exceptions.PermissionDenied as e:
            print 'Failed to create VN ... Test passes!'
            testfail = False
        self.assertThat(testfail, Equals(False))

        # allow permission to create objects
        for user in self.users:
            print "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role)
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj,
                rule_str = '* %s:CRUD' % user.role)

        print ''
        print 'alice: trying to create VN in her project'
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            print 'Created virtual network %s ... test passed!' % vn.get_fq_name()
            testfail = False
        except PermissionDenied as e:
            print 'Failed to create VN ... Test failed!'
            testfail = True
        self.assertThat(testfail, Equals(False))
    # end test_api_access_create

    def test_shared_with_tenant(self):
        # alice to create virtual network
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())

        print
        print '########### READ (SHARED WITH TENANT) ##################'
        print 'Disable share in virtual networks for others'
        set_perms(vn, share = [], globally_shared = False)
        self.alice.vnc_lib.virtual_network_update(vn)

        print 'Reading VN as bob ... should fail'
        try:
            net_obj = self.bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            print '*** Succeeded in reading VN. Test failed!'
            testfail = True
        except PermissionDenied as e:
            print 'Failed to read VN ... Test passed!'
            testfail = False
        self.assertThat(testfail, Equals(False))

        print 'Enable share in virtual networks for bob project'
        set_perms(vn, share = [(self.bob.project_uuid, PERMS_R)])
        self.alice.vnc_lib.virtual_network_update(vn)

        print 'Reading VN as bob ... should succeed'
        try:
            net_obj = self.bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            print '*** Succeeded in reading VN. Test passed!'
            testfail = False
        except PermissionDenied as e:
            print 'Failed to read VN ... Test failed!'
            testfail = True
        self.assertThat(testfail, Equals(False))
    # end test_shared_with_tenant

    def tearDown(self):
        self._api_svr_greenlet.kill()
        self._api_server._db_conn._msgbus.shutdown()
        test_utils.FakeIfmapClient.reset()
        test_utils.CassandraCFs.reset()
        super(TestPermissions, self).tearDown()
    # end tearDown

# class TestPermissions
