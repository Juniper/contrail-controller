#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from ansible.module_utils.basic import AnsibleModule

__metaclass__ = type


ANSIBLE_METADATA = {'metadata_version': '1.1',
                    'status': ['preview'],
                    'supported_by': 'network'}

DOCUMENTATION = '''
---
'''
EXAMPLES = '''
'''
RETURN = '''
'''

oid_mapping = {
    "1.3.6.1.4.1.2636.1.1.1.4.82.8": {"vendor": "juniper",
                                      "family": "juniper-qfx",
                                      "product": "qfx5100"},
    "1.3.6.1.4.1.2636.1.1.1.2.29": {"vendor": "juniper",
                                    "family": "juniper-mx",
                                    "product": "mx240"},
}


def find_vendor_family(module):
    mapped_value = {}

    if module.params['oid'] in oid_mapping:
        mapped_value['host'] = module.params['host']
        mapped_value['hostname'] = module.params['hostname']
        mapped_value.update(oid_mapping[module.params['oid']])
    return mapped_value


def main():
    module = AnsibleModule(
        argument_spec=dict(
            oid=dict(required=True),
            host=dict(required=True),
            hostname=dict(required=True)
        ),
        supports_check_mode=True
    )
    mapped_value = find_vendor_family(module)

    output = {}
    output['oid_mapping'] = mapped_value

    module.exit_json(**output)


if __name__ == '__main__':
    main()
