#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from setuptools import setup, find_packages


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))


setup(
    name='contrail-config-common',
    description="Contrail VNC Configuration Common Utils",
    long_description="Contrail VNC Configuration Common Utils",
    license='Apache-2',
    author='OpenContrail',
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
    package_data={'': ['*.xml']},
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    test_suite='cfgm_common.tests',
    keywords='contrail vnc utils',
)
