#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_disk_usage_high',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_disk_usage_high.main:DiskUsageHigh',
            'ObjectVRouter = alarm_disk_usage_high.main:DiskUsageHigh',
            'ObjectConfigNode = alarm_disk_usage_high.main:DiskUsageHigh',
            'ObjectCollectorInfo = alarm_disk_usage_high.main:DiskUsageHigh',
            'ObjectBgpRouter = alarm_disk_usage_high.main:DiskUsageHigh',
        ],
    },
    zip_safe=False,
    long_description="High Disk usage Alarm"
)
