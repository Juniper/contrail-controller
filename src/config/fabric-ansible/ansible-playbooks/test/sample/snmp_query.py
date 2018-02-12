#!/usr/bin/python

# Copyright 2015 Patrick Ogenstad <patrick@ogenstad.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

DOCUMENTATION = '''
---

module: snmp_query
author: Patrick Ogenstad (@networklore)
short_description: Returns data from an SNMP query
description:
    - Returns vendor, os and version of an SNMP device.
requirements:
    - nelsnmp
options:
    host:
        description:
            - Typically set to {{ inventory_hostname }}
        required: true
    version:
        description:
            - SNMP Version to use, 2c or 3
        choices: [ '2c', '3' ]
        required: true
    community:
        description:
            - The SNMP community string, required if version is 2c
        required: false
    level:
        description:
            - Authentication level, required if version is 3
        choices: [ 'authPriv', 'authNoPriv' ]
        required: false
    username:
        description:
            - Username for SNMPv3, required if version is 3
        required: false
    integrity:
        description:
            - Hashing algoritm, required if version is 3
        choices: [ 'md5', 'sha' ]
        required: false
    authkey:
        description:
            - Authentication key, required if version is 3
        required: false
    privacy:
        description:
            - Encryption algoritm, required if level is authPriv
        choices: [ 'des', '3des', 'aes', 'aes192', 'aes256' ]
        required: false
    privkey:
        description:
            - Encryption key, required if version is authPriv
        required: false
    port:
        description:
            - SNMP port number
        default: 161
        required: false
    oid:
        description:
            - Oid or a list of oids to poll
        required: true
    query_type:
        description:
            - Type of SNMP query to use
        choices: [ 'get', 'getnext']
        default: 'get'
        required: false
'''

EXAMPLES = '''
# Get device info with SNMPv2
- snmp_query: host={{ inventory_hostname }} version=2c community=public oid=1.3.6.1.4.1.9.9.176.1.1.4.0

# Get device info with SNMPv3
- snmp_query:
    host: "{{ inventory_hostname }}"
    version: 3
    level: authPriv
    integrity: sha
    privacy: aes
    username: snmp-user
    authkey: abc12345
    privkey: def6789
    oid:
     - 1.3.6.1.4.1.9.9.176.1.1.4.0
     - 1.3.6.1.2.1.1.5.0
'''

from ansible.module_utils.basic import *

try:
    from nelsnmp.snmp import SnmpHandler
    from pysnmp.proto.rfc1905 import NoSuchObject
    has_nelsnmp = True
except:
    has_nelsnmp = False

NELSNMP_PARAMETERS = (
    'host',
    'community',
    'version',
    'level',
    'integrity',
    'privacy',
    'username',
    'authkey',
    'privkey',
    'port'
)


def main():
    module = AnsibleModule(
        argument_spec=dict(
            host=dict(required=True),
            version=dict(required=True, choices=['2c', '3']),
            community=dict(required=False, default=False),
            username=dict(required=False),
            level=dict(required=False, choices=['authNoPriv', 'authPriv']),
            integrity=dict(required=False, choices=['md5', 'sha']),
            port=dict(required=False, default=161, type='int'),
            privacy=dict(required=False, choices=[
                'des', '3des', 'aes', 'aes192', 'aes256']),
            authkey=dict(required=False),
            privkey=dict(required=False),
            oid=dict(required=True, type='list'),
            query_type=dict(choices=['get', 'getnext'], default='get'),
            timeout=dict(required=False, default=60)),
        required_together=(
            ['username', 'level', 'integrity', 'authkey'],
            ['privacy', 'privkey']
        ),
        supports_check_mode=False)

    m_args = module.params

    if not has_nelsnmp:
        module.fail_json(msg='Missing required nelsnmp module (check docs)')

    # Verify that we receive a community when using snmp v2
    if m_args['version'] == "2c":
        if m_args['community'] is False:
            module.fail_json(msg='Community not set when using snmp version 2')

    if m_args['version'] == "3":
        if m_args['username'] is None:
            module.fail_json(msg='Username not set when using snmp version 3')

        if m_args['level'] == "authPriv" and m_args['privacy'] is None:
            module.fail_json(msg='Privacy algorithm not set when using authPriv')

    nelsnmp_args = {}
    for key in m_args:
        if key in NELSNMP_PARAMETERS and m_args[key] is not None:
            nelsnmp_args[key] = m_args[key]

    try:
        dev = SnmpHandler(**nelsnmp_args)
    except Exception, err:
        module.fail_json(msg=str(err))

    results = {}

    if m_args['query_type'] == 'get':
        for oid in m_args['oid']:
            results[oid] = None
        try:
            varbinds = dev.get(*m_args['oid'])
        except Exception, err:
            module.fail_json(msg=str(err))
        for oid, value in varbinds:
            for desired_oid in m_args['oid']:
                if desired_oid in oid:
                    if isinstance(value, NoSuchObject):
                        results[desired_oid] = None
                    else:
                        results[desired_oid] = value
    else:
        try:
            vartable = dev.getnext(*m_args['oid'])
        except Exception, err:
            module.fail_json(msg=str(err))
        for varbinds in vartable:
            for oid, value in varbinds:
                results[oid] = value

    module.exit_json(**results)


main()
