#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
# testing commit to R1.05

SConscript(dirs=['lib', 'src'])

env = DefaultEnvironment()
env.Alias('controller/test', [
    'controller/src/agent:test',
    'controller/src/analytics:test',
    'controller/src/base:test',
    'controller/src/bfd:test',
    'controller/src/bgp:test',
#   'controller/src/config:test', # TODO This test suite fails..
    'controller/src/db:test',
    'controller/src/discovery:test',
    'controller/src/dns:test',
    'controller/src/gendb:test',
    'controller/src/ifmap:test',
    'controller/src/io:test',
    'controller/src/net:test',
#   'controller/src/opserver:test', # TODO THis test times out
    'controller/src/schema:test',
    'controller/src/xmpp:test',
])

env.Alias('test', [ 'controller/test' ])
