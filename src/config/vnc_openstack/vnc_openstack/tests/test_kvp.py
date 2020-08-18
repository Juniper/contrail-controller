from __future__ import absolute_import

from . import test_case

from cfgm_common.tests import test_common

import vnc_openstack

from vnc_api.vnc_api import (
    Domain, IpamSubnetType, IpamType, NetworkIpam, Project,
    SubnetType, VirtualNetwork, vnc_api, VnSubnetsType)

from builtins import str
from builtins import object
import uuid
import logging

import gevent.event
import gevent.monkey

gevent.monkey.patch_all()

logger = logging.getLogger(__name__)


class NetworkKVPTest(test_case.ResourceDriverTestCase):
    def setUp(self):
        super(NetworkKVPTest, self).setUp()
        domain_name = 'my-domain-%s' % (self.id())
        proj_name = 'my-proj'
        domain = Domain(domain_name)
        self._vnc_lib.domain_create(domain)
        self._dom_obj = self._vnc_lib.domain_read(id=domain.uuid)

        project = Project(proj_name, domain)
        self._vnc_lib.project_create(project)
        self._proj_obj = self._vnc_lib.project_read(id=project.uuid)

        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        self._ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

    def tearDown(self):
        self._vnc_lib.network_ipam_delete(id=self._ipam_obj.uuid)
        self._vnc_lib.project_delete(id=self._proj_obj.uuid)
        self._vnc_lib.domain_delete(id=self._dom_obj.uuid)
        self._dom_obj = None
        self._proj_obj = None
        self._ipam_obj = None
        super(NetworkKVPTest, self).tearDown()

    def test_network_create(self):
        subnet = '192.168.1.0'
        prefix = 24
        subnet_1 = '10.1.1.0'
        vn_name = 'my-fe'
        vn = VirtualNetwork(vn_name, self._proj_obj)
        ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet, prefix))
        ipam_sn_2 = IpamSubnetType(subnet=SubnetType(subnet_1, prefix))
        vn.add_network_ipam(
            self._ipam_obj, VnSubnetsType([ipam_sn_1, ipam_sn_2]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)
        # Ensure entries present in KV
        kvp = self._vnc_lib.kv_retrieve()
        self.assertIn(
            '%s 192.168.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])
        self.assertIn('%s 10.1.1.0/24' % (vn.uuid), [p['value'] for p in kvp])

        # Ensure entry present in KV
        key = net_obj.uuid + " " + subnet + '/' + str(prefix)
        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[
            0].subnet_uuid
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        key = net_obj.uuid + " " + subnet_1 + '/' + str(prefix)
        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[
            1].subnet_uuid
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        # Test delete
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        # Ensure all entries are gone
        kvp = self._vnc_lib.kv_retrieve()
        self.assertNotIn(
            '%s 192.168.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])
        self.assertNotIn(
            '%s 10.1.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])

        # Test update
        vn_name = 'my-be'
        subnet_2 = '9.1.1.0'
        vn = VirtualNetwork(vn_name, self._proj_obj)
        ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet_2, prefix))
        vn.add_network_ipam(self._ipam_obj, VnSubnetsType([ipam_sn_1]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)

        # Ensure entry in KV
        kvp = self._vnc_lib.kv_retrieve()
        self.assertIn('%s 9.1.1.0/24' % (vn.uuid), [p['value'] for p in kvp])
        key = net_obj.uuid + " " + subnet_2 + '/' + str(prefix)
        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[
            0].subnet_uuid
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        subnet_3 = '8.1.1.0'
        ipam_sn_2 = IpamSubnetType(subnet=SubnetType(subnet_3, prefix))
        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                ipam_ref['attr'].set_ipam_subnets([ipam_sn_2])
                net_obj._pending_field_updates.add('network_ipam_refs')
        self._vnc_lib.virtual_network_update(net_obj)
        # Ensure entry in KV
        kvp = self._vnc_lib.kv_retrieve()
        self.assertIn('%s 8.1.1.0/24' % (vn.uuid), [p['value'] for p in kvp])
        key = net_obj.uuid + " " + subnet_3 + '/' + str(prefix)
        net_obj = self._vnc_lib.virtual_network_read(id=net_obj.uuid)
        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[
            0].subnet_uuid
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        # Test delete
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        kvp = self._vnc_lib.kv_retrieve()
        self.assertNotIn(
            '%s 9.1.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])
        self.assertNotIn(
            '%s 8.1.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])

        # Test ref_update
        vn_name = 'my-ref'
        vn = VirtualNetwork(vn_name, self._proj_obj)
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)

        subnet_3 = '99.1.1.0'
        ipam_sn_3 = IpamSubnetType(subnet=SubnetType(subnet_3, prefix))
        self._vnc_lib.ref_update(
            'virtual-network',
            net_obj.uuid,
            'network-ipam-refs',
            None,
            self._ipam_obj.get_fq_name(),
            'ADD',
            VnSubnetsType(
                [ipam_sn_3]))
        # Ensure entries in KV
        kvp = self._vnc_lib.kv_retrieve()
        self.assertIn('%s 99.1.1.0/24' % (vn.uuid), [p['value'] for p in kvp])
        key = net_obj.uuid + " " + subnet_3 + '/' + str(prefix)
        net_obj = self._vnc_lib.virtual_network_read(id=net_obj.uuid)
        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[
            0].subnet_uuid
        key_tmp = self._vnc_lib.kv_retrieve(subnet_uuid)
        self.assertEqual(key, key_tmp)

        self._vnc_lib.ref_update(
            'virtual-network',
            net_obj.uuid,
            'network-ipam-refs',
            None,
            self._ipam_obj.get_fq_name(),
            'DELETE')
        kvp = self._vnc_lib.kv_retrieve()
        self.assertNotIn(
            '%s 99.1.1.0/24' %
            (vn.uuid), [
                p['value'] for p in kvp])

        # Test delete
        self._vnc_lib.virtual_network_delete(id=vn.uuid)


# end class NetworkKVPTest


class DelayedApiServerConnectionTest(test_case.ResourceDriverTestCase):
    CONN_DELAY = 1

    def setUp(self):
        orig_get_api_connection = getattr(
            vnc_openstack.ResourceApiDriver, '_get_api_connection')
        orig_post_project_create = getattr(
            vnc_openstack.ResourceApiDriver, 'post_project_create')
        orig_pre_project_delete = getattr(
            vnc_openstack.ResourceApiDriver, 'pre_project_delete')
        orig_domain_project_sync = getattr(
            vnc_openstack.OpenstackDriver, 'resync_domains_projects')

        self.post_project_create_entered = gevent.event.Event()

        def delayed_get_api_connection(*args, **kwargs):
            self.post_project_create_entered.wait()
            # sleep so hook code is forced to wait
            gevent.sleep(self.CONN_DELAY)

            # Fake errors in connection upto RETRIES_BEFORE_LOG
            orig_retries_before_log = vnc_openstack.RETRIES_BEFORE_LOG
            vnc_openstack.RETRIES_BEFORE_LOG = 2

            orig_vnc_api = vnc_api.VncApi
            try:
                err_instances = []

                class BoundedErrorVncApi(object):
                    def __init__(self, *args, **kwargs):
                        if len(
                                err_instances) < \
                                vnc_openstack.RETRIES_BEFORE_LOG:
                            err_instances.append(True)
                            raise Exception("Faking Api connection exception")
                        vnc_api.VncApi = orig_vnc_api
                        return orig_vnc_api(*args, **kwargs)

                vnc_api.VncApi = BoundedErrorVncApi

                return orig_get_api_connection(
                    self.resource_driver, *args, **kwargs)
            finally:
                setattr(
                    vnc_openstack,
                    'RETRIES_BEFORE_LOG',
                    orig_retries_before_log)
                vnc_api.VncApi = orig_vnc_api

        def delayed_post_project_create(*args, **kwargs):
            self.post_project_create_entered.set()
            return orig_post_project_create(
                self.resource_driver, *args, **kwargs)

        def stub(*args, **kwargs):
            pass

        test_common.setup_extra_flexmock([
            (vnc_openstack.ResourceApiDriver, '_get_api_connection',
             delayed_get_api_connection),
            (vnc_openstack.ResourceApiDriver, 'post_project_create',
             delayed_post_project_create),
            (vnc_openstack.ResourceApiDriver, 'pre_project_delete',
             None),
            (vnc_openstack.OpenstackDriver, 'resync_domains_projects',
             stub), ])
        super(DelayedApiServerConnectionTest, self).setUp()

        def unset_mocks():
            setattr(vnc_openstack.ResourceApiDriver, '_get_api_connection',
                    orig_get_api_connection)
            setattr(vnc_openstack.ResourceApiDriver, 'post_project_create',
                    orig_post_project_create)
            setattr(vnc_openstack.ResourceApiDriver, 'pre_project_delete',
                    orig_pre_project_delete)
            setattr(vnc_openstack.OpenstackDriver, 'resync_domains_projects',
                    orig_domain_project_sync)

        self.addCleanup(unset_mocks)
    # end setUp
    # end tearDown

    def test_post_project_create_default_sg(self):
        proj_id = str(uuid.uuid4())
        proj_name = self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        sg_obj = self._vnc_lib.security_group_read(
            fq_name=proj_obj.fq_name + ['default'])
        self._vnc_lib.security_group_delete(id=sg_obj.uuid)
    # end test_post_project_create_default_sg
# end class DelayedApiServerConnectionTest
