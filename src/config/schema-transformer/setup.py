#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
import sys

import setuptools


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    result = list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))
    if sys.version_info.major < 3:
        # current UT doesn't work with gevent==1.4.0 for python2
        # and gevent==1.1.2 can't be used with python3
        # Apply this workaround as markers are not supported in requirements.txt
        result.remove('gevent<1.5.0')
        result.append('gevent==1.1.2')


setuptools.setup(
    name='schema_transformer',
    version='0.1dev',
    packages=setuptools.find_packages(
        exclude=["*.test", "*.test.*", "test.*", "test"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Schema Transformer",
    entry_points={
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts': [
            'contrail-schema = schema_transformer.to_bgp:server_main',
        ],
    },
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    test_suite='schema_transformer.tests'
)
