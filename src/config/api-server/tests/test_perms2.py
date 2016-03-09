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

       if self.name not in kc_users:
           logger.info( 'user %s missing from keystone ... creating' % self.name)
           user = kc.users.create(self.name, self.password, '', tenant_id=tenant.id)

       role_dict = {role.name:role for role in kc.roles.list()}
       user_dict = {user.name:user for user in kc.users.list()}

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
        logger.info( '*** Unable to set perms2 in object %s' % obj.get_fq_name())
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
def vnc_read_obj(vnc, obj_type, name = None, obj_uuid = None):
    if name is None and obj_uuid is None:
        logger.info( 'Need FQN or UUID to read object')
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
        logger.info( '%s %s not found!' % (obj_type, name if name else obj_uuid))
        return None
    except PermissionDenied:
        logger.info( 'Permission denied reading %s %s' % (obj_type, name))
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
    # logger.info( '**** Generate token %s ****' % rval)
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
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

class TestPermissions(test_case.ApiServerTestCase):
    domain_name = 'default-domain'
    fqdn = [domain_name]
    vn_name='alice-vn'

    @classmethod
    def setUpClass(cls):
        extra_mocks = [(keystone.Client,
                            '__new__', test_utils.FakeKeystoneClient),
                       (vnc_api.vnc_api.VncApi,
                            '_authenticate',  ks_admin_authenticate),
                       (auth_token, 'AuthProtocol',
                            test_utils.FakeAuthProtocol)]
        extra_config_knobs = [
            ('DEFAULTS', 'multi_tenancy_with_rbac', 'True'),
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        super(TestPermissions, cls).setUpClass(extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestPermissions, self).setUp()
        ip = self._api_server_ip
        port = self._api_server_port
        # kc = test_utils.get_keystone_client()
        kc = keystone.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')

        # prepare token before vnc api invokes keystone
        alice = User(ip, port, kc, 'alice', 'alice123', 'alice-role', 'alice-proj-%s' % self.id())
        bob =   User(ip, port, kc, 'bob', 'bob123', 'bob-role', 'bob-proj-%s' % self.id())
        admin = User(ip, port, kc, 'admin', 'contrail123', 'admin', 'admin-%s' % self.id())

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
            logger.info( 'Creating Project object for %s, uuid %s' \
                % (user.project, user.project_uuid))
            admin.vnc_lib.project_create(project_obj)

            # read projects back
            user.project_obj = vnc_read_obj(admin.vnc_lib,
                'project', obj_uuid = user.project_uuid)

            logger.info( 'Change owner of project %s to %s' % (user.project, user.project_uuid))
            set_perms(user.project_obj, owner=user.project_uuid, share = [])
            admin.vnc_lib.project_update(user.project_obj)

        # delete test VN if it exists
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = vnc_read_obj(admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        if vn:
            logger.info( '%s exists ... deleting to start fresh' % vn_fq_name)
            admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # allow permission to create objects
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj,
                rule_str = '* %s:CRUD' % user.role)

    # delete api-access-list for alice and bob and disallow api access to their projects
    # then try to create VN in the project. This should fail
    def test_api_access(self):
        logger.info('')
        logger.info( '########### API ACCESS (CREATE) ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin

        rv_json = admin.vnc_lib._request(rest.OP_GET, '/multi-tenancy-with-rbac')
        rv = json.loads(rv_json)
        self.assertEquals(rv["enabled"], True)

        # disable rbac

        # delete api-access-list for alice and bob and disallow api access to their projects
        for user in self.users:
            logger.info( "Delete api-acl for project %s to disallow api access" % user.project)
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj, rule_str = None)

        logger.info( 'alice: trying to create VN in her project')
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            self.assertTrue(False, '*** Created virtual network ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to create VN ... Test passes!')

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj,
                rule_str = 'virtual-networks %s:C' % user.role)

        logger.info( '')
        logger.info( 'alice: trying to create VN in her project')
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            logger.info( 'Created virtual network %s ... test passed!' % vn.get_fq_name())
            testfail = False
        except PermissionDenied as e:
            logger.info( 'Failed to create VN ... Test failed!')
            testfail = True
        self.assertThat(testfail, Equals(False))

        logger.info('')
        logger.info( '########### API ACCESS (READ) ##################')
        logger.info( 'alice: trying to read VN in her project (should fail)')
        try:
            vn_fq_name = [self.domain_name, self.alice.project, self.vn_name]
            vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
            self.assertTrue(False, '*** Read VN without read permission')
        except PermissionDenied as e:
            self.assertTrue(True, 'Unable to read VN ... test passed')

        # allow read access
        vnc_fix_api_access_list(self.admin.vnc_lib, self.alice.project_obj,
                rule_str = 'virtual-network %s:R' % self.alice.role)
        logger.info( 'alice: added permission to read virtual-network')
        logger.info( 'alice: trying to read VN in her project (should succeed)')
        try:
            vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
            self.assertTrue(True, 'Read VN successfully ... test passed')
        except PermissionDenied as e:
            self.assertTrue(False, '*** Read VN failed ... test failed!!!')

        logger.info('')
        logger.info( '########### API ACCESS (UPDATE) ##################')
        logger.info( 'alice: trying to update VN in her project (should fail)')
        try:
            vn.display_name = "foobar"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, '*** Set field in VN ... test failed!')
            testfail += 1
        except PermissionDenied as e:
            self.assertTrue(True, 'Unable to update field in VN ... Test succeeded!')

        # give update API access to alice
        vnc_fix_api_access_list(admin.vnc_lib, alice.project_obj,
                rule_str = 'virtual-network %s:U' % alice.role)

        logger.info( '')
        logger.info( 'alice: added permission to update virtual-network')
        logger.info( 'alice: trying to set field in her VN ')
        try:
            vn.display_name = "foobar"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(True, 'Set field in VN ... test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, '*** Failed to update field in VN ... Test failed!')
            testfail += 1
        if testfail > 0:
            sys.exit()

        vn2 = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())
        logger.info( 'alice: display_name %s' % vn2.display_name)
        self.assertEquals(vn2.display_name, "foobar")

        logger.info('')
        logger.info( '####### API ACCESS (update field restricted to admin) ##############')
        logger.info( 'Restricting update of field to admin only        ')
        vnc_fix_api_access_list(admin.vnc_lib, alice.project_obj,
                rule_str = 'virtual-network.display_name admin:U')
        try:
            vn.display_name = "alice"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, '*** Set field in VN  ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to update field in VN ... Test passed!')

        logger.info('')
        logger.info( '########### API ACCESS (DELETE) ##################')
        logger.info( 'alice: try deleting VN .. should fail')

        # delete test VN  ... should fail
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        try:
            alice.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(False, '*** Deleted VN ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Error deleting VN ... test passed!')

        logger.info('')
        logger.info( '############### PERMS2 ##########################')
        logger.info( 'Giving bob API level access to perform all ops on virtual-network')
        logger.info( "Bob should'nt be able to create VN in alice project because only\n ")
        logger.info( 'owner (alice) has write permission in her project')
        vnc_fix_api_access_list(admin.vnc_lib, bob.project_obj,
                rule_str = 'virtual-network %s:CRUD' % bob.role)

        logger.info( '')
        logger.info( 'bob: trying to create VN in alice project ... should fail')
        try:
            vn2 = VirtualNetwork('bob-vn-in-alice-project', alice.project_obj)
            bob.vnc_lib.virtual_network_create(vn2)
            self.assertTrue(False, '*** Created virtual network ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to create VN ... Test passed!')

        vn = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn_fq_name)

        logger.info('')
        logger.info( '######### READ (SHARED WITH ANOTHER TENANT) ############')
        logger.info( 'Disable share in virtual networks for others')
        set_perms(vn, share = [], global_access = PERMS_NONE)
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should fail')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(False, '*** Succeeded in reading VN. Test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to read VN ... Test passed!')

        logger.info( 'Enable share in virtual network for bob project')
        set_perms(vn, share = [(bob.project_uuid, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should succeed')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(True, 'Succeeded in reading VN. Test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, '*** Failed to read VN ... Test failed!')

        logger.info('')
        logger.info( '########### READ (DISABLE READ SHARING) ##################')
        logger.info( 'Disable share in virtual networks for others')
        set_perms(vn, share = [])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should fail')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(False, 'Succeeded in reading VN. Test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, '*** Failed to read VN ... Test passed!')

        logger.info('')
        logger.info( '########### READ (GLOBALLY SHARED) ##################')
        logger.info( 'Enable virtual networks in alice project for global sharing (read only)')
        set_perms(vn, share = [], global_access = PERMS_R)
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should succeed')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(True, 'Succeeded in reading VN. Test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, '*** Failed to read VN ... Test failed!')

        logger.info( '########### WRITE (GLOBALLY SHARED) ##################')
        logger.info( 'Writing shared VN as bob ... should fail')
        try:
            vn.display_name = "foobar"
            bob.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, '*** Succeeded in updating VN. Test failed!!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to update VN ... Test passed!')

        logger.info('')
        logger.info( 'Enable virtual networks in alice project for global sharing (read, write)')
        logger.info( 'Writing shared VN as bob ... should succeed')
        # important: read VN afresh to overwrite display_name update pending status
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
        set_perms(vn, global_access = PERMS_RW)
        alice.vnc_lib.virtual_network_update(vn)
        try:
            vn.display_name = "foobar"
            bob.vnc_lib.virtual_network_update(vn)
            self.assertTrue(True, 'Succeeded in updating VN. Test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, '*** Failed to update VN ... Test failed!!')

        logger.info( '')
        logger.info( '########################### COLLECTIONS #################')
        logger.info( 'User should be able to see VN in own project and any shared')
        logger.info( 'alice: get virtual network collection ... should fail')

        try:
            x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
            self.assertTrue(False,
                '*** Read VN collection without list permission ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to read VN collection ... test passed')

        # allow permission to read virtual-network collection
        for user in [alice, bob]:
            logger.info( "%s: project %s to allow collection access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_fix_api_access_list(admin.vnc_lib, user.project_obj,
                rule_str = 'virtual-networks %s:CR' % user.role)

        # create one more VN in alice project to differentiate from what bob sees
        vn2 = VirtualNetwork('second-vn', alice.project_obj)
        alice.vnc_lib.virtual_network_create(vn2)
        logger.info( 'Alice: created additional VN %s in her project' % vn2.get_fq_name())

        logger.info( 'Alice: network list')
        x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
        for item in x['virtual-networks']:
            logger.info( '    %s: %s' % (item['uuid'], item['fq_name']))
        expected = set(['alice-vn', 'second-vn'])
        received = set([item['fq_name'][-1] for item in x['virtual-networks']])
        self.assertEquals(expected, received)

        logger.info('')
        logger.info( 'Bob: network list')
        y = bob.vnc_lib.virtual_networks_list(parent_id = bob.project_uuid)
        for item in y['virtual-networks']:
            logger.info( '    %s: %s' % (item['uuid'], item['fq_name']))
        # need changes in auto code generation for lists
        expected = set(['alice-vn'])
        received = set([item['fq_name'][-1] for item in y['virtual-networks']])

        self.assertEquals(expected, received)

    def test_check_obj_perms_api(self):
        logger.info('')
        logger.info( '########### CHECK OBJ PERMS API ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_fix_api_access_list(self.admin.vnc_lib, user.project_obj,
                rule_str = 'virtual-networks %s:CRUD' % user.role)

        logger.info( '')
        logger.info( 'alice: trying to create VN in her project')
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            logger.info( 'Created virtual network %s ... test passed!' % vn.get_fq_name())
            testfail = False
        except PermissionDenied as e:
            logger.info( 'Failed to create VN ... Test failed!')
            testfail = True
        self.assertThat(testfail, Equals(False))

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':''}
        for user in self.users:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info( 'Enable share in virtual network for bob project')
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
        set_perms(vn, share = [(bob.project_uuid, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in self.users:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info('')
        logger.info( '########### READ (DISABLE READ SHARING) ##################')
        logger.info( 'Disable share in virtual networks for others')
        set_perms(vn, share = [])
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':''}
        for user in self.users:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])
        logger.info( 'Reading VN as bob ... should fail')

        logger.info('')
        logger.info( '########### READ (GLOBALLY SHARED) ##################')
        logger.info( 'Enable virtual networks in alice project for global sharing (read only)')
        set_perms(vn, share = [], global_access = PERMS_R)
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in self.users:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])
        logger.info( 'Reading VN as bob ... should fail')

        logger.info('')
        logger.info( '########### WRITE (GLOBALLY SHARED) ##################')
        logger.info( 'Enable virtual networks in alice project for global sharing (read, write)')
        set_perms(vn, global_access = PERMS_RW)
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'RW'}
        for user in self.users:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])


    def tearDown(self):
        super(TestPermissions, self).tearDown()
    # end tearDown

