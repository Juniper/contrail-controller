# Copyright 2018 Juniper Networks. All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#

from builtins import str
from builtins import range
import uuid

from gevent import monkey
monkey.patch_all()  # noqa
from mock import patch
from neutron_lib import constants
from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import NoIdError
from vnc_api.vnc_api import ApplicationPolicySet
from vnc_api.vnc_api import FirewallPolicy
from vnc_api.vnc_api import FirewallRule
from vnc_api.vnc_api import FirewallSequence
from vnc_api.vnc_api import FirewallServiceType
from vnc_api.vnc_api import PortType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualNetwork

from vnc_openstack.tests import test_case
from vnc_openstack.neutron_plugin_db import\
    _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME
from vnc_openstack.neutron_plugin_db import\
    _NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME
from vnc_openstack.neutron_plugin_db import\
    _NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME
from vnc_openstack.neutron_plugin_db import _NEUTRON_FWAAS_TAG_TYPE


class TestFirewallBase(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestFirewallBase, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'fwaas_enabled', True)])

    def setUp(self):
        super(TestFirewallBase, self).setUp()
        self.project_id = self._vnc_lib.project_create(
            Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=self.project_id)

    def _insert_rule(self, project_id, firewall_policy_id, firewall_rule_id,
                     insert_before=None, insert_after=None):
        extra_res_fields = {
            'firewall_rule_id': firewall_rule_id,
        }
        if insert_before:
            extra_res_fields['insert_before'] = insert_before
        elif insert_after:
            extra_res_fields['insert_after'] = insert_after

        return self.update_resource(
            'firewall_policy',
            firewall_policy_id,
            project_id,
            extra_res_fields=extra_res_fields,
            operation='INSERT_RULE')

    def _remove_rule(self, project_id, firewall_policy_id, firewall_rule_id):
        extra_res_fields = {
            'firewall_rule_id': firewall_rule_id,
        }
        return self.update_resource(
            'firewall_policy',
            firewall_policy_id,
            project_id,
            extra_res_fields=extra_res_fields,
            operation='REMOVE_RULE')

    def _get_tag_fq_name(self, firewall_group, project=None):
        if (not project and 'project_id' not in firewall_group and
                'tenant_id' not in firewall_group):
            return

        if not project:
            project_id = str(uuid.UUID(firewall_group.get(
                'project_id', firewall_group['tenant_id'])))
            project = self._vnc_lib.project_read(id=project_id)

        return project.fq_name + [
            '%s=%s' % (_NEUTRON_FWAAS_TAG_TYPE, firewall_group['id'])]


class TestFirewallGroup(TestFirewallBase):
    def test_dedicated_tag_created(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)

        tag_fq_name = self._get_tag_fq_name(neutron_fg, self.project)
        try:
            tag = self._vnc_lib.tag_read(tag_fq_name)
        except NoIdError:
            msg = ("Dedicated Tag %s for firewall group %s was not created" %
                   (':'.join(tag_fq_name), neutron_fg['id']))
            self.fail(msg)

        aps_backrefs = tag.get_application_policy_set_back_refs() or []
        self.assertIsNotNone(len(aps_backrefs), 1)

    def test_dedicated_tag_and_refs_deleted(self):
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        vmi_ids = []
        for i in range(3):
            vmi = VirtualMachineInterface(
                '%s-vmi%d' % (self.id(), i), parent_obj=self.project)
            vmi.add_virtual_network(vn)
            vmi_ids.append(self._vnc_lib.virtual_machine_interface_create(vmi))
        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'ports': vmi_ids,
            },
        )

        tag_fq_name = self._get_tag_fq_name(neutron_fg, self.project)
        try:
            self._vnc_lib.tag_read(tag_fq_name)
        except NoIdError:
            msg = ("Dedicated Tag %s for firewall group %s was not created" %
                   (':'.join(tag_fq_name), neutron_fg['id']))
            self.fail(msg)

        self.delete_resource('firewall_group', self.project_id,
                             neutron_fg['id'])
        self.assertRaises(NoIdError, self._vnc_lib.tag_read, tag_fq_name)

    def test_aps_cleaned_if_create_tag_fails(self):
        with patch.object(self.neutron_db_obj._vnc_lib, 'tag_create',
                          side_effect=BadRequest(400, "Fake bad request")):
            self.create_resource(
                'firewall_group', self.project_id, status="400 Bad Request")

        tags = self._vnc_lib.tags_list(parent_id=self.project_id)['tags']
        # Only dedicated tag for Neutron FWaaSv2 default firewall group remains
        self.assertEquals(len(tags), 1)
        apss = self._vnc_lib.application_policy_sets_list(
            parent_id=self.project_id)['application-policy-sets']
        # Only default Contrail project APS and Neutron FWaaSv2 default
        # firewall group remains
        self.assertEquals(len(apss), 2)
        self.assertEquals(
            {r['fq_name'][-1] for r in apss},
            set([ApplicationPolicySet(parent_type='project').name,
                 _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME]),
        )

    def test_aps_cleaned_if_associate_tag_fails(self):
        with patch.object(self.neutron_db_obj._vnc_lib, 'set_tag',
                          side_effect=BadRequest(400, "Fake bad request")):
            self.create_resource(
                'firewall_group', self.project_id, status='400 Bad Request')

        tags = self._vnc_lib.tags_list(parent_id=self.project_id)['tags']
        # Only dedicated tag for Neutron FWaaSv2 default firewall group remains
        self.assertEquals(len(tags), 1)
        apss = self._vnc_lib.application_policy_sets_list(
            parent_id=self.project_id)['application-policy-sets']
        # Only default Contrail project APS and Neutron FWaaSv2 default
        # firewall group remains
        self.assertEquals(len(apss), 2)
        self.assertEquals(
            {r['fq_name'][-1] for r in apss},
            set([ApplicationPolicySet(parent_type='project').name,
                 _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME]),
        )

    def test_ingress_policy_set_to_egress(self):
        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'ingress_firewall_policy_id': fp.uuid,
            },
        )
        self.assertEquals(neutron_fg['ingress_firewall_policy_id'], fp.uuid)
        self.assertEquals(neutron_fg['egress_firewall_policy_id'], fp.uuid)

    def test_egress_policy_set_to_ingress(self):
        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': fp.uuid,
            },
        )
        self.assertEquals(neutron_fg['ingress_firewall_policy_id'], fp.uuid)
        self.assertEquals(neutron_fg['egress_firewall_policy_id'], fp.uuid)

    def test_can_set_same_egress_and_ingress_policies(self):
        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'ingress_firewall_policy_id': fp.uuid,
                'egress_firewall_policy_id': fp.uuid,
            },
        )
        self.assertEquals(neutron_fg['ingress_firewall_policy_id'], fp.uuid)
        self.assertEquals(neutron_fg['egress_firewall_policy_id'], fp.uuid)

    def test_cannot_set_different_egress_and_ingress_policies(self):
        fp1 = FirewallPolicy('%s-fp1' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp1)
        fp2 = FirewallPolicy('%s-fp2' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp2)

        self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'ingress_firewall_policy_id': fp1.uuid,
                'egress_firewall_policy_id': fp2.uuid,
            },
            status="400 Bad Request",
        )

    def test_egress_and_ingress_policies_remove_if_ingress_deleted(self):
        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': fp.uuid,
            },
        )

        self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'ingress_firewall_policy_id': None,
            },
        )
        neutron_fg = self.read_resource('firewall_group', neutron_fg['id'])
        self.assertNotIn('ingress_firewall_policy_id', neutron_fg)
        self.assertNotIn('egress_firewall_policy_id', neutron_fg)

    def test_egress_and_ingress_policies_remove_if_egress_deleted(self):
        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': fp.uuid,
            },
        )

        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': None,
            },
        )
        self.assertNotIn('ingress_firewall_policy_id', neutron_fg)
        self.assertNotIn('egress_firewall_policy_id', neutron_fg)

    def test_firewall_group_status(self):
        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'admin_state_up': False,
            },
        )
        self.assertEquals(neutron_fg['status'], constants.DOWN)

        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'admin_state_up': True,
            },
        )
        self.assertEquals(neutron_fg['status'], constants.INACTIVE)

        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        vmi = VirtualMachineInterface(
            '%s-vmi' % self.id(), parent_obj=self.project)
        vmi.add_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'ports': [vmi.uuid],
            },
        )
        self.assertEquals(neutron_fg['status'], constants.INACTIVE)

        fp = FirewallPolicy('%s-fp' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp)
        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': fp.uuid,
            },
        )
        self.assertEquals(neutron_fg['status'], constants.ACTIVE)

        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'ports': [],
            },
        )
        self.assertEquals(neutron_fg['status'], constants.INACTIVE)

    def test_remove_extra_fp_refs(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)
        aps = self._vnc_lib.application_policy_set_read(id=neutron_fg['id'])
        fp1 = FirewallPolicy('%s-fp1' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp1)
        fp2 = FirewallPolicy('%s-fp2' % self.id(), parent_obj=self.project)
        self._vnc_lib.firewall_policy_create(fp2)
        aps.add_firewall_policy(fp1, FirewallSequence(sequence='0.0'))
        aps.add_firewall_policy(fp2, FirewallSequence(sequence='1.0'))
        self._vnc_lib.application_policy_set_update(aps)

        neutron_fg = self.read_resource('firewall_group', neutron_fg['id'])
        self.assertEquals(neutron_fg['ingress_firewall_policy_id'], fp1.uuid)
        self.assertEquals(neutron_fg['egress_firewall_policy_id'], fp1.uuid)
        aps = self._vnc_lib.application_policy_set_read(id=neutron_fg['id'])
        fp_refs = aps.get_firewall_policy_refs() or []
        self.assertEquals(len(fp_refs), 1)
        self.assertEquals(fp_refs[0]['uuid'], fp1.uuid)

    def test_firewall_group_port_association(self):
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        vmi_ids = []
        for i in range(3):
            vmi = VirtualMachineInterface(
                '%s-vmi%d' % (self.id(), i), parent_obj=self.project)
            vmi.add_virtual_network(vn)
            vmi_ids.append(self._vnc_lib.virtual_machine_interface_create(vmi))

        neutron_fg = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'ports': vmi_ids[:-1],
            },
        )
        self.assertEquals(set(neutron_fg['ports']), set(vmi_ids[:-1]))

        neutron_fg = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'ports': vmi_ids[1:],
            },
        )
        self.assertEquals(set(neutron_fg['ports']), set(vmi_ids[1:]))

    def test_multiple_firewall_group_port_association(self):
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        vmi = VirtualMachineInterface('%s-vmi' % self.id(),
                                      parent_obj=self.project)
        vmi.add_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi)

        neutron_fg1 = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'name': '%s-fg1' % self.id(),
                'ports': [vmi_id],
            },
        )
        neutron_fg2 = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'name': '%s-fg2' % self.id(),
            },
        )
        tag1_fq_name = self._get_tag_fq_name(neutron_fg1, self.project)
        tag2_fq_name = self._get_tag_fq_name(neutron_fg2, self.project)
        self.assertEquals(set(neutron_fg1['ports']), set([vmi_id]))
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        tag_refs = [r['to'] for r in vmi.get_tag_refs() or []]
        self.assertEquals(len(tag_refs), 1)
        self.assertEquals(tag1_fq_name, tag_refs[0])

        neutron_fg2 = self.update_resource(
            'firewall_group',
            neutron_fg2['id'],
            self.project_id,
            extra_res_fields={
                'ports': [vmi_id],
            },
        )
        neutron_fg1 = self.read_resource('firewall_group', neutron_fg1['id'])
        self.assertEquals(set(neutron_fg1['ports']), set([vmi_id]))
        self.assertEquals(set(neutron_fg2['ports']), set([vmi_id]))
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        tag_refs = [r['to'] for r in vmi.get_tag_refs() or []]
        self.assertEquals(len(tag_refs), 2)
        self.assertIn(tag1_fq_name, tag_refs)
        self.assertIn(tag2_fq_name, tag_refs)

        neutron_fg1 = self.update_resource(
            'firewall_group',
            neutron_fg1['id'],
            self.project_id,
            extra_res_fields={
                'ports': [],
            },
        )
        neutron_fg2 = self.read_resource('firewall_group', neutron_fg2['id'])
        self.assertFalse(neutron_fg1['ports'])
        self.assertEquals(set(neutron_fg2['ports']), set([vmi_id]))
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        tag_refs = [r['to'] for r in vmi.get_tag_refs() or []]
        self.assertEquals(len(tag_refs), 1)
        self.assertEquals(tag2_fq_name, tag_refs[0])

    def test_list_firewall_group(self):
        vn = VirtualNetwork('%s-vn' % self.id(), parent_obj=self.project)
        self._vnc_lib.virtual_network_create(vn)
        neutron_fgs = []
        fp_ids = []
        vmi_ids = []
        for i in range(2):
            fp = FirewallPolicy('%s-fp%d' % (self.id(), i),
                                parent_obj=self.project)
            fp_ids.append(self._vnc_lib.firewall_policy_create(fp))

            vmi = VirtualMachineInterface(
                '%s-vmi%d' % (self.id(), i), parent_obj=self.project)
            vmi.add_virtual_network(vn)
            vmi_ids.append(self._vnc_lib.virtual_machine_interface_create(vmi))

            neutron_fgs.append(
                self.create_resource(
                    'firewall_group',
                    self.project_id,
                    extra_res_fields={
                        'name': '%s-fg%d' % (self.id(), i),
                        'ingress_firewall_policy_id': fp.uuid,
                        'ports': [vmi.uuid],
                    },
                ),
            )

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'ingress_firewall_policy_id': fp_ids,
            },
        )
        self.assertEquals(len(list_result), len(neutron_fgs))
        self.assertEquals({r['id'] for r in list_result},
                          {r['id'] for r in neutron_fgs})

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'egress_firewall_policy_id': fp_ids,
            },
        )
        self.assertEquals(len(list_result), len(neutron_fgs))
        self.assertEquals({r['id'] for r in list_result},
                          {r['id'] for r in neutron_fgs})

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'ports': vmi_ids,
            },
        )
        self.assertEquals(len(list_result), len(neutron_fgs))
        self.assertEquals({r['id'] for r in list_result},
                          {r['id'] for r in neutron_fgs})

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'ingress_firewall_policy_id': [fp_ids[0]],
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0], neutron_fgs[0])

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'egress_firewall_policy_id': [fp_ids[1]],
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0], neutron_fgs[1])

        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'ports': [vmi_ids[0]],
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0], neutron_fgs[0])

    def test_default_firewall_group_exists(self):
        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['name'],
                          _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME)

    def test_cannot_create_firewall_group_with_default_name(self):
        resp = self.create_resource(
            'firewall_group',
            self.project_id,
            extra_res_fields={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'],
                          'FirewallGroupDefaultAlreadyExists')

    def test_cannot_update_firewall_group_with_default_name(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)
        resp = self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'],
                          'FirewallGroupDefaultAlreadyExists')

    def test_cannot_update_default_firewall_group(self):
        neutron_default_fg = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
        )[0]
        fp_uuid = self._vnc_lib.firewall_policy_create(FirewallPolicy(
            '%s-fp' % self.id(), parent_obj=self.project))

        attrs = {
            'name': 'fake name',
            'description': 'fake description',
            'admin_state_up': False,
            'ingress_firewall_policy_id': fp_uuid,
            'egress_firewall_policy_id': fp_uuid,
        }
        for attr, value in attrs.items():
            resp = self.update_resource(
                'firewall_group',
                neutron_default_fg['id'],
                self.project_id,
                extra_res_fields={
                    attr: value,
                },
                status="400 Bad Request",
            )
            self.assertEquals(resp['exception'],
                              'FirewallGroupCannotUpdateDefault')

        # admin can update default firewall group but not the name
        attrs.pop('name')
        for attr, value in list(attrs.items()):
            self.update_resource(
                'firewall_group',
                neutron_default_fg['id'],
                self.project_id,
                extra_res_fields={
                    attr: value,
                },
                is_admin=True,
            )
        resp = self.update_resource(
            'firewall_group',
            neutron_default_fg['id'],
            self.project_id,
            extra_res_fields={
                'name': 'fake name',
            },
            is_admin=True,
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'],
                          'FirewallGroupCannotUpdateDefault')

    def test_delete_default_firewall_group(self):
        neutron_default_fg = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
        )[0]

        # cannot delete default firewall group
        resp = self.delete_resource(
            'firewall_group',
            self.project_id,
            neutron_default_fg['id'],
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'],
                          'FirewallGroupCannotRemoveDefault')

        # admin can delete it
        self.delete_resource(
            'firewall_group',
            self.project_id,
            neutron_default_fg['id'],
            is_admin=True,
        )

        # default firewall group automatically re-created
        list_result = self.list_resource(
            'firewall_group',
            self.project_id,
            req_filters={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['name'],
                          _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME)
        self.assertNotEquals(list_result[0]['id'], neutron_default_fg['id'])

    def test_delete_firewall_group_used_as_remote_in_firewall_rule(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)
        neutron_fr = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg['id'],
                'destination_firewall_group_id': neutron_fg['id'],
            },
        )
        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        self.assertEquals(len(fr.get_tag_refs()), 1)
        self.assertEquals(fr.get_tag_refs()[0]['to'],
                          self._get_tag_fq_name(neutron_fg, self.project))

        self.delete_resource(
            'firewall_group',
            self.project_id,
            neutron_fg['id'],
        )
        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        self.assertIsNone(fr.get_tag_refs())


class TestFirewallPolicy(TestFirewallBase):
    def test_firewall_policy_audited_flag(self):
        neutron_fp = self.create_resource('firewall_policy', self.project_id)
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertFalse(neutron_fp['audited'])
        self.assertFalse(fp.get_id_perms().get_enable())

        neutron_fp = self.update_resource(
            'firewall_policy',
            neutron_fp['id'],
            self.project_id,
            extra_res_fields={
                'audited': True,
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertTrue(neutron_fp['audited'])
        self.assertTrue(fp.get_id_perms().get_enable())

        fr_ids = []
        for i in range(4):
            fr = FirewallRule(
                '%s-fr%d' % (self.id(), i),
                parent_obj=self.project,
                service=FirewallServiceType())
            fr_ids.append(self._vnc_lib.firewall_rule_create(fr))
        neutron_fp = self.update_resource(
            'firewall_policy',
            neutron_fp['id'],
            self.project_id,
            extra_res_fields={
                'firewall_rules': [fr_ids[0]],
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertFalse(neutron_fp['audited'])
        self.assertFalse(fp.get_id_perms().get_enable())

        neutron_fp = self.update_resource(
            'firewall_policy',
            neutron_fp['id'],
            self.project_id,
            extra_res_fields={
                'audited': True,
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertTrue(neutron_fp['audited'])
        self.assertTrue(fp.get_id_perms().get_enable())

        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr_ids[1],
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertFalse(neutron_fp['audited'])
        self.assertFalse(fp.get_id_perms().get_enable())

        neutron_fp = self.update_resource(
            'firewall_policy',
            neutron_fp['id'],
            self.project_id,
            extra_res_fields={
                'audited': True,
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertTrue(neutron_fp['audited'])
        self.assertTrue(fp.get_id_perms().get_enable())

        neutron_fp = self._remove_rule(
            self.project_id,
            neutron_fp['id'],
            fr_ids[1],
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertFalse(neutron_fp['audited'])
        self.assertFalse(fp.get_id_perms().get_enable())

    def test_firewall_policy_audited_flag_when_referenced_rule_change(self):
        fr_id = self._vnc_lib.firewall_rule_create(
            FirewallRule(
                '%s-fr' % self.id(),
                parent_obj=self.project,
                service=FirewallServiceType(),
            ),
        )
        neutron_fp = self.create_resource(
            'firewall_policy',
            self.project_id,
            extra_res_fields={
                'audited': True,
                'firewall_rules': [fr_id],
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertTrue(neutron_fp['audited'])
        self.assertTrue(fp.get_id_perms().get_enable())

        self.update_resource(
            'firewall_rule',
            fr_id,
            self.project_id,
            extra_res_fields={
                'action': 'deny',
            },
        )
        neutron_fp = self.read_resource('firewall_policy', neutron_fp['id'])
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        self.assertFalse(neutron_fp['audited'])
        self.assertFalse(fp.get_id_perms().get_enable())

    def test_firewall_policy_rule_association(self):
        fr_ids = []
        for i in range(4):
            fr = FirewallRule(
                '%s-fr%d' % (self.id(), i),
                parent_obj=self.project,
                service=FirewallServiceType())
            fr_ids.append(self._vnc_lib.firewall_rule_create(fr))
        neutron_fp = self.create_resource(
            'firewall_policy',
            self.project_id,
            extra_res_fields={
                'firewall_rules': fr_ids[1:3],
            },
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 2)
        for idx, fr_id in enumerate(fr_ids[1:3]):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # insert at the begining
        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr_ids[0],
        )
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 3)
        for idx, fr_id in enumerate(fr_ids[0:3]):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # insert after
        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr_ids[3],
            insert_after=fr_ids[2])
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 4)
        for idx, fr_id in enumerate(fr_ids):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # insert before
        fr5 = FirewallRule(
            '%s-fr5' % self.id(),
            parent_obj=self.project,
            service=FirewallServiceType())
        before_id = fr_ids[2]
        fr_ids.insert(fr_ids.index(before_id),
                      self._vnc_lib.firewall_rule_create(fr5))
        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr5.uuid,
            insert_before=before_id)
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 5)
        for idx, fr_id in enumerate(fr_ids):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # remove
        fr_ids.remove(fr5.uuid)
        neutron_fp = self._remove_rule(
            self.project_id,
            neutron_fp['id'],
            fr5.uuid)
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 4)
        for idx, fr_id in enumerate(fr_ids):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # insert_after ignored if insert_before is set
        before_id = fr_ids[2]
        after_id = fr_ids[fr_ids.index(before_id) + 1]
        fr_ids.insert(fr_ids.index(before_id), fr5.uuid)
        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr5.uuid,
            insert_before=before_id,
            insert_after=after_id)
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 5)
        for idx, fr_id in enumerate(fr_ids):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

        # move existing rule in the list
        fr_ids.remove(fr5.uuid)
        before_id = fr_ids[1]
        fr_ids.insert(fr_ids.index(before_id), fr5.uuid)
        neutron_fp = self._insert_rule(
            self.project_id,
            neutron_fp['id'],
            fr5.uuid,
            insert_before=before_id)
        fp = self._vnc_lib.firewall_policy_read(id=neutron_fp['id'])
        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        self.assertEquals(len(sorted_fr_refs), 5)
        for idx, fr_id in enumerate(fr_ids):
            self.assertEquals(sorted_fr_refs[idx]['uuid'], fr_id)

    def test_firewall_policy_list(self):
        fr_ids = []
        fp_ids = []
        for i in range(2):
            neutron_fr = self.create_resource('firewall_rule', self.project_id)
            fr_ids.append(neutron_fr['id'])
            fp_ids.append(self.create_resource(
                'firewall_policy',
                self.project_id,
                extra_res_fields={
                    'name': '%s-fp%d' % (self.id(), i),
                    'firewall_rules': [neutron_fr['id']],
                },
            )['id'])

        list_result = self.list_resource(
            'firewall_policy',
            self.project_id,
            req_filters={
                'firewall_rules': [fr_ids[0]],
            }
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['id'], fp_ids[0])

        list_result = self.list_resource(
            'firewall_policy',
            self.project_id,
            req_filters={
                'firewall_rules': [fr_ids[1]],
            }
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['id'], fp_ids[1])

        list_result = self.list_resource(
            'firewall_policy',
            self.project_id,
            req_filters={
                'firewall_rules': fr_ids,
            }
        )
        self.assertEquals(len(list_result), 2)
        self.assertEquals({fp['id'] for fp in list_result}, set(fp_ids))

    def test_default_firewall_policy_exists(self):
        list_result = self.list_resource(
            'firewall_policy',
            self.project_id,
            req_filters={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['name'],
                          _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME)

    def test_cannot_create_firewall_policy_with_default_name(self):
        self.create_resource(
            'firewall_policy',
            self.project_id,
            extra_res_fields={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
            status="400 Bad Request",
        )

    def test_cannot_update_firewall_policy_with_default_name(self):
        neutron_fp = self.create_resource('firewall_policy', self.project_id)
        self.update_resource(
            'firewall_policy',
            neutron_fp['id'],
            self.project_id,
            extra_res_fields={
                'name': _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME,
            },
            status="400 Bad Request",
        )


class TestFirewallRule(TestFirewallBase):
    def test_firewall_rule_properties_mapping(self):
        src_addr = '1.2.3.4'
        dst_addr = '0/0'
        protocol = 'tcp'
        src_ports = (123, 456)
        dst_ports = (789, 1234)
        action = 'allow'

        neutron_fr = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': src_addr,
                'destination_ip_address': dst_addr,
                'protocol': protocol,
                'source_port': '%d:%d' % src_ports,
                'destination_port': '%d:%d' % dst_ports,
                'action': action,
            },
        )

        self.assertEquals(neutron_fr['ip_version'], 4)
        self.assertEquals(neutron_fr['source_ip_address'], '%s/32' % src_addr)
        self.assertEquals(neutron_fr['destination_ip_address'], '0.0.0.0/0')
        self.assertEquals(neutron_fr['protocol'], protocol)
        self.assertEquals(neutron_fr['source_port'], '%d:%d' % src_ports)
        self.assertEquals(neutron_fr['destination_port'], '%d:%d' % dst_ports)
        self.assertEquals(neutron_fr['action'], action)

        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        ep1 = fr.get_endpoint_1()
        subnet = ep1.get_subnet()
        prefix = '%s/%d' % (subnet.get_ip_prefix(), subnet.get_ip_prefix_len())
        self.assertEquals(prefix, '%s/32' % src_addr)
        self.assertIsNone(ep1.get_any())
        ep2 = fr.get_endpoint_2()
        self.assertIsNone(ep2.get_any())
        self.assertEqual(ep2.get_subnet().ip_prefix, '0.0.0.0')
        self.assertEqual(ep2.get_subnet().ip_prefix_len, 0)
        service = fr.get_service()
        self.assertEquals(service.protocol, protocol)
        self.assertEquals(service.src_ports, PortType(*src_ports))
        self.assertEquals(service.dst_ports, PortType(*dst_ports))
        action = fr.get_action_list()
        self.assertEquals(action.get_simple_action(), 'pass')
        self.assertEquals(fr.get_direction(), '>')

    def test_port_range(self):
        port = '42'
        neutron_fr = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_port': port,
            },
        )
        self.assertEquals(neutron_fr['source_port'], port)
        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        service = fr.get_service()
        self.assertEquals(service.src_ports, PortType(int(port), int(port)))

        resp = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_port': 'foo:%s' % port,
            },
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'], 'FirewallRuleInvalidPortValue')

    def test_subnet(self):
        neutron_fr1 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': '0/0',
            },
        )
        self.assertEquals(neutron_fr1['source_ip_address'], '0.0.0.0/0')
        fr1 = self._vnc_lib.firewall_rule_read(id=neutron_fr1['id'])
        self.assertIsNone(fr1.get_endpoint_1().get_any())
        self.assertEqual(fr1.get_endpoint_1().get_subnet().ip_prefix,
                         '0.0.0.0')
        self.assertEqual(fr1.get_endpoint_1().get_subnet().ip_prefix_len, 0)

        neutron_fr2 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': '0:0::/0',
            },
        )
        self.assertEquals(neutron_fr2['source_ip_address'], '::/0')
        fr2 = self._vnc_lib.firewall_rule_read(id=neutron_fr2['id'])
        self.assertIsNone(fr2.get_endpoint_1().get_any())
        self.assertEqual(fr2.get_endpoint_1().get_subnet().ip_prefix, '::')
        self.assertEqual(fr2.get_endpoint_1().get_subnet().ip_prefix_len, 0)

        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': 'stale prefix',
            },
            status="400 Bad Request",
        )

    def test_ip_version_match_rule_addresses(self):
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'ip_version': 6,
                'source_ip_address': '1.2.3.0/24',
            },
            status="400 Bad Request",
        )
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'ip_version': 6,
                'destination_ip_address': '1.2.3.0/24',
            },
            status="400 Bad Request",
        )
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'ip_version': 4,
                'source_ip_address': 'dead:beef::/64',
            },
            status="400 Bad Request",
        )
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'ip_version': 4,
                'destination_ip_address': 'dead:beef::/64',
            },
            status="400 Bad Request",
        )
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': '1.2.3.0/24',
                'destination_ip_address': 'dead:beef::/64',
            },
            status="400 Bad Request",
        )
        self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': 'dead:beef::/64',
                'destination_ip_address': '1.2.3.0/24',
            },
            status="400 Bad Request",
        )

    def test_firewall_group_as_source_or_and_destination(self):
        neutron_fg1 = self.create_resource('firewall_group', self.project_id)
        neutron_fg2 = self.create_resource('firewall_group', self.project_id)

        neutron_fr1 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg1['id'],
                'destination_firewall_group_id': None,
            },
        )
        self.assertEquals(
            neutron_fr1['source_firewall_group_id'], neutron_fg1['id'])
        self.assertEquals(neutron_fr1['ip_version'], 4)
        fr1 = self._vnc_lib.firewall_rule_read(id=neutron_fr1['id'])
        ep1 = fr1.get_endpoint_1()
        ep2 = fr1.get_endpoint_2()
        self.assertEquals(len(ep1.get_tags()), 1)
        self.assertEquals(ep1.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])
        self.assertTrue(ep2.get_any())

        neutron_fr2 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': None,
                'destination_firewall_group_id': neutron_fg1['id'],
            },
        )
        self.assertEquals(
            neutron_fr2['destination_firewall_group_id'], neutron_fg1['id'])
        self.assertEquals(neutron_fr2['ip_version'], 4)
        fr2 = self._vnc_lib.firewall_rule_read(id=neutron_fr2['id'])
        ep1 = fr2.get_endpoint_1()
        ep2 = fr2.get_endpoint_2()
        self.assertTrue(ep1.get_any())
        self.assertEquals(len(ep2.get_tags()), 1)
        self.assertEquals(ep2.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])

        neutron_fr3 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg1['id'],
                'destination_ip_address': '1.2.3.0/24',
            },
        )
        self.assertEquals(
            neutron_fr3['source_firewall_group_id'], neutron_fg1['id'])
        self.assertEquals(neutron_fr3['ip_version'], 4)
        fr3 = self._vnc_lib.firewall_rule_read(id=neutron_fr3['id'])
        ep1 = fr3.get_endpoint_1()
        ep2 = fr3.get_endpoint_2()
        self.assertEquals(len(ep1.get_tags()), 1)
        self.assertEquals(ep1.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])
        self.assertIsNone(ep2.get_any())
        self.assertEqual(ep2.get_subnet().ip_prefix, '1.2.3.0')
        self.assertEqual(ep2.get_subnet().ip_prefix_len, 24)

        neutron_fr4 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg1['id'],
                'destination_ip_address': 'dead:beef::/64',
            },
        )
        self.assertEquals(
            neutron_fr4['source_firewall_group_id'], neutron_fg1['id'])
        self.assertEquals(neutron_fr4['ip_version'], 6)
        fr4 = self._vnc_lib.firewall_rule_read(id=neutron_fr4['id'])
        ep1 = fr4.get_endpoint_1()
        ep2 = fr4.get_endpoint_2()
        self.assertEquals(len(ep1.get_tags()), 1)
        self.assertEquals(ep1.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])
        self.assertIsNone(ep2.get_any())
        self.assertEqual(ep2.get_subnet().ip_prefix, 'dead:beef::')
        self.assertEqual(ep2.get_subnet().ip_prefix_len, 64)

        neutron_fr5 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg1['id'],
                'destination_firewall_group_id': neutron_fg2['id'],
            },
        )
        self.assertEquals(neutron_fr5['ip_version'], 4)
        fr5 = self._vnc_lib.firewall_rule_read(id=neutron_fr5['id'])
        ep1 = fr5.get_endpoint_1()
        ep2 = fr5.get_endpoint_2()
        self.assertEquals(len(ep1.get_tags()), 1)
        self.assertEquals(ep1.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])
        self.assertEquals(len(ep2.get_tags()), 1)
        self.assertEquals(ep2.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg2, self.project)[-1])

        neutron_fr6 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_firewall_group_id': neutron_fg1['id'],
                'destination_firewall_group_id': neutron_fg2['id'],
                'ip_version': 6,
                'protocol': 'icmp',
            },
        )
        self.assertEquals(neutron_fr6['ip_version'], 4)
        fr6 = self._vnc_lib.firewall_rule_read(id=neutron_fr6['id'])
        ep1 = fr6.get_endpoint_1()
        ep2 = fr6.get_endpoint_2()
        self.assertEquals(len(ep1.get_tags()), 1)
        self.assertEquals(ep1.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg1, self.project)[-1])
        self.assertEquals(len(ep2.get_tags()), 1)
        self.assertEquals(ep2.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg2, self.project)[-1])

    def test_firewall_rule_update_endpoint(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)
        neutron_fr = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': None,
                'destination_firewall_group_id': None,
            },
        )
        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        ep1 = fr.get_endpoint_1()
        ep2 = fr.get_endpoint_2()
        self.assertTrue(ep1.get_any())
        self.assertTrue(ep2.get_any())

        self.update_resource(
            'firewall_rule',
            neutron_fr['id'],
            self.project_id,
            extra_res_fields={
                'source_ip_address': '1.2.3.0/24',
                'destination_firewall_group_id': neutron_fg['id'],
            },
        )
        fr = self._vnc_lib.firewall_rule_read(id=neutron_fr['id'])
        ep1 = fr.get_endpoint_1()
        ep2 = fr.get_endpoint_2()
        self.assertIsNone(ep1.get_any())
        self.assertEqual(ep1.get_subnet().ip_prefix, '1.2.3.0')
        self.assertEqual(ep1.get_subnet().ip_prefix_len, 24)
        self.assertIsNone(ep2.get_any())
        self.assertEquals(len(ep2.get_tags()), 1)
        self.assertEquals(ep2.get_tags()[0],
                          self._get_tag_fq_name(neutron_fg, self.project)[-1])

    def test_firewall_rule_set_policy_refs(self):
        neutron_fr = self.create_resource('firewall_rule', self.project_id)
        fp_ids = set()
        for i in range(3):
            neutron_fp = self.create_resource(
                'firewall_policy',
                self.project_id,
                extra_res_fields={
                    'name': '%s-fp%d' % (self.id(), i),
                    'firewall_rules': [neutron_fr['id']],
                },
            )
            fp_ids.add(neutron_fp['id'])

        neutron_fr = self.read_resource('firewall_rule', neutron_fr['id'])
        self.assertEquals(len(neutron_fr['firewall_policy_id']), 3)
        self.assertEquals(set(neutron_fr['firewall_policy_id']), fp_ids)

    def test_firewall_rule_list(self):
        fr_ids = []
        fp_ids = []
        for i in range(2):
            neutron_fr = self.create_resource('firewall_rule', self.project_id)
            fr_ids.append(neutron_fr['id'])
            fp_ids.append(self.create_resource(
                'firewall_policy',
                self.project_id,
                extra_res_fields={
                    'name': '%s-fp%d' % (self.id(), i),
                    'firewall_rules': [neutron_fr['id']],
                },
            )['id'])
        fp_ids.append(self.create_resource(
            'firewall_policy',
            self.project_id,
            extra_res_fields={
                'name': '%s-fp2' % self.id(),
                'firewall_rules': fr_ids,
            },
        )['id'])

        list_result = self.list_resource(
            'firewall_rule',
            self.project_id,
            req_filters={
                'firewall_policy_id': [fp_ids[0]],
            }
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['id'], fr_ids[0])

        list_result = self.list_resource(
            'firewall_rule',
            self.project_id,
            req_filters={
                'firewall_policy_id': [fp_ids[1]],
            }
        )
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['id'], fr_ids[1])

        list_result = self.list_resource(
            'firewall_rule',
            self.project_id,
            req_filters={
                'firewall_policy_id': [fp_ids[2]],
            }
        )
        self.assertEquals(len(list_result), 2)
        self.assertEquals({fr['id'] for fr in list_result}, set(fr_ids))

        list_result = self.list_resource(
            'firewall_rule',
            self.project_id,
            req_filters={
                'firewall_policy_id': fp_ids[:2],
            }
        )
        self.assertEquals(len(list_result), 2)
        self.assertEquals({fr['id'] for fr in list_result}, set(fr_ids))

        list_result = self.list_resource(
            'firewall_rule',
            self.project_id,
            req_filters={
                'firewall_policy_id': fp_ids[1:],
            }
        )
        self.assertEquals(len(list_result), 2)
        self.assertEquals({fr['id'] for fr in list_result}, set(fr_ids))

    def test_firewall_rule_does_not_support_reject_action(self):
        resp = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'action': 'reject',
            },
            status="400 Bad Request",
        )
        self.assertEquals(resp['exception'], 'FirewallRuleInvalidAction')

    def test_default_firewall_rule_exists(self):
        for name in [_NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME,
                     _NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME]:
            list_result = self.list_resource(
                'firewall_rule',
                self.project_id,
                req_filters={
                    'name': name,
                },
            )
            self.assertEquals(len(list_result), 1)
            self.assertEquals(list_result[0]['name'], name)

    def test_cannot_create_firewall_rule_with_default_name(self):
        for name in [_NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME,
                     _NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME]:
            self.create_resource(
                'firewall_rule',
                self.project_id,
                extra_res_fields={
                    'name': name,
                },
                status="400 Bad Request",
            )

    def test_cannot_update_firewall_rule_with_default_name(self):
        neutron_fr = self.create_resource('firewall_rule', self.project_id)
        for name in [_NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME,
                     _NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME]:
            self.update_resource(
                'firewall_rule',
                neutron_fr['id'],
                self.project_id,
                extra_res_fields={
                    'name': name,
                },
                status="400 Bad Request",
            )

    def test_protocol_set_to_any_if_not_defined_or_set_to_any(self):
        neutron_fr1 = self.create_resource('firewall_rule', self.project_id)
        self.assertEquals(neutron_fr1['protocol'], 'any')
        fr1 = self._vnc_lib.firewall_rule_read(id=neutron_fr1['id'])
        self.assertEquals(fr1.get_service().get_protocol(), 'any')

        neutron_fr2 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'protocol': None,
            },
        )
        self.assertEquals(neutron_fr2['protocol'], 'any')
        fr2 = self._vnc_lib.firewall_rule_read(id=neutron_fr2['id'])
        self.assertEquals(fr2.get_service().get_protocol(), 'any')

        neutron_fr3 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'protocol': 'any',
            },
        )
        self.assertEquals(neutron_fr3['protocol'], 'any')
        fr3 = self._vnc_lib.firewall_rule_read(id=neutron_fr3['id'])
        self.assertEquals(fr3.get_service().get_protocol(), 'any')

    def test_firewall_rule_icmp_convert_to_ipv6_icmp(self):
        neutron_fr1 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': 'dead:beef::/64',
                'protocol': 'icmp',
            },
        )
        fr1 = self._vnc_lib.firewall_rule_read(id=neutron_fr1['id'])
        self.assertEqual(neutron_fr1['ip_version'], 6)
        self.assertEqual(neutron_fr1['protocol'], 'icmp')
        self.assertEqual(fr1.get_service().get_protocol(), 'ipv6-icmp')
        self.assertEqual(fr1.get_service().get_protocol_id(), 58)

        neutron_fr2 = self.create_resource(
            'firewall_rule',
            self.project_id,
            extra_res_fields={
                'source_ip_address': '1.2.3.0/24',
                'protocol': 'ipv6-icmp',
            },
        )
        fr2 = self._vnc_lib.firewall_rule_read(id=neutron_fr2['id'])
        self.assertEqual(neutron_fr2['ip_version'], 4)
        self.assertEqual(neutron_fr2['protocol'], 'icmp')
        self.assertEqual(fr2.get_service().get_protocol(), 'icmp')
        self.assertEqual(fr2.get_service().get_protocol_id(), 1)
