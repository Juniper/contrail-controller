#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# Util to manage RBAC group and rules (add, delete etc)",
#
import argparse
import os
import re
import sys

example_usage = \
"""
 Examples:
 ---------
 Read RBAC group using UUID or FQN
 python rbacutil.py --uuid 'b27c3820-1d5f-4bfd-ba8b-246fefef56b0' --op read
 python rbacutil.py --name 'default-domain:default-api-access-list' --op read

"""

class RbacConfig():

    def parse_args(self):
        # Eg. python vnc_op.py VirtualNetwork
        # domain:default-project:default-virtual-network

        defaults = {
            'aaa_mode': None,
            'name': 'default-global-system-config:default-api-access-list'
        }

        parser = argparse.ArgumentParser(
            description="Util to manage RBAC group and rules",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog = example_usage)
        parser.add_argument(
            '--api_server_ip', help="API server address ",
            default = '127.0.0.1')
        parser.add_argument(
            '--admin_user',  help="Keystone User Name", default=None)
        parser.add_argument(
            '--admin_password',  help="Keystone User Password", default=None)
        parser.add_argument(
            '--admin_tenant_name',  help="Keystone Tenant Name", default=None)

        self.args = parser.parse_args()
        self.opts = vars(self.args)
    # end parse_args

rbac_config_obj = RbacConfig()
rbac_config_obj.parse_args()


rule_dict = {}
rule_list = []

with open('/etc/contrail/policy.json') as data_file:
    rule_dict = json.load(data_file)

    for key, val in rule_dict.items():
        print('key ->', key, 'value=', val)
  
#`      extend the name TODO  
#       Fixme

        rule_list = val.split(",")
        for rule in rule_list:
            print('---> rule', rule)
            os.system('python /opt/contrail/utils/rbacutil.py --server "' + server_ip + '" --name "' + key + '" --rule "' + rule + '" --op add-rule')


