#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_prouter_connectivity',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectPRouter = alarm_prouter_connectivity.main:ProuterConnectivity',
        ],
    },
    zip_safe=False,
    long_description="ProuterConnectivity alarm"
)
