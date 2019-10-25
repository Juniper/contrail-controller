#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import platform
import json

with open('ci_unittests.json') as f:
  ci_unittests = json.load(f)

default_target_list = ci_unittests['default']['scons_test_targets']
default_cplusplus_target_list = \
    ci_unittests['default_cplusplus']['scons_test_targets']

env = DefaultEnvironment()

SConscript(dirs=['lib', 'src'])

env['api_repo_path'] = '#/src/contrail-api-client'
SConscript(dirs=['../src/contrail-api-client'])

SConscript(dirs=['../src/contrail-analytics'])

env.Alias('controller/test', default_target_list)
env.Alias('controller/cplusplus_test', default_cplusplus_target_list)

env.Alias('controller/flaky-test', [
    'controller/src/agent:flaky-test',
    'controller/src/bfd:flaky-test',
    'controller/src/bgp:flaky-test',
    #'controller/src/config:test',
    'controller/src/db:flaky-test',
    'controller/src/dns:flaky-test',
    'controller/src/ifmap:flaky-test',
    'controller/src/xmpp:flaky-test',
    'controller/src/config/device-manager:flaky-test',
    'src/contrail-common/database/gendb:flaky-test',
    'src/contrail-analytics/contrail-collector:flaky-test',
])

env.Alias('test', [ 'controller/test' ])
env.Alias('flaky-test', [ 'controller/flaky-test' ])
