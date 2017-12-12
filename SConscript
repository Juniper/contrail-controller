#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

SConscript(dirs=['lib', 'src'])
SConscript(dirs=['../src/contrail-analytics'])

env = DefaultEnvironment()
env.Alias('controller/test', [
    'controller/src/agent:test',
    'controller/src/bfd:test',
    'controller/src/bgp:test',
    'controller/src/control-node:test',
    'controller/src/db:test',
    'controller/src/dns:test',
    'controller/src/database/gendb:test',
    'controller/src/ifmap:test',
    'controller/src/net:test',
    'controller/src/schema:test',
    'controller/src/xmpp:test',
    'controller/src/api-lib:test',
    'controller/src/config/api-server:test',
    'controller/src/config/schema-transformer:test',
    'controller/src/ksync:test',
    'src/contrail-analytics/contrail-collector:test',
    'src/contrail-analytics/contrail-opserver:test',
    'src/contrail-analytics/contrail-query-engine:test',
])

env.Alias('controller/flaky-test', [
    'controller/src/agent:flaky-test',
    'controller/src/bfd:flaky-test',
    'controller/src/bgp:flaky-test',
    #'controller/src/config:test',
    'controller/src/db:flaky-test',
    'controller/src/dns:flaky-test',
    'controller/src/database/gendb:flaky-test',
    'controller/src/database/cassandra/thrift:flaky-test',
    'controller/src/ifmap:flaky-test',
    'controller/src/xmpp:flaky-test',
    'controller/src/config/device-manager:flaky-test',
    'src/contrail-analytics/contrail-collector:flaky-test',
])

env.Alias('test', [ 'controller/test' ])
env.Alias('flaky-test', [ 'controller/flaky-test' ])
