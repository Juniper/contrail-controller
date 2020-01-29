#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
from builtins import object
import logging

from cfgm_common.exceptions import BadRequest
import mock
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import SubCluster

from vnc_cfg_api_server.tests.test_case import ApiServerTestCase


logger = logging.getLogger(__name__)
ASN_2_BYTES = (1 << 16) - 42
ASN_4_BYTES = (1 << 16) + 42


class TestSubClusterBase(ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        super(TestSubClusterBase, cls).setUpClass(*args, **kwargs)

        cls._gsc = cls._vnc_lib.global_system_config_read(
            GlobalSystemConfig().fq_name)

        if hasattr(cls, '_global_asn') and cls._global_asn:
            cls._gsc.set_autonomous_system(cls._global_asn)
            if cls._global_asn > 0xFFFF:
                cls._gsc.enable_4byte_as = True
            cls._vnc_lib.global_system_config_update(cls._gsc)
        else:
            cls._global_asn = cls._gsc.get_autonomous_system()

        # Create a pool of 10 sub-cluster IDs to use for the tests
        start = 42
        if cls._global_asn <= 0xFFFF:
            start += 1 << 16
        cls._id_range = set(range(start, start + 10))

    @property
    def api(self):
        return self._vnc_lib


class SubClusterTestSuite(object):
    def test_cannot_update_asn(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)

        sc.set_sub_cluster_asn(43)
        self.assertRaises(BadRequest, self.api.sub_cluster_update, sc)

    def test_cluster_id_automatically_assigned(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)

        sc = self.api.sub_cluster_read(id=sc.uuid)
        self.assertIsNotNone(sc.get_sub_cluster_id())
        zk_db = self._api_server._db_conn._zk_db
        self.assertEqual(
            zk_db._get_sub_cluster_from_id(sc.get_sub_cluster_id()),
            sc.get_fq_name_str(),
        )

    def test_manually_allocate_sub_cluster_id(self):
        sub_cluster_id = self._id_range.pop()
        sc = SubCluster('sc-%s' % self.id(), sub_cluster_id=sub_cluster_id)
        self.api.sub_cluster_create(sc)

        sc = self.api.sub_cluster_read(id=sc.uuid)
        self.assertEqual(sc.get_sub_cluster_id(), sub_cluster_id)
        zk_db = self._api_server._db_conn._zk_db
        self.assertEqual(
            zk_db._get_sub_cluster_from_id(sub_cluster_id),
            sc.get_fq_name_str(),
        )

    def test_cannot_use_allocated_sub_cluster_id(self):
        # automatically allocated
        sc1 = SubCluster('sc1-%s' % self.id())
        self.api.sub_cluster_create(sc1)
        sc1 = self.api.sub_cluster_read(id=sc1.uuid)

        sc2 = SubCluster(
            'sc2-%s' % self.id(), sub_cluster_id=sc1.get_sub_cluster_id())
        self.assertRaises(BadRequest, self.api.sub_cluster_create, sc2)

        # or manually allocated
        sc3 = SubCluster(
            'sc3-%s' % self.id(), sub_cluster_id=self._id_range.pop())
        self.api.sub_cluster_create(sc3)

        sc4 = SubCluster(
            'sc4-%s' % self.id(),
            sub_cluster_id=sc3.get_sub_cluster_id())
        self.assertRaises(BadRequest, self.api.sub_cluster_create, sc4)

    def test_sub_cluster_id_deallocated_on_delete(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)

        sc = self.api.sub_cluster_read(id=sc.uuid)
        self.api.sub_cluster_delete(id=sc.uuid)
        zk_db = self._api_server._db_conn._zk_db
        self.assertNotEqual(
            zk_db._get_sub_cluster_from_id(sc.get_sub_cluster_id()),
            sc.get_fq_name_str(),
        )

    def test_sub_cluster_id_range(self):
        sub_cluster_id = 1 << 32  # more than 4 bytes cluster ID
        if self._global_asn > 0xFFFF:
            sub_cluster_id = 1 << 16  # more than 2 bytes cluster ID

        sc = SubCluster('sc-%s' % self.id(), sub_cluster_id=sub_cluster_id)
        with mock.patch.object(self._api_server, '_validate_simple_type',
                               return_value=sub_cluster_id):
            self.assertRaises(BadRequest, self.api.sub_cluster_create, sc)

        sc = SubCluster('sc-%s' % self.id(), sub_cluster_id=0)
        with mock.patch.object(self._api_server, '_validate_simple_type',
                               return_value=0):
            self.assertRaises(BadRequest, self.api.sub_cluster_create, sc)

    def test_update_allocated_id(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)
        sc = self.api.sub_cluster_read(id=sc.uuid)
        allocated_id = sc.get_sub_cluster_id()
        self.assertIsNotNone(allocated_id)

        sub_cluster_id = self._id_range.pop()
        self.assertNotEqual(allocated_id, sub_cluster_id)
        sc.set_sub_cluster_id(sub_cluster_id)
        self.api.sub_cluster_update(sc)
        zk_db = self._api_server._db_conn._zk_db
        self.assertEqual(
            zk_db._get_sub_cluster_from_id(sub_cluster_id),
            sc.get_fq_name_str(),
        )
        self.assertIsNone(zk_db._get_sub_cluster_from_id(allocated_id))


class TestSubClusterWith2BytesASN(TestSubClusterBase, SubClusterTestSuite):
    _global_asn = ASN_2_BYTES


class TestSubClusterWith4BytesASN(TestSubClusterBase, SubClusterTestSuite):
    _global_asn = ASN_4_BYTES


class TestSubClusterChangeGlobalASN(TestSubClusterBase):
    _global_asn = ASN_2_BYTES

    def test_change_global_asn_from_2_to_4_bytes(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)
        sc = self.api.sub_cluster_read(id=sc.uuid)
        allocated_id = sc.get_sub_cluster_id()
        self.assertIsNotNone(allocated_id)
        self.assertLess(allocated_id, 1 << 16)

        self._gsc.set_autonomous_system(ASN_4_BYTES)
        self._gsc.enable_4byte_as = True
        self.api.global_system_config_update(self._gsc)

        sc = self.api.sub_cluster_read(id=sc.uuid)
        self.assertEqual(sc.get_sub_cluster_id(), allocated_id)

        self._gsc.set_autonomous_system(ASN_2_BYTES)
        self._gsc.enable_4byte_as = False
        self.api.global_system_config_update(self._gsc)

    def test_change_global_asn_from_2_to_4_bytes_with_to_high_id(self):
        sub_cluster_id = self._id_range.pop()
        self.assertGreaterEqual(sub_cluster_id, 1 << 16)
        sc = SubCluster('sc-%s' % self.id(), sub_cluster_id=sub_cluster_id)
        self.api.sub_cluster_create(sc)

        self._gsc.set_autonomous_system(ASN_4_BYTES)
        self._gsc.enable_4byte_as = True
        self.assertRaises(BadRequest, self.api.global_system_config_update,
                          self._gsc)

        self.api.sub_cluster_delete(id=sc.uuid)
        self._gsc.set_autonomous_system(ASN_4_BYTES)
        self._gsc.enable_4byte_as = True
        self.api.global_system_config_update(self._gsc)
        sc = SubCluster('sc-%s' % self.id(), sub_cluster_id=sub_cluster_id)
        self.assertRaises(BadRequest, self.api.sub_cluster_create, sc)

        self._gsc.set_autonomous_system(ASN_2_BYTES)
        self._gsc.enable_4byte_as = False
        self.api.global_system_config_update(self._gsc)
        self.api.sub_cluster_create(sc)


class TestSubClusterFirstAllocatedId(TestSubClusterBase):
    def test_first_allocated_id_is_one(self):
        sc = SubCluster('sc-%s' % self.id())
        self.api.sub_cluster_create(sc)
        sc = self.api.sub_cluster_read(id=sc.uuid)

        self.assertEqual(sc.get_sub_cluster_id(), 1)
