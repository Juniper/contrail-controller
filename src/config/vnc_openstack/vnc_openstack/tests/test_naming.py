from __future__ import absolute_import

import json
import logging
import uuid
from builtins import range
from builtins import str

import gevent
from cfgm_common.tests import test_common
from cfgm_common.tests import test_utils
from gevent import monkey
from pysandesh.connection_info import ConnectionState
from testtools import ExpectedException
from testtools.matchers import Contains, Equals, Not
from vnc_api import vnc_api

from . import test_case

monkey.patch_all()

logger = logging.getLogger(__name__)


class NBTestNaming(test_case.NeutronBackendTestCase):

    def _create_subnet(self, proj_id, extra_res_fields=None):
        if extra_res_fields is None:
            extra_res_fields = {}
        net_q = self.create_resource('network', proj_id)
        extra_res_fields.update(
            {'network_id': net_q['id'], 'cidr': '1.0.0.0/24', 'ip_version': 4})
        return self.create_resource(
            'subnet', proj_id, extra_res_fields=extra_res_fields)

    def _create_port(self, proj_id, extra_res_fields):
        subnet_q = self._create_subnet(proj_id, )
        extra_res_fields.update({'network_id': subnet_q['network_id']})
        return self.create_resource(
            'port', proj_id, extra_res_fields=extra_res_fields)

    def _create_resource_with_fallback(
            self, res_type, proj_id, extra_res_fields=None):
        def default_create_resource_func(pid, extra):
            return self.create_resource(res_type, pid, extra_res_fields=extra)

        return getattr(
            self,
            '_create_' + res_type,
            default_create_resource_func)(
            proj_id,
            extra_res_fields)

    def _change_resource_name(self, res_type, res_q):
        new_res_name = 'new-%s-%s' % (res_type, str(uuid.uuid4()))
        context = {'operation': 'UPDATE',
                   'user_id': '',
                   'roles': ''}
        data = {'resource': {'name': new_res_name},
                'id': res_q['id']}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' % (res_type), body)
        res_q = json.loads(resp.text)
        return new_res_name, res_q
    # end _change_resource_name

    def test_name_change(self):
        proj_obj = vnc_api.Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj_obj)
        for res_type in [
                'network',
                'subnet',
                'security_group',
                'port',
                'router']:
            # create a resource
            res_name = '%s-%s' % (res_type, self.id())
            res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={'name': res_name})
            self.assertThat(res_q['name'], Equals(res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['fq_name'], Contains(res_name))

            # change its name
            # new_res_name, res_q = self._change_resource_name(res_type, res_q)
            new_res_name = 'new-%s' % res_name
            res_q = self.update_resource(
                res_type,
                res_q['id'],
                proj_obj.uuid,
                extra_res_fields={
                    'name': new_res_name})
            self.assertThat(res_q['name'], Equals(new_res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['fq_name'], Contains(res_name))
                self.assertThat(res_q['fq_name'], Not(Contains(new_res_name)))

            # list by filter of new name
            res_list = self.list_resource(
                res_type, proj_uuid=proj_obj.uuid, req_filters={
                    'name': new_res_name})
            self.assertThat(len(res_list), Equals(1))
            self.assertThat(res_list[0]['name'], Equals(new_res_name))
    # end test_name_change

    def test_duplicate_name(self):
        proj_obj = vnc_api.Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj_obj)
        for res_type in [
                'network',
                'subnet',
                'security_group',
                'port',
                'router']:
            # create a resource
            res_name = '%s-%s' % (res_type, self.id())
            res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={'name': res_name})
            self.assertThat(res_q['name'], Equals(res_name))

            # create another resource
            res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={'name': res_name})
            self.assertThat(res_q['name'], Equals(res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['fq_name'][-1],
                                Not(Equals(res_q['name'])))

            # list by filter of new name
            res_list = self.list_resource(
                res_type, proj_uuid=proj_obj.uuid, req_filters={
                    'name': res_name})
            self.assertThat(len(res_list), Equals(2))
    # end test_duplicate_name

    def test_uuid(self):
        proj_obj = vnc_api.Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj_obj)
        for res_type in [
                'network',
                'subnet',
                'security_group',
                'port',
                'router']:
            res_uuid = str(uuid.uuid4())
            res_name = '%s-%s' % (res_type, res_uuid)
            res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={
                    'name': res_name, 'id': res_uuid})
            self.assertThat(res_q['id'], Equals(res_uuid))
    # end test_uuid

    def test_uuid_in_duplicate_name(self):
        proj_obj = vnc_api.Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj_obj)
        for res_type in [
                'network',
                'subnet',
                'security_group',
                'port',
                'router']:
            # create a resource
            res_uuid = str(uuid.uuid4())
            res_name = '%s-%s' % (res_type, res_uuid)
            res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={
                    'name': res_name, 'id': res_uuid})
            self.assertThat(res_q['name'], Equals(res_name))
            self.assertThat(res_q['id'], Equals(res_uuid))

            # create another resource
            another_res_uuid = str(uuid.uuid4())
            another_res_q = self._create_resource_with_fallback(
                res_type, proj_obj.uuid, extra_res_fields={
                    'name': res_name, 'id': another_res_uuid})

            if res_type != 'subnet':
                expected_fq_name = "%s-%s" % (res_name, another_res_uuid)
                self.assertThat(
                    another_res_q['fq_name'][-1], Equals(expected_fq_name))
            self.assertThat(another_res_q['id'], Equals(another_res_uuid))

            res_list = self.list_resource(
                res_type, proj_uuid=proj_obj.uuid, req_filters={
                    'name': res_name})
            self.assertThat(len(res_list), Equals(2))
    # end test_uuid_in_duplicate_name
# end class NBTestNaming


class KeystoneSync(test_case.KeystoneSyncTestCase):
    def test_dup_project_new_unique_fqn(self):
        logger.info('Creating first project in "keystone"')
        proj_id = str(uuid.uuid4())
        proj_name = self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        self.assertThat(proj_obj.name, Equals(proj_name))
        # create a VN in it so old isn't removed (due to synchronous delete)
        # when new with same name is created
        vn_obj = vnc_api.VirtualNetwork('vn-%s' % (self.id()), proj_obj)
        self._vnc_lib.virtual_network_create(vn_obj)

        logger.info(
            'Creating second project with same name diff id in "keystone"')
        new_proj_id = str(uuid.uuid4())
        test_case.get_keystone_client().tenants.add_tenant(new_proj_id,
                                                           proj_name)
        new_proj_obj = self._vnc_lib.project_read(id=new_proj_id)
        self.assertThat(new_proj_obj.name, Not(Equals(proj_name)))
        self.assertThat(new_proj_obj.name, Contains(proj_name))
        self.assertThat(new_proj_obj.display_name, Equals(proj_name))

        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
        self._vnc_lib.project_delete(id=proj_id)
        self._vnc_lib.project_delete(id=new_proj_id)

    def test_dup_project_fails(self):
        logger.info('Creating first project in "keystone"')
        proj_id = str(uuid.uuid4())
        proj_name = self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        self.assertThat(proj_obj.name, Equals(proj_name))
        # create a VN in it so old isn't removed (due to synchronous delete)
        # when new with same name is created
        vn_obj = vnc_api.VirtualNetwork('vn-%s' % (self.id()), proj_obj)
        self._vnc_lib.virtual_network_create(vn_obj)

        stale_mode = self.openstack_driver._resync_stale_mode
        self.openstack_driver._resync_stale_mode = 'new_fails'
        try:
            logger.info(
                'Creating second project with same name diff id in "keystone"')
            new_proj_id = str(uuid.uuid4())
            test_case.get_keystone_client().tenants.add_tenant(new_proj_id,
                                                               proj_name)
            with ExpectedException(vnc_api.NoIdError):
                self._vnc_lib.project_read(id=new_proj_id)

            self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
            self._vnc_lib.project_delete(id=proj_id)
        finally:
            self.openstack_driver._resync_stale_mode = stale_mode

    def test_delete_synchronous_on_dup(self):
        logger.info('Creating project in "keystone" and syncing')
        proj_id1 = str(uuid.uuid4())
        proj_name = self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id1, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id1)
        self.assertThat(proj_obj.name, Equals(proj_name))

        logger.info('Deleting project in keystone and immediately re-creating')

        def stub(*args, **kwargs):
            return

        with test_common.patch(self.openstack_driver,
                               '_del_project_from_vnc', stub):
            test_case.get_keystone_client().tenants.delete_tenant(proj_id1)
            proj_id2 = str(uuid.uuid4())
            test_case.get_keystone_client().tenants.add_tenant(
                proj_id2, proj_name)
            proj_obj = self._vnc_lib.project_read(id=proj_id2)
            self.assertThat(proj_obj.uuid, Equals(proj_id2))
            with ExpectedException(vnc_api.NoIdError):
                self._vnc_lib.project_read(id=proj_id1)

        self._vnc_lib.project_delete(id=proj_id2)

    def test_dup_domain(self):
        orig_ks_domains_list = self.openstack_driver._ks_domains_list
        orig_ks_domain_get = self.openstack_driver._ks_domain_get
        try:
            self.openstack_driver._ks_domains_list = \
                self.openstack_driver._ksv3_domains_list
            self.openstack_driver._ks_domain_get = \
                self.openstack_driver._ksv3_domain_get
            logger.info('Creating first domain in "keystone"')
            dom_id = str(uuid.uuid4())
            dom_name = self.id()
            test_case.get_keystone_client().domains.add_domain(dom_id,
                                                               dom_name)
            dom_obj = self._vnc_lib.domain_read(id=dom_id)
            self.assertThat(dom_obj.name, Equals(dom_name))
            # create a project under domain so synch delete of domain fails
            proj_obj = vnc_api.Project('proj-%s' % (self.id()), dom_obj)
            self._vnc_lib.project_create(proj_obj)

            logger.info(
                'Creating second domain with same name diff id in "keystone"')
            new_dom_id = str(uuid.uuid4())
            test_case.get_keystone_client().domains.add_domain(new_dom_id,
                                                               dom_name)
            new_dom_obj = self._vnc_lib.domain_read(id=new_dom_id)
            self.assertThat(new_dom_obj.name, Not(Equals(dom_name)))
            self.assertThat(new_dom_obj.name, Contains(dom_name))
            self.assertThat(new_dom_obj.display_name, Equals(dom_name))

            self._vnc_lib.project_delete(id=proj_obj.uuid)
            self._vnc_lib.domain_delete(id=dom_id)
            self._vnc_lib.domain_delete(id=new_dom_id)
        finally:
            self.openstack_driver._ks_domains_list = orig_ks_domains_list
            self.openstack_driver._ks_domain_get = orig_ks_domain_get
# end class KeystoneSync


class KeystoneConnectionStatus(test_case.KeystoneSyncTestCase):
    resync_interval = 0.5

    @classmethod
    def setUpClass(cls):
        super(KeystoneConnectionStatus, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'keystone_resync_interval_secs',
                                 cls.resync_interval)])
    # end setUpClass

    def test_connection_status_change(self):
        # up->down->up transition check
        proj_id = str(uuid.uuid4())
        proj_name = self.id() + 'verify-active'
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        conn_info = [ConnectionState._connection_map[x]
                     for x in ConnectionState._connection_map if
                     x[1] == 'Keystone'][0]
        self.assertThat(conn_info.status.lower(), Equals('up'))

        fake_list_invoked = list()

        def fake_list(*args, **kwargs):
            fake_list_invoked.append(True)
            raise Exception("Fake Keystone Projects List exception")

        def verify_down():
            conn_info = [ConnectionState._connection_map[x]
                         for x in ConnectionState._connection_map
                         if x[1] == 'Keystone'][0]
            self.assertThat(conn_info.status.lower(), Equals('down'))

        with test_common.flexmocks(
                [(self.openstack_driver._ks.tenants, 'list', fake_list)]):
            # wait for tenants.list is invoked for 2*self.resync_interval max
            for x in range(10):
                if len(fake_list_invoked) >= 1:
                    break
                gevent.sleep(float(self.resync_interval) / 5.0)
            # check that tenants.list was called once
            self.assertThat(len(fake_list_invoked), Equals(1))
            # wait for 1/10 of self.resync_interval to let code reach
            # reset_connection in service
            gevent.sleep(float(self.resync_interval) / 10.0)
            # verify up->down
            verify_down()
            # should remain down
            gevent.sleep(float(self.resync_interval) * 1.05)
            verify_down()
            self.assertThat(len(fake_list_invoked), Equals(2))

        # sleep for a retry and verify down->up
        gevent.sleep(self.resync_interval)
        conn_info = [ConnectionState._connection_map[x]
                     for x in ConnectionState._connection_map if
                     x[1] == 'Keystone'][0]
        self.assertThat(conn_info.status.lower(), Equals('up'))
    # end test_connection_status_change
# end class KeystoneConnectionStatus


keystone_ready = False


def get_keystone_client(*args, **kwargs):
    if keystone_ready:
        return test_utils.get_keystone_client()
    raise Exception("keystone connection failed.")


class TestKeystoneConnection(test_case.KeystoneSyncTestCase):
    resync_interval = 0.5

    @classmethod
    def setUpClass(cls):
        keystone_ready = False
        from keystoneclient import client as keystone
        extra_mocks = [(keystone, 'Client', get_keystone_client)]
        super(TestKeystoneConnection, cls).setUpClass(
            extra_mocks=extra_mocks,
            extra_config_knobs=[('DEFAULTS', 'keystone_resync_interval_secs',
                                 cls.resync_interval)])
    # end setUpClass

    def test_connection_status(self):
        # check that connection was not obtained
        conn_info = [ConnectionState._connection_map[x]
                     for x in ConnectionState._connection_map if
                     x[1] == 'Keystone'][0]
        self.assertThat(conn_info.status.lower(), Equals('initializing'))

        # sleep and check again
        gevent.sleep(self.resync_interval)
        conn_info = [ConnectionState._connection_map[x]
                     for x in ConnectionState._connection_map if
                     x[1] == 'Keystone'][0]
        self.assertThat(conn_info.status.lower(), Equals('initializing'))

        # allow to create connection
        global keystone_ready
        keystone_ready = True
        # wait for connection set up
        gevent.sleep(float(self.resync_interval) * 1.5)
        conn_info = [ConnectionState._connection_map[x]
                     for x in ConnectionState._connection_map if
                     x[1] == 'Keystone'][0]
        self.assertThat(conn_info.status.lower(), Equals('up'))

        # check that driver works as required
        proj_id = str(uuid.uuid4())
        proj_name = self.id() + 'verify-active'
        get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
