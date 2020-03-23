# Copyright 2019 Juniper Networks. All rights reserved.

from builtins import str
import uuid

from gevent import monkey
monkey.patch_all()  # noqa
from vnc_api.vnc_api import AddressType
from vnc_api.vnc_api import PolicyEntriesType
from vnc_api.vnc_api import PolicyRuleType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SecurityGroup

from vnc_openstack.tests import test_case


class TestSecurityGroup(test_case.NeutronBackendTestCase):
    def setUp(self):
        super(TestSecurityGroup, self).setUp()
        self.project_id = self._vnc_lib.project_create(
            Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=self.project_id)

    def test_security_group_rule_list_per_security_group(self):
        sg1 = SecurityGroup('sg1-%s' % self.id(), parent_obj=self.project)
        sgr1_id = str(uuid.uuid4())
        rule = PolicyRuleType(
            rule_uuid=sgr1_id,
            protocol='any',
            src_addresses=[AddressType(security_group='local')],
            dst_addresses=[AddressType(security_group='local')],
        )
        sg1.set_security_group_entries(PolicyEntriesType([rule]))
        self._vnc_lib.security_group_create(sg1)
        sg2 = SecurityGroup('sg2-%s' % self.id(), parent_obj=self.project)
        sgr2_id = str(uuid.uuid4())
        rule = PolicyRuleType(
            rule_uuid=sgr2_id,
            protocol='any',
            src_addresses=[AddressType(security_group='local')],
            dst_addresses=[AddressType(security_group='local')],
        )
        sg2.set_security_group_entries(PolicyEntriesType([rule]))
        self._vnc_lib.security_group_create(sg2)

        list_result = self.list_resource(
            'security_group_rule',
            self.project_id,
            req_filters={
                'security_group_id': [sg1.uuid],
            },
        )
        self.assertEqual(set([sgr1_id]), {sgr['id'] for sgr in list_result})

        list_result = self.list_resource(
            'security_group_rule',
            self.project_id,
            req_filters={
                'security_group_id': [sg1.uuid, sg2.uuid],
            },
        )
        self.assertEqual(set([sgr1_id, sgr2_id]),
                         {sgr['id'] for sgr in list_result})

        list_result = self.list_resource('security_group_rule',
                                         self.project_id)
        self.assertTrue(set([sgr1_id, sgr2_id]).issubset(
            {sgr['id'] for sgr in list_result}))
