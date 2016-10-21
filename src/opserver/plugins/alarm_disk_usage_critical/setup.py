#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_disk_usage_critical',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_disk_usage_critical.main:DiskUsageCritical',
            'ObjectVRouter = alarm_disk_usage_critical.main:DiskUsageCritical',
            'ObjectConfigNode = alarm_disk_usage_critical.main:DiskUsageCritical',
            'ObjectCollectorInfo = alarm_disk_usage_critical.main:DiskUsageCritical',
            'ObjectBgpRouter = alarm_disk_usage_critical.main:DiskUsageCritical',
        ],
    },
    zip_safe=False,
    long_description="Critical Disk usage Alarm"
)
