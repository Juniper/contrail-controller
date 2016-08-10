#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_pending_cassandra_compaction_tasks',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_pending_cassandra_compaction_tasks.main:PendingCassandraCompactionTasks',
        ],
    },
    zip_safe=False,
    long_description="Pending Compaction Tasks Alarm"
)
