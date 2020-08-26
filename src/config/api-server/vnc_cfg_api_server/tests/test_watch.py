from __future__ import absolute_import
#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import gevent
import logging
import requests
from testtools.matchers import Equals
import unittest
from flexmock import flexmock
import sseclient
from . import test_case

from vnc_api.vnc_api import *
from cfgm_common.tests import test_utils
from cfgm_common import vnc_cgitb
from vnc_cfg_api_server.event_dispatcher import EventDispatcher
import keystoneclient.v2_0.client as keystone
from keystonemiddleware import auth_token

from .test_perms2 import (
    User,
    set_perms,
    vnc_read_obj,
    vnc_aal_create,
    vnc_aal_add_rule,
    ks_admin_authenticate,
    vnc_aal_del_rule)

vnc_cgitb.enable(format='text')

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class CustomError(Exception):
    pass


class TestWatch(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestWatch, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestWatch, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestWatch, self).setUp()
        self.listen_ip = self._api_server_ip
        self.listen_port = self._api_server._args.listen_port
        self.url = 'http://%s:%s/watch' % (self.listen_ip, self.listen_port)
        self.mock = flexmock(EventDispatcher)
        self.stream_response = None
    # end setUp

    def test_subscribe_exception(self):
        param = {"resource_type": "virtual_network"}
        self.error = "value error occured"
        self.mock.should_receive('subscribe_client').and_raise(
            CustomError, self.error)

        response = requests.get(self.url, params=param, stream=True)
        self.response_error = "Client queue registration failed with exception %s" % (
            self.error)
        self.assertThat(response.status_code, Equals(500))
        self.assertThat(
            response.content.decode('utf-8'),
            Equals(
                self.response_error))
    # end test_subscribe_exception

    def test_valid_params(self):
        param = {"resource_type": "virtual_network,virtual_machine_interface"}
        self.error = "value error occured"
        init_sample = {
            "event": "init", "data": [{"type": "virtual_network"}], }
        self.mock.should_receive('subscribe_client').and_return().once()
        self.mock.should_receive('initialize').and_return(
            True, init_sample).twice()

        self.count = 0
        self.data = "[{'type': 'virtual_network'}]"

        def watch_client():
            self.stream_response = requests.get(
                self.url, params=param, stream=True)
            client = sseclient.SSEClient(self.stream_response)
            for event in client.events():
                if (event.event == 'init'):
                    self.count += 1
                    self.assertThat(event.data, Equals(self.data))

        gevent.spawn(watch_client)
        gevent.sleep(0.1)
        self.assertThat(self.stream_response.status_code, Equals(200))
        self.assertEqual(self.count, 2)
    # end test_valid_params

    def test_invalid_request(self):
        response = requests.get(self.url, stream=True)
        self.assertEqual(response.status_code, 400)
        self.assertThat(response.content.decode('utf-8'), Equals(
            'resource_type required in request'))
    # end test_invalid_request

    def test_invalid_resource(self):
        param = {
            "resource_type":
                "virtual_network,virtual_machine_i"}

        response = requests.get(self.url, stream=True, params=param)
        self.assertEqual(response.status_code, 404)
    # end test_invalid_resource
# end TestWatch


class TestWatchIntegration(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestWatchIntegration, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestWatchIntegration, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestWatchIntegration, self).setUp()
        self.listen_ip = self._api_server_ip
        self.listen_port = self._api_server._args.listen_port
        self.url = 'http://%s:%s/watch' % (self.listen_ip, self.listen_port)
        self.stream_response = None
    # end setUp

    @unittest.skip("Flaky test")
    def test_watch(self):
        param = {
            "resource_type":
                "virtual_network,virtual_machine_interface,routing_instance"}
        expected_event_list = [
            "init",
            "init",
            "init",
            "create",
            "create",
            "create",
            "create",
            "update"]
        self.event_index = 0

        def watch_client():
            self.stream_response = requests.get(
                self.url, params=param, stream=True)
            client = sseclient.SSEClient(self.stream_response)
            for event in client.events():
                logger.info('%s: %s' % (event.event, event.data))
                if self.event_index < len(expected_event_list):
                    self.assertThat(event.event, Equals(
                        expected_event_list[self.event_index]))
                    self.event_index += 1
            return

        try:
            greenlet = gevent.spawn(watch_client)
            num_vn = 1
            vn_objs, ri_objs, vmi_objs, x = self._create_vn_ri_vmi(num_vn)
            gevent.sleep(0)
            greenlet.get(timeout=5)
        except gevent.timeout.Timeout as e:
            logger.info("Request failed")
            self.assertFalse(False, greenlet.successful())
    # end test_watch
# end TestWatchIntegration


class TestWatchPermission(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_mocks = [(keystone.Client,
                        '__new__', test_utils.FakeKeystoneClient),
                       (vnc_api.vnc_api.VncApi,
                        '_authenticate', ks_admin_authenticate),
                       (auth_token, 'AuthProtocol',
                        test_utils.FakeAuthProtocol)]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'global_read_only_role', 'read-only-role'),
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        super(
            TestWatchPermission, cls).setUpClass(
            extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestWatchPermission, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestWatchPermission, self).setUp()
        self.ip = self._api_server_ip
        self.port = self._api_server._args.listen_port
        self.url = 'http://%s:%s/watch' % (self.ip, self.port)
        self.kc = keystone.Client(username='admin', password='contrail123',
                                  tenant_name='admin',
                                  auth_url=self.url)
    # end setUp

    def test_rbac_cloud_admin_role(self):
        self.admin = User(self.ip, self.port, self.kc, 'admin', 'contrail123',
                          'cloud-admin', 'admin-%s' % self.id())
        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.admin.vnc_lib.get_auth_token()}
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 200)
    # end test_rbac_cloud_admin_role

    def test_rbac_read_only_role(self):
        self.adminr = User(self.ip, self.port, self.kc, 'adminr',
                           'contrail123', 'read-only-role', 'adminr-%s' %
                           self.id())
        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.adminr.vnc_lib.get_auth_token()}
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 200)
    # end test_rbac_read_only_role

    def test_rbac_admin_role(self):
        self.admin1 = User(self.ip, self.port, self.kc, 'admin1',
                           'contrail123', 'admin', 'admin1-%s' % self.id())
        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.admin1.vnc_lib.get_auth_token()}
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 403)
    # end test_rbac_admin_role

    def test_rbac_member_role(self):
        self.admin2 = User(self.ip, self.port, self.kc, 'admin2',
                           'contrail123', 'member', 'admin2-%s' % self.id())
        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.admin2.vnc_lib.get_auth_token()}
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 403)
    # end test_rbac_member_role

    def test_rbac_user_role(self):
        self.alice = User(self.ip, self.port, self.kc, 'alice', 'alice123',
                          'alice-role', 'alice-proj-%s' % self.id())
        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.alice.vnc_lib.get_auth_token()}
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 403)
    # end test_rbac_user_role

    def test_rbac_user_role_with_resource_read_access(self):
        self.admin = User(self.ip, self.port, self.kc, 'admin', 'contrail123',
                          'cloud-admin', 'admin-%s' % self.id())
        self.alice = User(self.ip, self.port, self.kc, 'alice', 'alice123',
                          'alice-role', 'alice-proj-%s' % self.id())
        user = self.alice
        project_obj = Project(user.project)
        project_obj.uuid = user.project_uuid
        self.admin.vnc_lib.project_create(project_obj)

        # read projects back
        user.project_obj = vnc_read_obj(self.admin.vnc_lib,
                                        'project', obj_uuid=user.project_uuid)
        user.domain_id = user.project_obj.parent_uuid
        user.vnc_lib.set_domain_id(user.project_obj.parent_uuid)

        logger.info(
            'Change owner of project %s to %s' %
            (user.project, user.project_uuid))
        set_perms(user.project_obj, owner=user.project_uuid, share=[])
        self.admin.vnc_lib.project_update(user.project_obj)

        user.proj_rg = vnc_aal_create(
            self.admin.vnc_lib, self.alice.project_obj)
        vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                         rule_str='virtual-network %s:CR' % user.role)

        logger.info('')
        logger.info('alice: trying to create VN in her project')
        self.vn_name = "alice-vn-%s" % self.id()
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            logger.info('Created virtual network: %s' % vn.get_fq_name())
            testfail = False
        except PermissionDenied as e:
            logger.info('Failed to create VN')
            testfail = True
        self.assertThat(testfail, Equals(False))

        param = {"resource_type": "virtual_network"}
        headers = {'X-Auth-Token': self.alice.vnc_lib.get_auth_token()}
        logger.info("alice has been granted read permission for the resource")
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 200)

        vnc_aal_del_rule(self.admin.vnc_lib, self.alice.proj_rg,
                         rule_str='virtual-network %s:R' % self.alice.role)
        logger.info("alice's read permission for the resource revoked")
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 403)
    # end test_rbac_user_role_with_resource_read_access

    def test_rbac_user_role_with_multiple_resources(self):
        self.admin = User(self.ip, self.port, self.kc, 'admin', 'contrail123',
                          'cloud-admin', 'admin-%s' % self.id())
        self.alice = User(self.ip, self.port, self.kc, 'alice', 'alice123',
                          'alice-role', 'alice-proj-%s' % self.id())
        user = self.alice
        project_obj = Project(user.project)
        project_obj.uuid = user.project_uuid
        self.admin.vnc_lib.project_create(project_obj)

        # read projects back
        user.project_obj = vnc_read_obj(self.admin.vnc_lib,
                                        'project', obj_uuid=user.project_uuid)
        user.domain_id = user.project_obj.parent_uuid
        user.vnc_lib.set_domain_id(user.project_obj.parent_uuid)

        logger.info(
            'Change owner of project %s to %s' %
            (user.project, user.project_uuid))
        set_perms(user.project_obj, owner=user.project_uuid, share=[])
        self.admin.vnc_lib.project_update(user.project_obj)

        user.proj_rg = vnc_aal_create(
            self.admin.vnc_lib, self.alice.project_obj)
        vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                         rule_str='virtual-network %s:CR' % user.role)

        self.vn_name = "alice-vn-%s" % self.id()
        vn = VirtualNetwork(self.vn_name, self.alice.project_obj)
        try:
            self.alice.vnc_lib.virtual_network_create(vn)
            logger.info('Created virtual network %s' % vn.get_fq_name())
            testfail = False
        except PermissionDenied as e:
            logger.info('Failed to create VN')
            testfail = True
        self.assertThat(testfail, Equals(False))

        param = {"resource_type": "virtual_network,virtual_machine"}
        headers = {'X-Auth-Token': self.alice.vnc_lib.get_auth_token()}
        logger.info("alice has read permission for only one resource")
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 403)

        vnc_aal_add_rule(self.admin.vnc_lib, user.proj_rg,
                         rule_str='virtual-machine %s:R' % user.role)
        logger.info("alice has read permission for both resources")
        response = requests.get(
            self.url,
            params=param,
            stream=True,
            headers=headers)
        self.assertEqual(response.status_code, 200)
    # test_rbac_user_role_with_multiple_resources
# end TestWatchPermission


if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
