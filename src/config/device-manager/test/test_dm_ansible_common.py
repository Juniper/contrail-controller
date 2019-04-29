#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")

from test_case import DMTestCase
from vnc_api.vnc_api import *


class TestAnsibleCommonDM(DMTestCase):
    @classmethod
    def setUpClass(cls):
        dm_config_knobs = [
            ('DEFAULTS', 'push_mode', '1')
        ]
        super(TestAnsibleCommonDM, cls).setUpClass(
            dm_config_knobs=dm_config_knobs)
    # end setUpClass

    def create_job_template(self, name):
        job_template = JobTemplate(fq_name=[self.GSC, name], name=name,
                                   parent_type='global-system-config',
                                   display_name=name)
        self._vnc_lib.job_template_create(job_template)
        return job_template
    # end create_job_template

    def create_node_profile(self, name, vendor='juniper', device_family=None,
                            role_mappings=[], job_template=None):
        node_profile_role_mappings = [NodeProfileRoleType(
                                            physical_role=r.physical_role,
                                            rb_roles=r.rb_roles)
                                        for r in role_mappings]
        node_profile_roles = NodeProfileRolesType(
                                role_mappings=node_profile_role_mappings)
        node_profile = NodeProfile(fq_name=[self.GSC, name], name=name,
                                   parent_type='global-system-config',
                                   display_name=name,
                                   node_profile_vendor=vendor,
                                   node_profile_device_family=device_family,
                                   node_profile_roles=node_profile_roles)
        node_profile.set_job_template(job_template)
        self._vnc_lib.node_profile_create(node_profile)

        role_config = RoleConfig(fq_name=[self.GSC, name, 'basic'],
                                 parent_type='node-profile',
                                 name='basic',
                                 display_name='basic')
        self._vnc_lib.role_config_create(role_config)

        return node_profile, role_config
    # end create_node_profile
# end TestAnsibleDM
