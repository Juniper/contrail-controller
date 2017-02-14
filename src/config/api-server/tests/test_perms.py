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

import keystoneclient.v2_0.client as keystone
from keystonemiddleware import auth_token

from vnc_api.vnc_api import *
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_common
import test_case

from test_perms2 import User, set_perms, vnc_read_obj, vnc_aal_create, vnc_aal_add_rule

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

# This is needed for VncApi._authenticate invocation from within Api server.
# We don't have access to user information so we hard code admin credentials.
def ks_admin_authenticate(self, response=None, headers=None):
    rval = token_from_user_info('admin', 'admin', 'default-domain', 'cloud-admin')
    new_headers = {}
    new_headers['X-AUTH-TOKEN'] = rval
    return new_headers

class TestUserVisible(test_case.ApiServerTestCase):
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
        super(TestUserVisible, cls).setUpClass(extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestUserVisible, cls).tearDownClass(*args, **kwargs)

    def setUp(self):
        super(TestUserVisible, self).setUp()
        ip = self._api_server_ip
        port = self._api_server_port
        kc = keystone.Client(username='admin', password='contrail123',
                       tenant_name='admin',
                       auth_url='http://127.0.0.1:5000/v2.0')

        # prepare token before vnc api invokes keystone
        self.test = User(ip, port, kc, 'test', 'test123', 'test-role', 'admin-%s' % self.id())
        self.admin = User(ip, port, kc, 'admin', 'contrail123', 'cloud-admin', 'admin-%s' % self.id())

    def test_user_visible_perms(self):
        user = self.test
        project_obj = Project(user.project)
        project_obj.uuid = user.project_uuid
        self.admin.vnc_lib.project_create(project_obj)

        # read projects back
        user.project_obj = vnc_read_obj(self.admin.vnc_lib,
            'project', obj_uuid = user.project_uuid)
        user.domain_id = user.project_obj.parent_uuid
        user.vnc_lib.set_domain_id(user.project_obj.parent_uuid)

        logger.info( 'Change owner of project %s to %s' % (user.project, user.project_uuid))
        set_perms(user.project_obj, owner=user.project_uuid, share = [])
        self.admin.vnc_lib.project_update(user.project_obj)

        # allow permission to create all objects
        user.proj_rg = vnc_aal_create(self.admin.vnc_lib, user.project_obj)
        vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
            rule_str = '* %s:CRUD' % user.role)

        ipam_obj = NetworkIpam('ipam-%s' %(self.id()), user.project_obj)
        user.vnc_lib.network_ipam_create(ipam_obj)
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        kwargs = {'id_perms':{'user_visible': False}}
        vn = VirtualNetwork('vn-%s' %(self.id()), user.project_obj, **kwargs)
        vn.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))

        #create virtual-network by non-admin user should fail when user_visible -> 'false'
        with ExpectedException(BadRequest) as e:
            user.vnc_lib.virtual_network_create(vn)

        #create virtual-network by admin user
        self.admin.vnc_lib.virtual_network_create(vn)
        vn_fq_name = vn.get_fq_name()

        #delete virtual-network by non-admin user should fail when user_visible -> 'false'
        with ExpectedException(NoIdError) as e:
            user.vnc_lib.virtual_network_delete(fq_name = vn_fq_name)

        #update virtual-network by non-admin user should fail when user_visible -> 'false'
        vn.display_name = "test_perms"
        with ExpectedException(NoIdError) as e:
            user.vnc_lib.virtual_network_update(vn)
    #end test_user_visible_perms
# class TestPermissions
