#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

try:  # for pip >= 10
    from pip._internal.req import parse_requirements
except ImportError:  # for pip <= 9.0.3
    from pip.req import parse_requirements
from setuptools import setup, find_packages


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
    install_requires=[str(req.req) for req in parse_requirements('requirements.txt', session='hack')],
    tests_require=[str(req.req) for req in parse_requirements('test-requirements.txt', session='hack')],
    test_suite='cfgm_common.tests',
    keywords='contrail vnc utils',
)
