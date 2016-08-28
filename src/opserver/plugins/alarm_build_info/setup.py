#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_build_info',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_build_info.main:BuildInfoNode',
            'ObjectVRouter = alarm_build_info.main:BuildInfoNode',
            'ObjectConfigNode = alarm_build_info.main:BuildInfoNode',
            'ObjectBgpRouter = alarm_build_info.main:BuildInfoNode',
            'ObjectDatabaseInfo = alarm_build_info.main:BuildInfoNode',
        ],
    },
    zip_safe=False,
    long_description="BuildInfo Alarm"
)
