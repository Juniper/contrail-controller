import sys
import uuid

from gevent import monkey
monkey.patch_all()  # noqa
from mock import patch
from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import NoIdError
from vnc_api.vnc_api import ApplicationPolicySet
from vnc_api.vnc_api import FirewallPolicy
from vnc_api.vnc_api import Project

sys.path.append('../common/tests')
import test_case
from vnc_openstack.neutron_plugin_db import _NEUTRON_FIREWALL_APP_TAG_PREFIX
from test_utils import FakeExtensionManager


class TestFirewallGroup(test_case.NeutronBackendTestCase):
    def setUp(self):
        super(TestFirewallGroup, self).setUp()
        self.project_id = self._vnc_lib.project_create(
            Project('project-%s' % self.id()))
        self.project = self._vnc_lib.project_read(id=self.project_id)

    def _get_tag_fq_name(self, firewall_group, project=None):
        if (not project and not 'project_id' in firewall_group and
                not 'tenant_id' in firewall_group):
            return

        if not project:
            project_id = str(uuid.UUID(firewall_group.get(
                'project_id', firewall_group['tenant_id'])))
            project = self._vnc_lib.project_read(id=project_id)

        return project.fq_name + [
            'application=%s%s' % (
                _NEUTRON_FIREWALL_APP_TAG_PREFIX, firewall_group['id'])]

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

    def test_dedicated_tag_deleted(self):
        neutron_fg = self.create_resource('firewall_group', self.project_id)

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

        self.assertFalse(self._vnc_lib.tags_list(
            parent_id=self.project_id)['tags'])
        apss = self._vnc_lib.application_policy_sets_list(
            parent_id=self.project_id)['application-policy-sets']
        # Only default project APS remains
        self.assertEquals(len(apss), 1)
        self.assertEquals(apss[0]['fq_name'][-1],
                          ApplicationPolicySet(parent_type='project').name)

    def test_aps_cleaned_if_associate_tag_fails(self):
        with patch.object(self.neutron_db_obj._vnc_lib, 'set_tag',
                          side_effect=BadRequest(400, "Fake bad request")):
            self.create_resource(
                'firewall_group', self.project_id, status='400 Bad Request')

        self.assertFalse(self._vnc_lib.tags_list(
            parent_id=self.project_id)['tags'])
        apss = self._vnc_lib.application_policy_sets_list(
            parent_id=self.project_id)['application-policy-sets']
        # Only default project APS remains
        self.assertEquals(len(apss), 1)
        self.assertEquals(apss[0]['fq_name'][-1],
                          ApplicationPolicySet(parent_type='project').name)

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

        self.update_resource(
            'firewall_group',
            neutron_fg['id'],
            self.project_id,
            extra_res_fields={
                'egress_firewall_policy_id': None,
            },
        )
        neutron_fg = self.read_resource('firewall_group', neutron_fg['id'])
        self.assertNotIn('ingress_firewall_policy_id', neutron_fg)
        self.assertNotIn('egress_firewall_policy_id', neutron_fg)
