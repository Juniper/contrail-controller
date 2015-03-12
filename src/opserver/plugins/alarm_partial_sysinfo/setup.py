#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_partial_sysinfo',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_partial_sysinfo.main:PartialSysinfo',
            'ObjectVRouter = alarm_partial_sysinfo.main:PartialSysinfo',
            'ObjectConfigNode = alarm_partial_sysinfo.main:PartialSysinfo',
            'ObjectBgpRouter = alarm_partial_sysinfo.main:PartialSysinfo',
        ],
    },
    zip_safe=False,
    long_description="PartialSysinfo Alarm"
)
