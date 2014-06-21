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
    name='storage_stats',
    version='0.1dev',
    packages=setuptools.find_packages(),
    zip_safe=False,
    long_description="Storage Statistics",
    install_requires=requirements('requirements.txt'),
    test_suite='stats-daemon.tests',
    test_require=requirements('test-requirements.txt'),
)
