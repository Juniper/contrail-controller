#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
from builtins import range
import json
import logging

from cfgm_common.tests.test_utils import FakeKeystoneClient
from cfgm_common.tests.test_utils import FakeAuthProtocol
from cfgm_common.rbaclib import build_rule
from keystoneclient.v2_0.client import Client as kclient
from keystonemiddleware import auth_token
from vnc_api.exceptions import BadRequest
from vnc_api.vnc_api import AllowedAddressPair
from vnc_api.vnc_api import AllowedAddressPairs
from vnc_api.vnc_api import InstanceIp
from vnc_api.vnc_api import NetworkIpam
from vnc_api.vnc_api import IpamSubnetType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import VnSubnetsType
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VncApi

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


def get_token(user_name, project_name, domain_name, role_name, project_id=None,
              domain_id=None):
    token_dict = {
        'X-User': user_name,
        'X-User-Name': user_name,
        'X-Project-Name': project_name,
        'X-Project-Id': (project_id or '').replace('-', ''),
        'X-Domain-Id': (domain_id or '').replace('-', ''),
        'X-Domain-Name': domain_name,
        'X-Role': role_name,
    }
    rval = json.dumps(token_dict)
    return rval


class MyVncApi(VncApi):
    def __init__(self, username=None, password=None, project_name=None,
                 project_id=None, user_role=None, api_server_host=None,
                 api_server_port=None):
        self.username = username
        self.project_name = project_name
        self.project_id = project_id
        self.user_role = user_role
        self.domain_id = None
        VncApi.__init__(
            self, username=username,
            password=password,
            tenant_name=project_name,
            api_server_host=api_server_host,
            api_server_port=api_server_port)

    def set_domain_id(self, domain_id):
        self.domain_id = domain_id

    def _authenticate(self, response=None, headers=None):
        rval = get_token(
            self.username,
            self.project_name,
            'default-domain',
            self.user_role,
            self.project_id,
            self.domain_id)
        new_headers = headers or {}
        new_headers['X-AUTH-TOKEN'] = rval
        self._auth_token = rval
        return new_headers


def ks_admin_authenticate(self, response=None, headers=None):
    if not headers:
        headers = {}
    headers['X-AUTH-TOKEN'] = get_token(
        'admin', 'admin', 'default-domain', 'cloud-admin')
    return headers


class TestVncCfgApiServerBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncCfgApiServerBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncCfgApiServerBase, cls).tearDownClass(
            *args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestVncCfgApiServer(TestVncCfgApiServerBase):
    def test_subnet_type_validation(self):
        test_suite = [
            (SubnetType('0', 0), ('0.0.0.0', 0)),
            (SubnetType('1.1.1.1'), BadRequest),
            (SubnetType('1.1.1.1', 24), ('1.1.1.0', 24)),
            (SubnetType('1.1.1.1', '24'), ('1.1.1.0', 24)),
            (SubnetType('1.1.1.1', 32), ('1.1.1.1', 32)),
            (SubnetType('1.1.1.e', 32), BadRequest),
            (SubnetType('1.1.1.1', '32e'), BadRequest),
            (SubnetType('1.1.1.0,2.2.2.0', 24), BadRequest),
            (SubnetType(''), BadRequest),
            (SubnetType('', 30), BadRequest),
            (SubnetType('::', 0), ('::', 0)),
            (SubnetType('::'), BadRequest),
            (SubnetType('dead::beef', 128), ('dead::beef', 128)),
            (SubnetType('dead::beef', '128'), ('dead::beef', 128)),
            (SubnetType('dead::beef', 96), ('dead::', 96)),
            (SubnetType('dead::beez', 96), BadRequest),
            (SubnetType('dead::beef', '96e'), BadRequest),
            (SubnetType('dead::,beef::', 64), BadRequest),
        ]

        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        vmi = VirtualMachineInterface('vmi-%s' % self.id(), parent_obj=project)
        vmi.set_virtual_network(vn)
        self.api.virtual_machine_interface_create(vmi)

        for subnet, expected_result in test_suite:
            aaps = AllowedAddressPairs(
                allowed_address_pair=[AllowedAddressPair(ip=subnet)])
            vmi.set_virtual_machine_interface_allowed_address_pairs(aaps)
            if (type(expected_result) == type and
                    issubclass(expected_result, Exception)):
                self.assertRaises(
                    expected_result,
                    self.api.virtual_machine_interface_update,
                    vmi)
            else:
                self.api.virtual_machine_interface_update(vmi)
                vmi = self.api.virtual_machine_interface_read(id=vmi.uuid)
                returned_apps = vmi.\
                    get_virtual_machine_interface_allowed_address_pairs().\
                    get_allowed_address_pair()
                self.assertEqual(len(returned_apps), 1)
                returned_subnet = returned_apps[0].get_ip()
                self.assertEqual(returned_subnet.ip_prefix,
                                 expected_result[0])
                self.assertEqual(returned_subnet.ip_prefix_len,
                                 expected_result[1])


class TestVncCfgApiServerWithRbac(TestVncCfgApiServerBase):
    @classmethod
    def setUpClass(cls):
        extra_mocks = [
            (kclient, '__new__', FakeKeystoneClient),
            (VncApi, '_authenticate', ks_admin_authenticate),
            (auth_token, 'AuthProtocol', FakeAuthProtocol),
        ]
        extra_config_knobs = [
            ('DEFAULTS', 'aaa_mode', 'rbac'),
            ('DEFAULTS', 'cloud_admin_role', 'cloud-admin'),
            ('DEFAULTS', 'auth', 'keystone'),
        ]
        super(TestVncCfgApiServerWithRbac, cls).setUpClass(
            extra_mocks=extra_mocks,
            extra_config_knobs=extra_config_knobs)

    def setUp(self):
        super(TestVncCfgApiServerWithRbac, self).setUp()
        self.keystone = kclient(
            username='admin',
            password='password',
            tenant_name='admin',
            auth_url='http://127.0.0.1:5000/v2.0')

        self.admin_api = self._get_api_client(
            'admin-%s' % self.id(),
            'password',
            'admin-project-%s' % self.id(),
            'cloud-admin',
            create_project=False)
        p = Project(self.admin_api.project_name)
        p.uuid = self.admin_api.project_id
        self.admin_api.project_create(p)

        global_aal = self.admin_api.api_access_list_read(
            ['default-global-system-config', 'default-api-access-list'])
        global_aal_entries = global_aal.get_api_access_list_entries()
        global_aal_entries.add_rbac_rule(build_rule('* member:CRUD'))
        global_aal.set_api_access_list_entries(global_aal_entries)
        self.admin_api.api_access_list_update(global_aal)

    def _get_api_client(self, username, password, project_name, role_name,
                        create_project=True):
        project = self.keystone.tenants.get(name=project_name)
        if not project:
            project = self.keystone.tenants.create(project_name)
        user = self.keystone.users.get(username) or self.keystone.users.create(
            username, password, '', project.id)
        role = self.keystone.roles.get(role_name)
        if not role:
            role = self.keystone.roles.create(role_name)
        self.keystone.roles.add_user_role(user, role, project)

        if create_project:
            p = Project(project.name)
            p.uuid = project.id
            self.admin_api.project_create(p)

        return MyVncApi(
            username=username,
            password=password,
            project_name=project.name,
            project_id=project.id,
            user_role=role_name,
            api_server_host=self._api_server_ip,
            api_server_port=self._api_server_port)

    def test_rbac_on_back_ref(self):
        admin_iip_count = 10
        iip_uuids = set()
        user_api = self._get_api_client(
            'user-%s' % self.id(),
            'password',
            'project-%s' % self.id(),
            'member')
        user_project = self.admin_api.project_read(id=user_api.project_id)

        user_vn = VirtualNetwork(
            'user-vn-%s' % self.id(), parent_obj=user_project)
        user_ni = NetworkIpam('ni-%s' % self.id(), parent_obj=user_project)
        user_api.network_ipam_create(user_ni)
        user_vn.add_network_ipam(
            user_ni,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        user_api.virtual_network_create(user_vn)
        user_vmi_view = VirtualMachineInterface(
            'user-vmi-%s' % self.id(), parent_obj=user_project)
        user_vmi_view.add_virtual_network(user_vn)
        user_api.virtual_machine_interface_create(user_vmi_view)

        user_iip = InstanceIp('user-iip-%s' % self.id())
        user_iip.add_virtual_network(user_vn)
        user_iip.add_virtual_machine_interface(user_vmi_view)
        user_api.instance_ip_create(user_iip)
        iip_uuids.add(user_iip.uuid)

        for i in range(admin_iip_count):
            admin_iip = InstanceIp('admin-iip-%d-%s' % (i, self.id()))
            admin_iip.add_virtual_network(user_vn)
            admin_iip.add_virtual_machine_interface(user_vmi_view)
            self.admin_api.instance_ip_create(admin_iip)
            iip_uuids.add(admin_iip.uuid)

        user_iips = user_vmi_view.get_instance_ip_back_refs()
        self.assertEqual(len(user_iips), 1)
        self.assertEqual(user_iips[0]['uuid'], user_iip.uuid)
        admin_vmi_view = self.admin_api.virtual_machine_interface_read(
            id=user_vmi_view.uuid)
        admin_iips = admin_vmi_view.get_instance_ip_back_refs()
        self.assertEqual(len(admin_iips), admin_iip_count + 1)
        self.assertEqual({iip['uuid'] for iip in admin_iips}, iip_uuids)
