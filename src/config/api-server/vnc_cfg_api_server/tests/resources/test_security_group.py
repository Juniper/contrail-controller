#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import SG_NO_RULE_FQ_NAME
from cfgm_common import SGID_MIN_ALLOC
from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import PermissionDenied
from testtools import ExpectedException
from vnc_api.vnc_api import SecurityGroup

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestSecurityGroup(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestSecurityGroup, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestSecurityGroup, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_allocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(sg_id))
        self.assertGreaterEqual(sg_id, SGID_MIN_ALLOC)

    def test_deallocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id

        self.api.security_group_delete(id=sg_obj.uuid)

        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

    def test_not_deallocate_sg_id_if_fq_name_does_not_correspond(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id

        fake_fq_name = "fake fq_name"
        mock_zk._sg_id_allocator.delete(sg_id - SGID_MIN_ALLOC)
        mock_zk._sg_id_allocator.reserve(sg_id - SGID_MIN_ALLOC, fake_fq_name)
        self.api.security_group_delete(id=sg_obj.uuid)

        self.assertIsNotNone(mock_zk.get_sg_from_id(sg_id))
        self.assertEqual(fake_fq_name, mock_zk.get_sg_from_id(sg_id))

    def test_cannot_set_sg_id(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())

        sg_obj.set_security_group_id(8000042)
        with ExpectedException(PermissionDenied):
            self.api.security_group_create(sg_obj)

    def test_cannot_update_sg_id(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)

        sg_obj.set_security_group_id(8000042)
        with ExpectedException(PermissionDenied):
            self.api.security_group_update(sg_obj)

        # test can update with same value, needed internally
        # TODO(ethuleau): not sure why it's needed
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_obj.set_security_group_id(sg_obj.security_group_id)
        self.api.security_group_update(sg_obj)

    def test_create_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        sg_obj.set_configured_security_group_id(42)

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

    def test_update_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self.api.security_group_create(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertEqual(configured_sg_id, 0)

        sg_obj.set_configured_security_group_id(42)
        self.api.security_group_update(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertNotEqual(mock_zk.get_sg_from_id(sg_id),
                            sg_obj.get_fq_name_str())

        sg_obj.set_configured_security_group_id(0)
        self.api.security_group_update(sg_obj)

        sg_obj = self.api.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertEqual(configured_sg_id, 0)

    def test_create_sg_with_configured_id_is_limited(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())

        sg_obj.set_configured_security_group_id(8000000)
        with ExpectedException(BadRequest):
            self.api.security_group_create(sg_obj)

        sg_obj.set_configured_security_group_id(-1)
        with ExpectedException(BadRequest):
            self.api.security_group_create(sg_obj)

    def test_update_sg_with_configured_id_is_limited(self):
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self.api.security_group_create(sg_obj)
        sg_obj = self.api.security_group_read(id=sg_obj.uuid)

        sg_obj.set_configured_security_group_id(8000000)
        with ExpectedException(BadRequest):
            self.api.security_group_update(sg_obj)

        sg_obj.set_configured_security_group_id(-1)
        with ExpectedException(BadRequest):
            self.api.security_group_update(sg_obj)

    def test_singleton_no_rule_sg_for_openstack_created(self):
        try:
            no_rule_sg = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        self.assertIsNotNone(no_rule_sg.security_group_id)
        self.assertIsInstance(no_rule_sg.security_group_id, int)
        self.assertGreater(no_rule_sg.security_group_id, 0)

    def test_singleton_no_rule_sg(self):
        try:
            no_rule_sg = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        sg_obj = SecurityGroup(name=SG_NO_RULE_FQ_NAME[-1])
        self._api_server.create_singleton_entry(sg_obj)
        try:
            no_rule_sg_2 = self.api.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))
        self.assertEqual(no_rule_sg.security_group_id,
                         no_rule_sg_2.security_group_id)
