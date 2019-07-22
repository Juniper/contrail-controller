#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from setuptools import setup, find_packages, Command

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))


setup(
    name='schema_transformer',
    version='0.1dev',
    packages=find_packages(exclude=["*.test", "*.test.*", "test.*", "test"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Schema Transformer",
    entry_points = {
         # Please update sandesh/common/vns.sandesh on process name change
         'console_scripts' : [
             'contrail-schema = schema_transformer.to_bgp:server_main',
         ],
    },
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    test_suite='schema_transformer.tests'
)