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
            'ObjectCollectorInfo = alarm_config_incorrect.main:ConfIncorrect',
            'ObjectVRouter = alarm_config_incorrect.main:ConfIncorrect',
            'ObjectConfigNode = alarm_config_incorrect.main:ConfIncorrect',
            'ObjectBgpRouter = alarm_config_incorrect.main:ConfIncorrect',
            'ObjectDatabaseInfo = alarm_config_incorrect.main:ConfIncorrect',
        ],
    },
    zip_safe=False,
    long_description="ConfIncorrect Alarm"
)
