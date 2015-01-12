#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re

from setuptools import setup

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
        c = re.compile(r'\s*#.*')
        return filter(bool, map(lambda y: c.sub('', y).strip(), lines))

setup(
    name='discovery',
    version='0.1dev',
    packages=[
        'discovery',
        'discovery.sandesh',
        'discovery.sandesh.discovery_introspect',
    ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Discovery Server Implementation",
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    entry_points = {
        'console_scripts' : [
            'contrail-discovery = discovery.disc_server:server_main',
        ],
    },

    test_suite='discovery.tests'
)
