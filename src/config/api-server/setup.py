# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License"); you may
#  not use this file except in compliance with the License. You may obtain
#  a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#  License for the specific language governing permissions and limitations
#  under the License.
#

import re
from setuptools import setup, find_packages


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))


setup(
    name='contrail-api-server',
    description="Contrail VNC Configuration API Server Implementation",
    long_description=open('README.md').read(),
    license='Apache-2',
    author='OpenContrail',
    author_email='dev@lists.opencontrail.org',
    url='http://www.opencontrail.org/documentation/api/r4.0/',
    version='0.1dev',
    classifiers=[
        'Environment :: OpenContrail',
        'Intended Audience :: Information Technology',
        'Intended Audience :: Developers',
        'Intended Audience :: System Administrators',
        'License :: OSI Approved :: Apache Software License',
        'Operating System :: POSIX :: Linux',
        'Development Status :: 5 - Production/Stable',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
    ],
    packages=find_packages(),
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    entry_points={
        'console_scripts': [
            'contrail-api = vnc_cfg_api_server.api_server:server_main',
            'contrail-db-check = vnc_cfg_api_server.db_manage:db_check',
        ],
    },
    keywords='contrail vnc api server',
    test_suite="vnc_cfg_api_server.tests"
)
