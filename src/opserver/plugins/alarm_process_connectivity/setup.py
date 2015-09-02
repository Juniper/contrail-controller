#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_process_connectivity',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_process_connectivity.main:ProcessConnectivity',
            'ObjectVRouter = alarm_process_connectivity.main:ProcessConnectivity',
            'ObjectConfigNode = alarm_process_connectivity.main:ProcessConnectivity',
            'ObjectBgpRouter = alarm_process_connectivity.main:ProcessConnectivity',
            'ObjectDatabaseInfo = alarm_process_connectivity.main:ProcessConnectivity',
        ],
    },
    zip_safe=False,
    long_description="ProcessConnectivity alarm"
)
