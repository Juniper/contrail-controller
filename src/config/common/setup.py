#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup
import re

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))

setup(
    name='cfgm_common',
    version='0.1dev',
    packages=['cfgm_common',
              'cfgm_common.ifmap',
              'cfgm_common.uve',
              'cfgm_common.uve.acl',
              'cfgm_common.uve.service_instance',
              'cfgm_common.uve.vnc_api',
              'cfgm_common.uve.virtual_machine',
              'cfgm_common.uve.virtual_network',
              'cfgm_common.uve.vrouter',
              'cfgm_common.uve.cfgm_cpuinfo',
              'cfgm_common.uve.cfgm_cpuinfo.cpuinfo',
              'cfgm_common.uve.cfgm_cpuinfo.connection_info'
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Common Utils",
    install_requires=requirements('requirements.txt'),
)
