#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_pending_compaction_tasks',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectDatabaseInfo = alarm_pending_compaction_tasks.main:PendingCompactionTasks',
        ],
    },
    zip_safe=False,
    long_description="Pending Compaction Tasks Alarm"
)
