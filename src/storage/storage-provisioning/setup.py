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
    name='storage-provisioning',
    version='0.1dev',
    packages=setuptools.find_packages(),
    zip_safe=False,
    long_description="Storage Provisioning",
    install_requires=requirements('requirements.txt'),
    entry_points = {
        'console_scripts' : [
            'contrail-storage-provisioning = contrail_storage_provisioning.ceph_provision:main',
        ],
    },

)
