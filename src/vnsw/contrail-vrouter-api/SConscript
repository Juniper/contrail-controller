#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os

Import('BuildEnv')
env = BuildEnv.Clone()

sources = [
    'setup.py',
    'contrail_vrouter_api/__init__.py',
    'contrail_vrouter_api/vrouter_api.py',
    ]

cd_cmd = 'cd ' + Dir('.').path + ' && '
sdist_gen = env.Command('dist/contrail-vrouter-api-1.0.tar.gz',
                        sources,
                        cd_cmd + 'python setup.py sdist')

env.Alias('controller/src/vnsw/contrail_vrouter_api:sdist', sdist_gen)

if 'install' in BUILD_TARGETS:
    install_cmd = env.Command(None, sources,
                              cd_cmd + 'python setup.py install %s' %
                              env['PYTHON_INSTALL_OPT'])
    env.Alias('install', install_cmd)

# Local Variables:
# mode: python
# End:
