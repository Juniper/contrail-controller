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
              'cfgm_common.uve',
              'cfgm_common.uve.acl',
              'cfgm_common.uve.service_instance',
              'cfgm_common.uve.vnc_api',
              'cfgm_common.uve.virtual_machine',
              'cfgm_common.uve.virtual_network',
              'cfgm_common.uve.physical_router',
              'cfgm_common.uve.cfgm_cpuinfo',
              'cfgm_common.uve.greenlets',
              'cfgm_common.uve.msg_traces',
              'cfgm_common.uve.nodeinfo',
              'cfgm_common.uve.nodeinfo.cpuinfo',
              'cfgm_common.uve.nodeinfo.process_info',
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Common Utils",
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    test_suite='tests.test_suite',
)
