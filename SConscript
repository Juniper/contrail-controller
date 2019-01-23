#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import platform

env = DefaultEnvironment()

SConscript(dirs=['lib', 'src'])

env['api_repo_path'] = '#/src/contrail-api-client'
SConscript(dirs=['../src/contrail-api-client'])

if platform.system() != 'Windows':
    SConscript(dirs=['../src/contrail-analytics'])

env.Alias('controller/test', [
    'controller/src/config/api-server:test'
])

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
