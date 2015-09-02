#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup
import setuptools

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    return lines


setup(
    name='stats-daemon',
    version='0.1dev',
    packages=setuptools.find_packages(),
    zip_safe=False,
    long_description="Storage Statistics",
    install_requires=requirements('requirements.txt'),
    test_suite='stats_daemon.tests',
    tests_require=requirements('test-requirements.txt'),
    entry_points = {
        'console_scripts' : [
            'contrail-storage-stats = stats_daemon.storage_nodemgr:main',
        ],
    },

)
