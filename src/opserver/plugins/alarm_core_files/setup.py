#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_core_files',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_core_files.main:CoreFiles',
            'ObjectVRouter = alarm_core_files.main:CoreFiles',
            'ObjectConfigNode = alarm_core_files.main:CoreFiles',
            'ObjectCollectorInfo = alarm_core_files.main:CoreFiles',
            'ObjectBgpRouter = alarm_core_files.main:CoreFiles',
        ],
    },
    zip_safe=False,
    long_description="CoreFiles Alarm"
)
