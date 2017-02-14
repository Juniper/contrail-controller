#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import uuid
import logging
import coverage

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import json
import bottle

from vnc_api.vnc_api import *
import keystoneclient.exceptions as kc_exceptions
import keystoneclient.v2_0.client as keystone
from keystonemiddleware import auth_token
from cfgm_common.rbaclib import *
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

from vnc_api.gen.resource_xsd import PermType, PermType2, IdPermsType
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

       logger.info( 'Adding user %s with role %s to tenant %s' \
            % (name, role, project))
       try:
           kc.roles.add_user_role(user_dict[self.name], role_dict[self.role], tenant)
       except kc_exceptions.Conflict:
           pass

       self.vnc_lib = MyVncApi(username = self.name, password = self.password,
            tenant_name = self.project, tenant_id = self.project_uuid, user_role = role,
            api_server_host = apis_ip, api_server_port = apis_port)
   # end __init__

   def api_acl_name(self):
       rg_name = list(self.project_obj.get_fq_name())
       rg_name.append('default-api-access-list')
       return rg_name

   def check_perms(self, obj_uuid):
       rv = self.vnc_lib.obj_perms(self.vnc_lib.get_auth_token(), obj_uuid)
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

def vnc_aal_create(vnc, pobj):
    rg_name = list(pobj.get_fq_name())
    rg_name.append('default-api-access-list')
    rg = vnc_read_obj(vnc, 'api-access-list', name = rg_name)
    if rg == None:
        rge = RbacRuleEntriesType([])
        rg = ApiAccessList(
                 name = 'default-api-access-list',
                 parent_obj = pobj,
                 api_access_list_entries = rge)
        vnc.api_access_list_create(rg)
    return rg

def vnc_aal_add_rule(vnc, rg, rule_str):
    rule = build_rule(rule_str)
    rg = vnc_read_obj(vnc, 'api-access-list', rg.get_fq_name())
    rge = rg.get_api_access_list_entries()
    match = find_rule(rge, rule)
    if not match:
        rge.add_rbac_rule(rule)
    else:
        build_perms(rge.rbac_rule[match[0]-1], match[3])

    rg.set_api_access_list_entries(rge)
    vnc.api_access_list_update(rg)

def token_from_user_info(user_name, tenant_name, domain_name, role_name,
        tenant_id = None, domain_id = None):
    token_dict = {
        'X-User': user_name,
        'X-User-Name': user_name,
        'X-Project-Name': tenant_name,
        'X-Project-Id': tenant_id or '',
        'X-Domain-Id' : domain_id or '',
        'X-Domain-Name' : domain_name,
        'X-Role': role_name,
    }
    rval = json.dumps(token_dict)
    # logger.info( 'Generate token %s' % rval)
    return rval

class MyVncApi(VncApi):
    def __init__(self, username = None, password = None,
        tenant_name = None, tenant_id = None, user_role = None,
        api_server_host = None, api_server_port = None):
        self._username = username
        self._tenant_name = tenant_name
        self._tenant_id = tenant_id
        self._user_role = user_role
        self._domain_id = None
        VncApi.__init__(self, username = username, password = password,
            tenant_name = tenant_name, api_server_host = api_server_host,
            api_server_port = api_server_port)

    def set_domain_id(self, domain_id):
        self._domain_id = domain_id

    def _authenticate(self, response=None, headers=None):
        rval = token_from_user_info(self._username, self._tenant_name,
            'default-domain', self._user_role, self._tenant_id, self._domain_id)
        new_headers = headers or {}
        new_headers['X-AUTH-TOKEN'] = rval
        self._auth_token = rval
        return new_headers

# This is needed for VncApi._authenticate invocation from within Api server.
# We don't have access to user information so we hard code admin credentials.
def ks_admin_authenticate(self, response=None, headers=None):
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'cloud-admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

class TestPermissions(test_case.ApiServerTestCase):
    domain_name = 'default-domain'
    fqdn = [domain_name]

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_mocks = [(keystone.Client,
                            '__new__', test_utils.FakeKeystoneClient),
                       (vnc_api.vnc_api.VncApi,
                            '_authenticate',  ks_admin_authenticate),
                       (auth_token, 'AuthProtocol',
                            test_utils.FakeAuthProtocol)]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'global_read_only_role', 'read-only-role'),
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        super(TestPermissions, cls).setUpClass(extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPermissions, cls).tearDownClass(*args, **kwargs)


    def setUp(self):
        super(TestPermissions, self).setUp()
        ip = self._api_server_ip
        port = self._api_server_port
        # kc = test_utils.get_keystone_client()
        kc = keystone.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')

        # prepare token before vnc api invokes keystone
        self.alice = User(ip, port, kc, 'alice', 'alice123', 'alice-role', 'alice-proj-%s' % self.id())
        self.bob =   User(ip, port, kc, 'bob', 'bob123', 'bob-role', 'bob-proj-%s' % self.id())
        self.admin = User(ip, port, kc, 'admin', 'contrail123', 'cloud-admin', 'admin-%s' % self.id())
        self.admin1 = User(ip, port, kc, 'admin1', 'contrail123', 'admin', 'admin1-%s' % self.id())
        self.admin2 = User(ip, port, kc, 'admin2', 'contrail123', 'admin', 'admin2-%s' % self.id())
        self.adminr = User(ip, port, kc, 'adminr', 'contrail123', 'read-only-role', 'adminr-%s' % self.id())

        self.users = [self.alice, self.bob, self.admin, self.admin1, self.admin2, self.adminr]

        """
        1. create project in API server
        2. read objects back and pupolate locally
        3. reassign ownership of projects to user from admin
        """
        for user in self.users:
            project_obj = Project(user.project)
            project_obj.uuid = user.project_uuid
            logger.info( 'Creating Project object for %s, uuid %s' \
                % (user.project, user.project_uuid))
            self.admin.vnc_lib.project_create(project_obj)

            # read projects back
            user.project_obj = vnc_read_obj(self.admin.vnc_lib,
                'project', obj_uuid = user.project_uuid)
            user.domain_id = user.project_obj.parent_uuid
            user.vnc_lib.set_domain_id(user.project_obj.parent_uuid)

            logger.info( 'Change owner of project %s to %s' % (user.project, user.project_uuid))
            set_perms(user.project_obj, owner=user.project_uuid, share = [])
            self.admin.vnc_lib.project_update(user.project_obj)

        # allow permission to create objects (including obj-perms)
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            user.proj_rg = vnc_aal_create(self.admin.vnc_lib, user.project_obj)
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                rule_str = '* %s:CRUD' % user.role)

        """
        global_rg = vnc_read_obj(self.admin.vnc_lib, 'api-access-list',
            name = ['default-global-system-config', 'default-api-access-list'])
        vnc_aal_add_rule(self.admin.vnc_lib, global_rg, "obj-perms *:R")
        """

    def test_delete_non_admin_role(self):
        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        # allow permission to create all objects
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                rule_str = '* %s:CRUD' % user.role)

        vn_fq_name = [self.domain_name, alice.project, self.vn_name]

        # delete test VN if it exists
        if vnc_read_obj(admin.vnc_lib, 'virtual-network', name = vn_fq_name):
            logger.info( '%s exists ... deleting to start fresh' % vn_fq_name)
            admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)

        # bob - delete VN  ... should fail
        try:
            bob.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(False, 'Bob Deleted VN ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Error deleting VN ... test passed!')

        # Alice - delete VN  ... should succeed
        try:
            alice.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(True, 'Deleted VN ... test succeeded!')
        except PermissionDenied as e:
            self.assertTrue(False, 'Alice Error deleting VN ... test failed!')
    # end

    def test_delete_admin_role(self):
        vn = VirtualNetwork('admin1-vn', self.admin1.project_obj)
        vn_fq_name = vn.get_fq_name()

        # delete test VN if it exists
        if vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name):
            logger.info( '%s exists ... deleting to start fresh' % vn_fq_name)
            self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        self.admin1.vnc_lib.virtual_network_create(vn)

        # admin2 - delete VN  ... should fail
        try:
            self.admin2.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(False, 'Deleted VN ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Error deleting VN ... test passed!')

        # admin1 - delete VN  ... should succeed
        try:
            self.admin1.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(True, 'Deleted VN ... test succeeded!')
        except PermissionDenied as e:
            self.assertTrue(False, 'Error deleting VN ... test failed!')
    # end

    # delete api-access-list for alice and bob and disallow api access to their projects
    # then try to create VN in the project. This should fail
    def test_api_access(self):
        logger.info('')
        logger.info( '########### API ACCESS (CREATE) ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        rv = admin.vnc_lib.get_aaa_mode()
        self.assertEquals(rv["aaa-mode"], "rbac")

        # delete api-access-list for alice and bob and disallow api access to their projects
        for user in self.users:
            logger.info( "Delete api-acl for project %s to disallow api access" % user.project)
            rg_name = list(user.project_obj.get_fq_name())
            rg_name.append('default-api-access-list')
            self.admin.vnc_lib.api_access_list_delete(fq_name = rg_name)

        logger.info( 'alice: trying to create VN in her project')
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            self.assertTrue(False, 'Created virtual network ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to create VN ... Test passes!')

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            user.proj_rg = vnc_aal_create(self.admin.vnc_lib, user.project_obj)
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
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
            self.assertTrue(False, 'Read VN without read permission')
        except PermissionDenied as e:
            self.assertTrue(True, 'Unable to read VN ... test passed')

        # allow read access
        vnc_aal_add_rule(self.admin.vnc_lib, self.alice.proj_rg,
                rule_str = 'virtual-network %s:R' % self.alice.role)
        logger.info( 'alice: added permission to read virtual-network')
        logger.info( 'alice: trying to read VN in her project (should succeed)')
        try:
            vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
            self.assertTrue(True, 'Read VN successfully ... test passed')
        except PermissionDenied as e:
            self.assertTrue(False, 'Read VN failed ... test failed!!!')

        logger.info('')
        logger.info( '########### API ACCESS (UPDATE) ##################')
        logger.info( 'alice: trying to update VN in her project (should fail)')
        try:
            vn.display_name = "foobar"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, 'Set field in VN ... test failed!')
            testfail += 1
        except PermissionDenied as e:
            self.assertTrue(True, 'Unable to update field in VN ... Test succeeded!')

        # give update API access to alice
        vnc_aal_add_rule(admin.vnc_lib, alice.proj_rg,
                rule_str = 'virtual-network %s:U' % alice.role)

        logger.info( '')
        logger.info( 'alice: added permission to update virtual-network')
        logger.info( 'alice: trying to set field in her VN ')
        try:
            vn.display_name = "foobar"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(True, 'Set field in VN ... test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to update field in VN ... Test failed!')
            testfail += 1
        if testfail > 0:
            sys.exit()

        vn2 = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())
        logger.info( 'alice: display_name %s' % vn2.display_name)
        self.assertEquals(vn2.display_name, "foobar")

        logger.info('')
        logger.info( '####### API ACCESS (update field restricted to admin) ##############')
        logger.info( 'Restricting update of field to admin only        ')
        vnc_aal_add_rule(admin.vnc_lib, alice.proj_rg,
                rule_str = 'virtual-network.display_name admin:U')
        try:
            vn.display_name = "alice"
            alice.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, 'Set field in VN  ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to update field in VN ... Test passed!')

        logger.info('')
        logger.info( '########### API ACCESS (DELETE) ##################')
        logger.info( 'alice: try deleting VN .. should fail')

        # delete test VN  ... should fail
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        try:
            alice.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(False, 'Deleted VN ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Error deleting VN ... test passed!')

        logger.info('')
        logger.info( '############### PERMS2 ##########################')
        logger.info( 'Giving bob API level access to perform all ops on virtual-network')
        logger.info( "Bob should'nt be able to create VN in alice project because only\n ")
        logger.info( 'owner (alice) has write permission in her project')
        vnc_aal_add_rule(admin.vnc_lib, bob.proj_rg,
                rule_str = 'virtual-network %s:CRUD' % bob.role)

        logger.info( '')
        logger.info( 'bob: trying to create VN in alice project ... should fail')
        try:
            vn2 = VirtualNetwork('bob-vn-in-alice-project', alice.project_obj)
            bob.vnc_lib.virtual_network_create(vn2)
            self.assertTrue(False, 'Created virtual network ... test failed!')
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
            self.assertTrue(False, 'Succeeded in reading VN. Test failed!')
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
            self.assertTrue(False, 'Failed to read VN ... Test failed!')

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
            self.assertTrue(True, 'Failed to read VN ... Test passed!')

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
            self.assertTrue(False, 'Failed to read VN ... Test failed!')

        logger.info( '########### WRITE (GLOBALLY SHARED) ##################')
        logger.info( 'Writing shared VN as bob ... should fail')
        try:
            vn.display_name = "foobar"
            bob.vnc_lib.virtual_network_update(vn)
            self.assertTrue(False, 'Succeeded in updating VN. Test failed!!')
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
            self.assertTrue(False, 'Failed to update VN ... Test failed!!')

        logger.info( '')
        logger.info( '########################### COLLECTIONS #################')
        logger.info( 'User should be able to see VN in own project and any shared')
        logger.info( 'alice: get virtual network collection ... should fail')

        try:
            x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
            self.assertTrue(False,
                'Read VN collection without list permission ... test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to read VN collection ... test passed')

        # allow permission to read virtual-network collection
        for user in [alice, bob]:
            logger.info( "%s: project %s to allow collection access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_aal_add_rule(admin.vnc_lib, user.proj_rg,
                rule_str = 'virtual-networks %s:CR' % user.role)

        # create one more VN in alice project to differentiate from what bob sees
        vn2 = VirtualNetwork('second-vn', alice.project_obj)
        alice.vnc_lib.virtual_network_create(vn2)
        logger.info( 'Alice: created additional VN %s in her project' % vn2.get_fq_name())

        logger.info( 'Alice: network list')
        x = alice.vnc_lib.virtual_networks_list(parent_id = alice.project_uuid)
        for item in x['virtual-networks']:
            logger.info( '    %s: %s' % (item['uuid'], item['fq_name']))
        expected = set([self.vn_name, 'second-vn'])
        received = set([item['fq_name'][-1] for item in x['virtual-networks']])
        self.assertEquals(expected, received)

        logger.info('')
        logger.info( 'Bob: network list')
        y = bob.vnc_lib.virtual_networks_list(parent_id = bob.project_uuid)
        for item in y['virtual-networks']:
            logger.info( '    %s: %s' % (item['uuid'], item['fq_name']))
        # list should be empty because parent_id filter precludes sharing
        expected = set([])
        received = set([item['fq_name'][-1] for item in y['virtual-networks']])
        self.assertEquals(expected, received)

        logger.info('')
        logger.info( 'Bob: network list')
        y = bob.vnc_lib.virtual_networks_list()
        for item in y['virtual-networks']:
            logger.info( '    %s: %s' % (item['uuid'], item['fq_name']))
        # list should be non-empty because missing filter will enable sharing
        expected = set([self.vn_name])
        received = set([item['fq_name'][-1] for item in y['virtual-networks']])
        self.assertEquals(expected, received)

    def test_shared_access(self):
        logger.info('')
        logger.info( '########### API ACCESS (CREATE) ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            user.proj_rg = vnc_aal_create(self.admin.vnc_lib, user.project_obj)
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                rule_str = '* %s:CRUD' % user.role)

        logger.info( 'alice: create VN in her project')
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn.get_fq_name())

        logger.info( 'Reading VN as bob ... should fail')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(False, 'Succeeded in reading VN. Test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to read VN ... Test passed!')

        logger.info( 'Enable default share in virtual network for bob project')
        set_perms(vn, share = [(bob.project_uuid, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should succeed')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(True, 'Succeeded in reading VN. Test passed!')
            net_objs = bob.vnc_lib.virtual_networks_list(shared=True)
            self.assertTrue(vn.get_uuid(), net_objs['virtual-networks'][0]['uuid'])
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to read VN ... Test failed!')

        logger.info('')
        logger.info( 'Disable share in virtual networks for others')
        set_perms(vn, share = [])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should fail')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(False, 'Succeeded in reading VN. Test failed!')
        except PermissionDenied as e:
            self.assertTrue(True, 'Failed to read VN ... Test passed!')

        net_objs = bob.vnc_lib.virtual_networks_list(shared=True)
        for net_obj in net_objs['virtual-networks']:
            self.assertNotEquals(vn.get_uuid(), net_obj['uuid'])
        logger.info( 'Enable "tenant" scope share in virtual network for bob project')
        set_perms(vn, share = [('tenant:'+bob.project_uuid, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should succeed')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(True, 'Succeeded in reading VN. Test passed!')
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to read VN ... Test failed!')

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
            self.assertTrue(True, 'Failed to read VN ... Test passed!')

        logger.info( 'Enable domain scope share in virtual network for bob domain')
        set_perms(vn, share = [('domain:'+bob.domain_id, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        logger.info( 'Reading VN as bob ... should succeed')
        try:
            net_obj = bob.vnc_lib.virtual_network_read(id=vn.get_uuid())
            self.assertTrue(True, 'Succeeded in reading VN. Test passed!')
            net_objs = bob.vnc_lib.virtual_networks_list(shared=True)
            self.assertTrue(vn.get_uuid(), net_objs['virtual-networks'][0]['uuid'])
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to read VN ... Test failed!')

    def test_domain_sharing(self):
        # validate domain sharing is enabled for domain by default
        domain_name = 'domain-%s' % self.id()
        domain = Domain(domain_name)
        self.admin.vnc_lib.domain_create(domain)
        dom = vnc_read_obj(self.admin.vnc_lib, 'domain', name = [domain_name])
        share_set = set(["%s:%d" % (item.tenant, item.tenant_access) for item in dom.get_perms2().share])
        self.assertTrue('domain:%s:%d' % (dom.uuid, cfgm_common.DOMAIN_SHARING_PERMS) in share_set,
            "Domain scope not set in domain share list")

        # validate domain sharing is enabled for "default-domain" by default
        dom = vnc_read_obj(self.admin.vnc_lib, 'domain', name = ['default-domain'])
        share_set = set(["%s:%d" % (item.tenant, item.tenant_access) for item in dom.get_perms2().share])
        self.assertTrue('domain:%s:%d' % (dom.uuid, cfgm_common.DOMAIN_SHARING_PERMS) in share_set,
            "Domain scope not set in domain share list")

        # validate non-admin can create virtual-dns
        vdns = VirtualDns(name = "my-vDNS")
        d = VirtualDnsType(domain_name = "test-domain", record_order = "fixed", default_ttl_seconds = 3600)
        vdns.set_virtual_DNS_data(d)
        try:
            self.alice.vnc_lib.virtual_DNS_create(vdns)
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to create vDNS ... Test failed!')
        vdns = vnc_read_obj(self.alice.vnc_lib, 'virtual-DNS', name = vdns.get_fq_name())

        # validate non-admin can create service template
        st = ServiceTemplate(name = "my-st")
        try:
            self.alice.vnc_lib.service_template_create(st)
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to create service-template ... Test failed!')
        st = vnc_read_obj(self.alice.vnc_lib, 'service-template', name = st.get_fq_name())

        # validate anonther-user can't delete other's object due to domain sharing
        with ExpectedException(PermissionDenied) as e:
            self.bob.vnc_lib.service_template_delete(fq_name = st.get_fq_name())

        # validate owner can delete service template
        try:
            self.alice.vnc_lib.service_template_delete(fq_name = st.get_fq_name())
        except PermissionDenied as e:
            self.assertTrue(False, 'Failed to delete service-template ... Test failed!')

    def test_check_obj_perms_api(self):
        logger.info('')
        logger.info( '########### CHECK OBJ PERMS API ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                "virtual-networks %s:CRUD" % user.role)

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
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info( 'Enable share in virtual network for bob project')
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
        set_perms(vn, share = [(bob.project_uuid, PERMS_R)])
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info('')
        logger.info( '########### READ (DISABLE READ SHARING) ##################')
        logger.info( 'Disable share in virtual networks for others')
        set_perms(vn, share = [])
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':''}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])
        logger.info( 'Reading VN as bob ... should fail')

        logger.info('')
        logger.info( '########### READ (GLOBALLY SHARED) ##################')
        logger.info( 'Enable virtual networks in alice project for global sharing (read only)')
        set_perms(vn, share = [], global_access = PERMS_R)
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])
        logger.info( 'Reading VN as bob ... should fail')

        logger.info('')
        logger.info( '########### WRITE (GLOBALLY SHARED) ##################')
        logger.info( 'Enable virtual networks in alice project for global sharing (read, write)')
        set_perms(vn, global_access = PERMS_RW)
        alice.vnc_lib.virtual_network_update(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'RW'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        ExpectedCloudAdminRole = {'alice': False, 'bob': False, 'admin': True, 'adminr': False}
        ExpectedGlobalReadOnlyRole = {'alice': False, 'bob': False, 'admin': False, 'adminr': True}
        for user in [self.alice, self.bob, self.admin, self.adminr]:
            self.assertEquals(user.vnc_lib.is_cloud_admin_role(), ExpectedCloudAdminRole[user.name])
            self.assertEquals(user.vnc_lib.is_global_read_only_role(), ExpectedGlobalReadOnlyRole[user.name])

    def test_check_obj_perms_api_no_auth(self):
        logger.info('')
        logger.info( '########### CHECK OBJ PERMS API ##################')

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        rv = admin.vnc_lib.set_aaa_mode("no-auth")
        self.assertEquals(rv['aaa-mode'], "no-auth")

        vn = VirtualNetwork(self.vn_name, self.admin.project_obj)
        self.admin.vnc_lib.virtual_network_create(vn)

        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'RWX'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        rv = admin.vnc_lib.set_aaa_mode("rbac")
        self.assertEquals(rv['aaa-mode'], "rbac")

    # check owner of internally created ri is cloud-admin (bug #1528796)
    def test_ri_owner(self):
        """
        1) Create a virtual network as a non-admin user.
        2) Verify owner of automatically created routing instance is cloud-admin
        """

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                rule_str = 'virtual-networks %s:CRUD' % user.role)

        # Create VN as non-admin user
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn_obj = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertNotEquals(vn_obj, None)

        # Verify owner of automatically created routing instance is cloud-admin
        ri_name = [self.domain_name, alice.project, self.vn_name, self.vn_name]
        ri = vnc_read_obj(self.admin.vnc_lib, 'routing-instance', name = ri_name)
        self.assertEquals(ri.get_perms2().owner, 'cloud-admin')

    def test_chown_api(self):
        """
        1) Alice creates a VN in her project
        2) Alice changes ownership of her VN to Bob
        """

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        logger.info( 'alice: create VN in her project')
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)

        logger.info( "Verify Bob cannot chown Alice's virtual network")
        self.assertRaises(PermissionDenied, bob.vnc_lib.chown, vn.get_uuid(), bob.project_uuid)

        logger.info( 'Verify Alice can chown virtual network in her project to Bob')
        alice.vnc_lib.chown(vn.get_uuid(), bob.project_uuid)
        self.assertRaises(PermissionDenied, alice.vnc_lib.chown, vn.get_uuid(), alice.project_uuid)
        bob.vnc_lib.chown(vn.get_uuid(), alice.project_uuid)

        # negative test cases

        # Version 4 UUIDs have the form xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        # where x is any hexadecimal digit and y is one of 8, 9, A, or B.
        invalid_uuid = '7a574f27-6934-4970-C767-b4996bd30f36'
        valid_uuid_1 = '7a574f27-6934-4970-8767-b4996bd30f36'
        valid_uuid_2 = '7a574f27693449708767b4996bd30f36'

        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chown(invalid_uuid, vn.get_uuid())
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chown(vn.get_uuid(), owner=invalid_uuid)

        # test valid UUID formats
        alice.vnc_lib.chown(vn.get_uuid(), valid_uuid_1)
        admin.vnc_lib.chown(vn.get_uuid(), alice.project_uuid)
        alice.vnc_lib.chown(vn.get_uuid(), valid_uuid_2)
        admin.vnc_lib.chown(vn.get_uuid(), alice.project_uuid)

        # ensure chown/chmod works even when rbac is disabled
        self.assertRaises(PermissionDenied, alice.vnc_lib.set_aaa_mode, "cloud-admin")
        rv = admin.vnc_lib.set_aaa_mode("cloud-admin")
        self.assertEquals(rv['aaa-mode'], "cloud-admin")
        alice.vnc_lib.chown(vn.get_uuid(), valid_uuid_1)
        admin.vnc_lib.chown(vn.get_uuid(), alice.project_uuid)
        alice.vnc_lib.chmod(vn.get_uuid(), owner=valid_uuid_1)
        admin.vnc_lib.chown(vn.get_uuid(), alice.project_uuid)

        # re-enable rbac for subsequent tests!
        try:
            rv = admin.vnc_lib.set_aaa_mode("rbac")
            self.assertEquals(rv['aaa-mode'], "rbac")
        except Exception:
            self.fail("Error in enabling rbac")

    def test_chmod_api(self):
        """
        1) Alice creates a VN in her project
        3) Alice enables read sharing in VN for Bob's project
        4) Alice enables global read access for VN
        5) Alice enables global read/write access for VN
        """

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        logger.info( 'alice: create VN in her project')
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        vn.set_is_shared(False)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(alice.vnc_lib, 'virtual-network', name = vn_fq_name)

        # validate chmod of global perms reflected in is_shared flag
        alice.vnc_lib.chmod(vn.get_uuid(), global_access=PERMS_R)
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), True)
        alice.vnc_lib.chmod(vn.get_uuid(), global_access=0)
        vn = vnc_read_obj(self.alice.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), False)

        logger.info( "Verify Bob cannot chmod Alice's virtual network")
        self.assertRaises(PermissionDenied, bob.vnc_lib.chmod, vn.get_uuid(), owner=bob.project_uuid)

        logger.info( 'Enable read share in virtual network for bob project')
        alice.vnc_lib.chmod(vn.get_uuid(), share=[(bob.project_uuid,PERMS_R)])
        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(ExpectedPerms[user.name], perms)

        logger.info( 'Disable share in virtual networks for others')
        alice.vnc_lib.chmod(vn.get_uuid(), share=[])
        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':''}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info( 'Enable VN in alice project for global sharing (read only)')
        alice.vnc_lib.chmod(vn.get_uuid(), global_access=PERMS_R)
        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'R'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info( 'Enable virtual networks in alice project for global sharing (read, write)')
        alice.vnc_lib.chmod(vn.get_uuid(), global_access=PERMS_RW)
        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'RW'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        logger.info( 'Enable virtual networks in alice project for global sharing (read, write, link)')
        alice.vnc_lib.chmod(vn.get_uuid(), global_access=PERMS_RWX)
        ExpectedPerms = {'admin':'RWX', 'alice':'RWX', 'bob':'RWX'}
        for user in [alice, bob, admin]:
            perms = user.check_perms(vn.get_uuid())
            self.assertEquals(perms, ExpectedPerms[user.name])

        # negative test cases
        invalid_uuid = '7a574f27-6934-4970-C767-b4996bd30f36'
        valid_uuid_1 = '7a574f27-6934-4970-8767-b4996bd30f36'
        valid_uuid_2 = '7a574f27693449708767b4996bd30f36'

        logger.info( "verify Alice cannot set owner or global access to a bad value")
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), owner_access='7')
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), owner_access=8)
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), global_access='7')
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), global_access=8)

        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(invalid_uuid, owner=vn.get_uuid())
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), owner=invalid_uuid)
        with ExpectedException(BadRequest) as e:
            alice.vnc_lib.chmod(vn.get_uuid(), share=[(invalid_uuid,PERMS_R)])

        # test valid UUID formats
        alice.vnc_lib.chmod(vn.get_uuid(), owner=valid_uuid_1)
        admin.vnc_lib.chmod(vn.get_uuid(), owner=alice.project_uuid)
        alice.vnc_lib.chmod(vn.get_uuid(), owner=valid_uuid_2)
        admin.vnc_lib.chmod(vn.get_uuid(), owner=alice.project_uuid)

    def test_bug_1604986(self):
        """
        1) Create a VN
        2) Make is globally shared
        3) list of virtual-networks should not return VN information twice
        """
        admin = self.admin
        vn_name = "test-vn-1604986"
        vn_fq_name = [self.domain_name, admin.project, vn_name]

        test_vn = VirtualNetwork(vn_name, admin.project_obj)
        self.admin.vnc_lib.virtual_network_create(test_vn)

        z = self.admin.vnc_lib.resource_list('virtual-network')
        test_vn_list = [vn for vn in z['virtual-networks'] if vn['fq_name'][-1] == vn_name]
        self.assertEquals(len(test_vn_list), 1)

        test_vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        set_perms(test_vn, global_access = PERMS_RWX)
        admin.vnc_lib.virtual_network_update(test_vn)

        z = self.admin.vnc_lib.resource_list('virtual-network')
        test_vn_list = [vn for vn in z['virtual-networks'] if vn['fq_name'][-1] == vn_name]
        self.assertEquals(len(test_vn_list), 1)

    def test_shared_network(self):
        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]

        # create VN with 'is_shared' set - validate global_access set in vnc
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        vn.set_is_shared(True)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_perms2().global_access, PERMS_RWX)
        self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # create VN with global_access set - validate 'is_shared' gets set
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        perms = PermType2('cloud-admin', PERMS_RWX, PERMS_RWX, [])
        vn.set_perms2(perms)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), True)
        self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # update VN 'is_shared' after initial create - ensure reflectd in global_access
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_perms2().global_access, 0)
        vn.set_is_shared(True); self.alice.vnc_lib.virtual_network_update(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_perms2().global_access, PERMS_RWX)
        vn.set_is_shared(False); self.alice.vnc_lib.virtual_network_update(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_perms2().global_access, 0)
        self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # VN global_access is reset after initial create - ensure reflected in 'is_shared'
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), False or None)
        perms = vn.get_perms2()
        perms.global_access = PERMS_RWX
        vn.set_perms2(perms); self.alice.vnc_lib.virtual_network_update(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), True)
        perms = vn.get_perms2()
        perms.global_access = 0
        vn.set_perms2(perms); self.alice.vnc_lib.virtual_network_update(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), False)
        self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        """
        # create VN with inconsistent global_access and is_shared set
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        perms = PermType2('cloud-admin', PERMS_RWX, PERMS_RWX, [])
        vn.set_perms2(perms)
        vn.set_is_shared(False);
        with ExpectedException(BadRequest) as e:
            self.alice.vnc_lib.virtual_network_create(vn)
        perms.global_access = PERMS_NONE
        vn.set_perms2(perms)
        vn.set_is_shared(True);
        with ExpectedException(BadRequest) as e:
            self.alice.vnc_lib.virtual_network_create(vn)
        """

        # update VN with inconsistent global_access and is_shared set
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        perms = PermType2('cloud-admin', PERMS_RWX, PERMS_RWX, [])
        vn.set_perms2(perms)
        vn.set_is_shared(True);
        self.alice.vnc_lib.virtual_network_create(vn)
        vn = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertEquals(vn.get_is_shared(), True)
        self.assertEquals(vn.get_perms2().global_access, PERMS_RWX)
        perms.global_access = PERMS_NONE
        vn.set_perms2(perms)
        vn.set_is_shared(True);
        with ExpectedException(BadRequest) as e:
            self.alice.vnc_lib.virtual_network_update(vn)
        perms.global_access = PERMS_RWX
        vn.set_perms2(perms)
        vn.set_is_shared(False);
        with ExpectedException(BadRequest) as e:
            self.alice.vnc_lib.virtual_network_update(vn)
        # self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

    def test_doc_auth(self):
        alice = self.alice

        # delete api-access-list for alice project
        # disallow access to all API except globally allowed
        rg_name = list(alice.project_obj.get_fq_name())
        rg_name.append('default-api-access-list')
        self.admin.vnc_lib.api_access_list_delete(fq_name = rg_name)

        def fake_static_file(*args, **kwargs):
            return
        with test_common.patch(bottle, 'static_file', fake_static_file):
            status_code, result = alice.vnc_lib._http_get('/documentation/index.html')
            self.assertThat(status_code, Equals(200))

        status_code, result = alice.vnc_lib._http_get('/')
        self.assertThat(status_code, Equals(200))

        status_code, result = alice.vnc_lib._http_get('/virtual-networks')
        self.assertThat(status_code, Equals(401))

    # check owner of internally created ri is cloud-admin (bug #1528796)
    def test_ri_owner(self):
        """
        1) Create a virtual network as a non-admin user.
        2) Verify owner of automatically created routing instance is cloud-admin
        """

        alice = self.alice
        bob   = self.bob
        admin = self.admin
        self.vn_name = "alice-vn-%s" % self.id()

        # allow permission to create virtual-network
        for user in self.users:
            logger.info( "%s: project %s to allow full access to role %s" % \
                (user.name, user.project, user.role))
            # note that collection API is set for create operation
            vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                rule_str = 'virtual-networks %s:CRUD' % user.role)

        # Create VN as non-admin user
        vn_fq_name = [self.domain_name, alice.project, self.vn_name]
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        self.alice.vnc_lib.virtual_network_create(vn)
        vn_obj = vnc_read_obj(self.admin.vnc_lib, 'virtual-network', name = vn_fq_name)
        self.assertNotEquals(vn_obj, None)

        # Verify owner of automatically created routing instance is cloud-admin
        ri_name = [self.domain_name, alice.project, self.vn_name, self.vn_name]
        ri = vnc_read_obj(self.admin.vnc_lib, 'routing-instance', name = ri_name)
        self.assertEquals(ri.get_perms2().owner, 'cloud-admin')

    def test_default_ipam_perms(self):
        " test default-domain:default-project:default-network-ipam allows global read/linking by default"

        ipam_fq_name = ['default-domain', 'default-project', 'default-network-ipam']
        ipam = vnc_read_obj(self.alice.vnc_lib, 'network-ipam', name = ipam_fq_name)
        self.assertEquals(ipam.get_perms2().global_access, PERMS_RX)

    def test_global_read_only_role(self):
        vn = VirtualNetwork('alice-%s' % self.id(), self.alice.project_obj)
        vn_fq_name = vn.get_fq_name()

        # read-only role - create VN  ... should fail
        with ExpectedException(PermissionDenied) as e:
            self.adminr.vnc_lib.virtual_network_create(vn)

        # create VN owned by Alice
        self.alice.vnc_lib.virtual_network_create(vn)

        # read-only role - delete VN  ... should fail
        with ExpectedException(PermissionDenied) as e:
            self.adminr.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        # read-only role - read VN  ... should succeed
        try:
            vn = vnc_read_obj(self.adminr.vnc_lib, 'virtual-network', name = vn_fq_name)
            self.assertTrue(True, 'Read VN successfully ... test passed')
        except PermissionDenied as e:
            self.assertTrue(False, 'Read VN failed ... test failed!!!')

        # cloud admin - delete VN  ... should succeed
        try:
            self.admin.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)
            self.assertTrue(True, 'Deleted VN successfully ... test passed')
        except PermissionDenied as e:
            self.assertTrue(False, 'Delete VN failed ... test failed!!!')
    # end

    def test_obj_view(self):
        alice = self.alice
        admin = self.admin
        proj_obj = alice.project_obj

        ipam1_obj = NetworkIpam('ipam-1-%s' %(self.id()), parent_obj=proj_obj)
        alice.vnc_lib.network_ipam_create(ipam1_obj)
        vn_obj = VirtualNetwork('vn-1-%s' %(self.id()), parent_obj=proj_obj)
        vn_obj.add_network_ipam(ipam1_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        alice.vnc_lib.virtual_network_create(vn_obj)

        # set to different user
        ipam2_obj = NetworkIpam('ipam-2-%s' %(self.id()), parent_obj=proj_obj)
        admin.vnc_lib.network_ipam_create(ipam2_obj)

        """
        vn_obj.add_network_ipam(ipam2_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('2.2.2.0', 28))]))
        alice.vnc_lib.virtual_network_update(vn_obj)

        # assert refs
        read_vn_obj = alice.vnc_lib.virtual_network_read(id=vn_obj.uuid)
        assert ipam2_obj.uuid not in read_vn_obj.network_ipam_refs
        assert ipam_obj.uuid in read_proj_obj.network_ipam_refs
        """

        vmi_obj = VirtualMachineInterface('vmi-1', parent_obj=proj_obj)
        vmi_obj.add_virtual_network(vn_obj)
        alice.vnc_lib.virtual_machine_interface_create(vmi_obj)

        # instance-ip test
        iip1_obj = InstanceIp('iip-1-%s' %(self.id()))
        iip1_obj.add_virtual_network(vn_obj)
        iip1_obj.add_virtual_machine_interface(vmi_obj)
        alice.vnc_lib.instance_ip_create(iip1_obj)

        # set to different user
        iip2_obj = InstanceIp('iip-2-%s' %(self.id()))
        iip2_obj.add_virtual_network(vn_obj)
        iip2_obj.add_virtual_machine_interface(vmi_obj)
        admin.vnc_lib.instance_ip_create(iip2_obj)

        # assert backrefs
        read_vmi_obj = alice.vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid,
                        fields=['instance_ip_back_refs'])
        iip_back_refs = [iip['to'] for iip in read_vmi_obj.get_instance_ip_back_refs()]
        self.assertTrue(iip1_obj.get_fq_name() in iip_back_refs)
        self.assertTrue(iip2_obj.get_fq_name() not in iip_back_refs)

        # assert child
        read_proj_obj = alice.vnc_lib.project_read(id=proj_obj.uuid, fields=['network_ipams'])
        net_ipams_seen = [ipam['uuid'] for ipam in read_proj_obj.network_ipams]
        self.assertTrue(ipam1_obj.uuid in net_ipams_seen)
        self.assertTrue(ipam2_obj.uuid not in net_ipams_seen)

        # ensure admin isn't impacted
        # backref
        read_vmi_obj = admin.vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid,
                        fields=['instance_ip_back_refs'])
        iip_back_refs = [iip['to'] for iip in read_vmi_obj.get_instance_ip_back_refs()]
        self.assertTrue(iip1_obj.get_fq_name() in iip_back_refs)
        self.assertTrue(iip2_obj.get_fq_name() in iip_back_refs)

        # child
        read_proj_obj = admin.vnc_lib.project_read(id=proj_obj.uuid, fields=['network_ipams'])
        net_ipams_seen = [ipam['uuid'] for ipam in read_proj_obj.network_ipams]
        self.assertTrue(ipam1_obj.uuid in net_ipams_seen)
        self.assertTrue(ipam2_obj.uuid in net_ipams_seen)

    def tearDown(self):
        super(TestPermissions, self).tearDown()
    # end tearDown
