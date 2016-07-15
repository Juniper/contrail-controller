#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_node_status',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_node_status.main:NodeStatus',
            'ObjectVRouter = alarm_node_status.main:NodeStatus',
            'ObjectConfigNode = alarm_node_status.main:NodeStatus',
            'ObjectBgpRouter = alarm_node_status.main:NodeStatus',
            'ObjectDatabaseInfo = alarm_node_status.main:NodeStatus',
        ],
    },
    zip_safe=False,
    long_description="Node Status Alarm"
)
