#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import platform

SConscript(dirs=['lib', 'src'])

env = DefaultEnvironment()
env.Alias('controller/test', [
    'controller/src/agent:test',
    'controller/src/analytics:test',
    'controller/src/base:test',
    'controller/src/bfd:test',
    'controller/src/bgp:test',
    'controller/src/control-node:test',
    'controller/src/db:test',
    'controller/src/discovery:test',
    'controller/src/dns:test',
    'controller/src/gendb:test',
    'controller/src/ifmap:test',
    'controller/src/io:test',
    'controller/src/net:test',
    'controller/src/opserver:test',
    'controller/src/query_engine:test',
    'controller/src/schema:test',
    'controller/src/xmpp:test',
    'controller/src/api-lib:test',
    'controller/src/config/api-server:test',
    'controller/src/config/schema-transformer:test',
])

env.Alias('controller/flaky-test', [
    'controller/src/agent:flaky-test',
    'controller/src/analytics:flaky-test',
    'controller/src/base:flaky-test',
    'controller/src/bfd:flaky-test',
    'controller/src/bgp:flaky-test',
#   'controller/src/config:test',
    'controller/src/db:flaky-test',
    'controller/src/dns:flaky-test',
    'controller/src/gendb:flaky-test',
    'controller/src/ifmap:flaky-test',
    'controller/src/io:flaky-test',
    'controller/src/xmpp:flaky-test',
    'controller/src/config/device-manager:flaky-test',
])

env.Alias('test', [ 'controller/test' ])
env.Alias('flaky-test', [ 'controller/flaky-test' ])

if platform.machine().startswith('arm'):
    env.Append(CCFLAGS = ['-DTBB_USE_GCC_BUILTINS=1', '-D__TBB_64BIT_ATOMICS=0'])
