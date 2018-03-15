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
This is to map the given SNMP OID with the vendor and family and check for
supoorted device family.

oid_to_vendor_family:
  oid: The snmp OID from snmp_facts module return patameter.
       ansible_facts.ansible_sysobjectid.
  host: IP address
  hostname: hostname of the IP/host from snmp_facts module return parameter.
            ansible_facts.ansible_sysname.
'''

EXAMPLES = '''
oid_to_vendor_family:
  oid: "1.3.6.1.4.1.2636.1.1.1.2.29"
  host: "10.155.67.7"
  hostname: "cloudcpe"
'''

RETURN = '''
Returns three parameters. The vendor,family and product for a given
host is returned back to the caller.
'''

oid_mapping = {
    "1.3.6.1.4.1.2636.1.1.1.4.82.8": {"vendor": "juniper",
                                      "family": "juniper-qfx",
                                      "product": "qfx5100"},
    "1.3.6.1.4.1.2636.1.1.1.4.82.5": {"vendor": "juniper",
                                      "family": "juniper-qfx",
                                      "product": "qfx5100"},
    "1.3.6.1.4.1.2636.1.1.1.2.29": {"vendor": "juniper",
                                    "family": "juniper-mx",
                                    "product": "mx240"},
    "1.3.6.1.4.1.2636.1.1.1.2.11": {"vendor": "juniper",
                                    "family": "juniper-mx",
                                    "product": "m10i"},
    "1.3.6.1.4.1.2636.1.1.1.2.57": {"vendor": "juniper",
                                    "family": "juniper-mx",
                                    "product": "mx80"}
}
_output = {'job_log_message': '', 'oid_mapping': {}}


def find_vendor_family(module):

    mapped_value = {}

    if module.params['oid'] in oid_mapping:
        mapped_value['host'] = module.params['host']
        mapped_value['hostname'] = module.params['hostname']
        mapped_value.update(oid_mapping[module.params['oid']])
        _output['job_log_message'] += "\nTask: OID MAPPING: " + \
            "vendor and product for the host: " + \
            mapped_value['host'] + " is " + str(mapped_value)
    else:
        _output['job_log_message'] += "\nTask: OID MAPPING: " + \
            "device with oid " + \
            module.params['oid'] + " NOT supported"

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

    _output['oid_mapping'] = mapped_value

    module.exit_json(**_output)


if __name__ == '__main__':
    main()
