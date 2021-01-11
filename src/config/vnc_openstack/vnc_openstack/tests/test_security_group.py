# Copyright 2019 Juniper Networks. All rights reserved.

import uuid

from gevent import monkey
monkey.patch_all()  # noqa
from vnc_api.vnc_api import AddressType
from vnc_api.vnc_api import PolicyEntriesType
from vnc_api.vnc_api import PolicyRuleType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import SecurityGroup

from tests import test_case


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

    def test_sgr_remote_ip_prefix_none(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        sg1_dict = self.create_resource('security_group',
                                        proj_obj.uuid,
                                        extra_res_fields={
                                            'name': 'sg1-%s' % self.id(),
                                        })
        sg2_dict = self.create_resource('security_group',
                                        proj_obj.uuid,
                                        extra_res_fields={
                                            'name': 'sg2-%s' % self.id(),
                                        })
        sgr1 = self.create_resource(
            'security_group_rule',
            proj_obj.uuid,
            extra_res_fields={
                'name': 'sgr1-%s' % self.id(),
                'security_group_id': sg1_dict['id'],
                'remote_ip_prefix': None,
                'remote_group_id': None,
                'port_range_min': None,
                'port_range_max': None,
                'protocol': 'tcp',
                'ethertype': 'IPv4',
                'direction': 'egress',
            })
        sgr2 = self.create_resource(
            'security_group_rule',
            proj_obj.uuid,
            extra_res_fields={
                'name': 'sgr2-%s' % self.id(),
                'security_group_id': sg2_dict['id'],
                'remote_ip_prefix': None,
                'remote_group_id': sg1_dict['id'],
                'port_range_min': None,
                'port_range_max': None,
                'protocol': 'tcp',
                'ethertype': 'IPv4',
                'direction': 'egress',
            })
        sg_list = self.list_resource('security_group', proj_obj.uuid)
        found = 0
        for sg in sg_list:
            if sg['id'] == sg1_dict['id']:
                for rule in sg['security_group_rules']:
                    if rule['id'] == sgr1['id']:
                        self.assertEqual(
                            rule['remote_ip_prefix'], '0.0.0.0/0')
                found += 1
            if sg['id'] == sg2_dict['id']:
                for rule in sg['security_group_rules']:
                    if rule['id'] == sgr2['id']:
                        self.assertEqual(
                            rule['remote_ip_prefix'], None)
                found += 1
        self.assertEqual(found, 2)
        sg1_dict = self.update_resource('security_group',
                                        sg1_dict['id'],
                                        proj_obj.uuid,
                                        extra_res_fields={
                                            'name': 'sg1-%s-new' % self.id(),
                                        })
        sg_list = self.list_resource('security_group', proj_obj.uuid)
        found = 0
        for sg in sg_list:
            if sg['id'] == sg1_dict['id']:
                self.assertEqual(sg['name'], sg1_dict['name'])
                found += 1
        self.assertEqual(found, 1)
