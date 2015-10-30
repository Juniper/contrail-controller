#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_config_incorrect',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_config_incorrect.main:ConfIncorrectAnalytics',
            'ObjectVRouter = alarm_config_incorrect.main:ConfIncorrectCompute',
            'ObjectConfigNode = alarm_config_incorrect.main:ConfIncorrectConfig',
            'ObjectBgpRouter = alarm_config_incorrect.main:ConfIncorrectControl',
            'ObjectDatabaseInfo = alarm_config_incorrect.main:ConfIncorrectDatabase',
        ],
    },
    zip_safe=False,
    long_description="ConfIncorrect Alarm"
)
