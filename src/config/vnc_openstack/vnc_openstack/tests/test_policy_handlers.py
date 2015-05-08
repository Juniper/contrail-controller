from vnc_openstack import policy_res_handler as policy_handler
from vnc_openstack.tests import test_common
from vnc_api import vnc_api


class TestPolicyHandlers(test_common.TestBase):
    def setUp(self):
        super(TestPolicyHandlers, self).setUp()
        self._handler = policy_handler.PolicyHandler(self._test_vnc_lib)

    def test_create(self):
        self._test_failures_on_create(null_entry=True, invalid_tenant=True)

        entries = [
            {'input': {'policy_q': {
                    'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                    'name': 'test-policy-1',
                    'entries': {'policy_rule': []}}},
             'output': {'id': self._generated(),
                        'entries': {'policy_rule': []},
                        'fq_name': ['default-domain',
                                    'default-project',
                                    'test-policy-1']}}]
        entries.append(
            {'input': {'policy_q': {
                'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                'name': 'test-policy-2',
                'entries': {
                    'policy_rule': [{
                        "protocol": "icmp",
                        "direction": ">",
                        "ethertype": "IPv4"}]}}},
             'output': {'id': self._generated(),
                        'entries': {
                            'policy_rule': [{
                                "protocol": "icmp",
                                "direction": ">",
                                "ethertype": "IPv4"}]},
                        'fq_name': ['default-domain',
                                    'default-project',
                                    'test-policy-2']}})
        self._test_check_create(entries)

        count_entries = [{'input': {
            'context': {
                'tenant_id': self._uuid_to_str(self.proj_obj.uuid)},
            'filters': None},
            'output': 2}]
        self._test_check_count(count_entries)

    def _create_policys_in_projects(self, proj_1, proj_2):
        create_q = {'tenant_id': self._uuid_to_str(proj_1.uuid),
                    'name': 'policy-1', 'entries': {}}
        exp_output = {'fq_name': ['default-domain', 'proj-1', 'policy-1']}
        self._test_check_create([{'input': {'policy_q': create_q},
                                  'output': exp_output}])

        create_q['tenant_id'] = proj_2.uuid
        create_q['name'] = 'policy-2'
        exp_output = {'fq_name': ['default-domain', 'proj-2', 'policy-2']}
        self._test_check_create([{'input': {'policy_q': create_q},
                                  'output': exp_output}])

    def test_list(self):
        self._test_failures_on_list(invalid_tenant=True)
        proj_1 = self._project_create(name='proj-1')
        proj_2 = self._project_create(name='proj-2')
        self._create_policys_in_projects(proj_1, proj_2)

        # with proj-1 tenant and non-admin context
        inp = {
            'context': {'tenant': self._uuid_to_str(proj_1.uuid),
                        'is_admin': False},
            'filters': {
                'tenant_id': [self._uuid_to_str(proj_1.uuid),
                              self._uuid_to_str(proj_2.uuid)]}}
        exp_output = [
            {'fq_name': ['default-domain', 'proj-1', 'policy-1']}]
        self._test_check_list([{'input': inp, 'output': exp_output}])

        # with proj-2 tenant and non-admin context
        inp['context']['tenant'] = self._uuid_to_str(proj_2.uuid)
        exp_output = [
            {'fq_name': ['default-domain', 'proj-2', 'policy-2']}]
        self._test_check_list([{'input': inp, 'output': exp_output}])

        # with admin context
        inp['context']['is_admin'] = True
        inp['filters']['tenant_id'].append('28349234')  # invalid uuid
        exp_output = [
            {'fq_name': ['default-domain', 'proj-1', 'policy-1']},
            {'fq_name': ['default-domain', 'proj-2', 'policy-2']}]
        self._test_check_list([{'input': inp, 'output': exp_output}])

        # without filters
        inp['filters'] = None
        self._test_check_list([{'input': inp, 'output': exp_output}])

        # with no context
        inp['context'] = None
        self._test_check_list([{'input': inp, 'output': exp_output}])

    def test_count(self):
        self._test_check_count([
            {'input': {'context': None, 'filters': None}, 'output': 0}])
        proj_1 = self._project_create(name='proj-1')
        proj_2 = self._project_create(name='proj-2')

        # with no policy created yet
        self._test_check_count([
            {'input': {'context': None,
                       'filters': {
                           'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                         self._uuid_to_str(proj_2.uuid)]}},
             'output': 0}])

        self._create_policys_in_projects(proj_1, proj_2)

        # count for both project filters
        self._test_check_count([
            {'input': {'context': None,
                       'filters': {
                           'tenant_id': [self._uuid_to_str(proj_1.uuid),
                                         self._uuid_to_str(proj_2.uuid)]}},
             'output': 2}])

        # count for one project filter
        self._test_check_count([
            {'input': {'context': None,
                       'filters': {
                           'tenant_id': [self._uuid_to_str(proj_2.uuid)]}},
             'output': 1}])

    def test_get(self):
        self._test_failures_on_get()
        policy_obj = vnc_api.NetworkPolicy('test-policy', self.proj_obj)
        self._test_vnc_lib.network_policy_create(policy_obj)

        entries = [{'input': str(policy_obj.uuid),
                    'output': {
                        'fq_name': ['default-domain',
                                    'default-project',
                                    'test-policy'],
                        'name': 'test-policy',
                        'id': str(policy_obj.uuid)}}]
        self._test_check_get(entries)

    def test_update(self):
        self._test_failures_on_update()
        # create a null entry policy
        entries = [
            {'input': {'policy_q': {
                'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
                'name': 'test-policy',
                'entries': {'policy_rule': []}}},
             'output': {'id': self._generated(),
                        'entries': {'policy_rule': []},
                        'fq_name': ['default-domain',
                                    'default-project',
                                    'test-policy']}}]
        self._test_check_create(entries)

        policys = self._test_vnc_lib.network_policys_list(
            parent_id=str(self.proj_obj.uuid))['network-policys']
        policy_uuid = policys[0]['uuid']

        # update the policy with a valid rule
        _q = {'tenant_id': self._uuid_to_str(self.proj_obj.uuid),
              'name': 'test-policy-2',
              'entries': {
                  'policy_rule': [{
                      "protocol": "icmp",
                      "direction": ">",
                      "ethertype": "IPv4"}]}}
        self._handler.resource_update(policy_uuid, _q)

        # check if the rule is updated or not
        entries = [{'input': str(policy_uuid),
                    'output': {
                        'fq_name': ['default-domain',
                                    'default-project',
                                    'test-policy'],
                        'name': 'test-policy',
                        'entries': {
                            'policy_rule': [{
                                "protocol": "icmp",
                                "direction": ">",
                                "ethertype": "IPv4"}]},
                        'id': str(policy_uuid)}}]
        self._test_check_get(entries)

    def test_delete(self):
        self._test_failures_on_delete()

        proj_1 = self._project_create(name='proj-1')
        proj_2 = self._project_create(name='proj-2')

        self._create_policys_in_projects(proj_1, proj_2)

        # count for both project filters
        entry = {'input': {
            'context': None,
            'filters': {
                'tenant_id': [self._uuid_to_str(proj_1.uuid),
                              self._uuid_to_str(proj_2.uuid)]}},
                 'output': 2}
        self._test_check_count([entry])

        policys = self._test_vnc_lib.network_policys_list()['network-policys']
        self.assertEqual(len(policys), 2)

        self._handler.resource_delete(policys[0]['uuid'])
        entry['output'] = 1
        self._test_check_count([entry])

        self._handler.resource_delete(policys[1]['uuid'])
        entry['output'] = 0
        self._test_check_count([entry])

        self._test_failures_on_delete(policys[1]['uuid'])
