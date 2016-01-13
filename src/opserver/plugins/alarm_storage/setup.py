#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_storage',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectStorageClusterTable = alarm_storage.main:StorageClusterState',
        ],
    },
    zip_safe=False,
    long_description="Storage Cluster alarm"
)
