#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_process_version',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_process_version.main:ProcessVersion',
            'ObjectVRouter = alarm_process_version.main:ProcessVersion',
            'ObjectConfigNode = alarm_process_version.main:ProcessVersion',
            'ObjectBgpRouter = alarm_process_version.main:ProcessVersion',
            'ObjectDatabaseInfo = alarm_process_version.main:ProcessVersion',
        ],
    },
    zip_safe=False,
    long_description="ProcessVersion Alarm"
)
