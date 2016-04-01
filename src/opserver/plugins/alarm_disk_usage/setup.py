#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_disk_usage',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_disk_usage.main:DiskUsage',
            'ObjectVRouter = alarm_disk_usage.main:DiskUsage',
            'ObjectConfigNode = alarm_disk_usage.main:DiskUsage',
            'ObjectCollectorInfo = alarm_disk_usage.main:DiskUsage',
            'ObjectBgpRouter = alarm_disk_usage.main:DiskUsage',
        ],
    },
    zip_safe=False,
    long_description="DiskUsage Alarm"
)
