from __future__ import absolute_import
import sys
import uuid
import json
import ConfigParser

import bottle
from flexmock import flexmock, Mock
from testtools.matchers import Equals, Contains, Not
import stevedore.extension

from vnc_cfg_api_server import api_server
from cfgm_common.tests.test_utils import FakeExtensionManager
from cfgm_common.tests.test_utils import get_keystone_client
from cfgm_common.tests.test_common import TestCase, setup_extra_flexmock

from . import fake_neutron
import vnc_openstack


@bottle.hook('after_request')
def after_request():
    bottle.response.headers['Content-Type'] = 'application/json; charset="UTF-8"'
    try:
        del bottle.response.headers['Content-Length']
    except KeyError:
        pass


class VncOpenstackTestCase(TestCase):
    _config_knobs = [
            ('DEFAULTS', '', ''),
            ('KEYSTONE', 'admin_user', ''),
            ('KEYSTONE', 'admin_password', ''),
            ('KEYSTONE', 'admin_tenant_name', ''),
            ('KEYSTONE', 'admin_token', ''),
            ('KEYSTONE', 'auth_host', ''),
            ('KEYSTONE', 'auth_port', ''),
            ('KEYSTONE', 'auth_protocol', 'http'),
            ('KEYSTONE', 'default_domain_id', 'default'),
    ]
    _entry_pt_to_classes = FakeExtensionManager._entry_pt_to_classes
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        # below code is needed due to different behaviour
        # of creating flexmock in setUp and in setup_extra_flexmock
        from keystoneclient import client as keystone
        mocks = kwargs.get('extra_mocks', list())
        for index in range(len(mocks)):
            mock = mocks[index]
            if mock[0] == keystone and mock[1] == 'Client':
                del mocks[index]
                kwargs['extra_mocks'] = mocks
                setup_extra_flexmock([mock])
                break
        else:
            setup_extra_flexmock([(keystone, 'Client', get_keystone_client)])
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resync'] = [vnc_openstack.OpenstackDriver]
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = [vnc_openstack.ResourceApiDriver]
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.neutronApi'] = [vnc_openstack.NeutronApiDriver]
        setup_extra_flexmock([(stevedore.extension.ExtensionManager, '__new__', FakeExtensionManager)])
        super(VncOpenstackTestCase, cls).setUpClass(*args, **kwargs)
    # end setUp

    @classmethod
    def tearDownClass(cls):
        try:
            with open('vnc_openstack.err') as f:
                cls.assertThat(len(f.read()), Equals(0),
                                "Error log in vnc_openstack.err")
        except IOError:
            # vnc_openstack.err not created, No errors.
            pass
        FakeExtensionManager._entry_pt_to_classes = cls._entry_pt_to_classes
        super(VncOpenstackTestCase, cls).tearDownClass()
# end class VncOpenstackTestCase


class NeutronBackendTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        super(NeutronBackendTestCase, cls).setUpClass(*args, **kwargs)
        cls.neutron_api_obj = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.neutronApi')[0]
        cls.neutron_api_obj._npi._connect_to_db()
        cls.neutron_db_obj = cls.neutron_api_obj._npi._cfgdb
    # end setUpClass

    def read_resource(self, url_pfx, id, fields=None, **kwargs):
        body = {
            'context': {
                'operation': 'READ',
                'user_id': '',
                'roles': '',
                'is_admin': True,
            },
            'data': {
                'fields': fields,
                'id': id,
            },
        }
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % url_pfx, body, **kwargs)
        return json.loads(resp.text or 'null')

    def list_resource(self, url_pfx, proj_uuid=None, req_fields=None,
                      req_filters=None, is_admin=False, **kwargs):
        if proj_uuid is None:
            proj_uuid = self._vnc_lib.fq_name_to_id(
                'project', fq_name=['default-domain', 'default-project'])

        body = {
            'context': {
                'operation': 'READALL',
                'user_id': '',
                'tenant_id': proj_uuid,
                'roles': '',
                'is_admin': is_admin,
            },
            'data': {
                'fields': req_fields,
                'filters': req_filters or {},
            },

        }
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % url_pfx, body, **kwargs)
        return json.loads(resp.text or 'null')

    def create_resource(self, res_type, proj_id, extra_res_fields=None,
                        is_admin=False, **kwargs):
        if not extra_res_fields:
            extra_res_fields = {'name': '%s-%s' % (res_type, self.id())}
        elif not 'name' in extra_res_fields:
            extra_res_fields['name'] = '%s-%s' % (res_type, self.id())
        extra_res_fields['tenant_id'] = proj_id

        body = {
            'context': {
                'operation': 'CREATE',
                'user_id': '',
                'is_admin': is_admin,
                'roles': '',
                'tenant_id': proj_id,
            },
            'data': {
                'resource': extra_res_fields,
            },
        }
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % res_type, body, **kwargs)
        return json.loads(resp.text or 'null')

    def delete_resource(self, res_type, proj_id, id, is_admin=False, **kwargs):
        body = {
            'context': {
                'operation': 'DELETE',
                'user_id': '',
                'is_admin': is_admin,
                'roles': '',
                'tenant_id': proj_id,
            },
            'data': {
                'id': id,
            },
        }
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % res_type, body,**kwargs)
        return json.loads(resp.text or 'null')

    def update_resource(self, res_type, res_id, proj_id, extra_res_fields=None,
                        is_admin=False, operation=None, **kwargs):
        extra_res_fields['tenant_id'] = proj_id
        body = {
            'context': {
                'operation': operation or 'UPDATE',
                'user_id': '',
                'is_admin': is_admin,
                'roles': '',
                'tenant_id': proj_id,
            },
            'data': {
                'resource': extra_res_fields or {},
                'id': res_id,
            },
        }
        resp = self._api_svr_app.post_json(
            '/neutron/%s' % res_type, body, **kwargs)
        return json.loads(resp.text or 'null')

    def add_router_interface(self, router_id, proj_id, is_admin=False,
                             extra_res_fields=None, **kwargs):
        return self.update_resource(
            'router',
            router_id,
            proj_id,
            is_admin=is_admin,
            extra_res_fields=extra_res_fields,
            operation='ADDINTERFACE',
            **kwargs)

    def del_router_interface(self, router_id, proj_id, is_admin=False,
                             **kwargs):
        return self.update_resource(
            'router',
            router_id,
            proj_id,
            is_admin=is_admin,
            operation='DELINTERFACE',
            **kwargs)
# end class NeutronBackendTestCase


class KeystoneSyncTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        super(KeystoneSyncTestCase, cls).setUpClass(*args, **kwargs)
        cls.openstack_driver = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.resync')[0]
    # end setUpClass
# end class KeystoneSyncTestCase


class ResourceDriverTestCase(VncOpenstackTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        super(ResourceDriverTestCase, cls).setUpClass(*args, **kwargs)
        cls.resource_driver = FakeExtensionManager.get_extension_objects(
            'vnc_cfg_api.resourceApi')[0]
    # end setUpClass
# end class ResourceDriverTestCase
