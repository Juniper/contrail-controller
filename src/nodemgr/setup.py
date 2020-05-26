#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='nodemgr',
    version='0.1dev',
    install_requires=[
        'pyyaml',
        'psutil>=0.6.0,!=5.5.0,!=5.5.1',
    ],
    packages=['nodemgr',
              'nodemgr.analytics_nodemgr',
              'nodemgr.analytics_snmp_nodemgr',
              'nodemgr.analytics_alarm_nodemgr',
              'nodemgr.control_nodemgr',
              'nodemgr.config_nodemgr',
              'nodemgr.analytics_database_nodemgr',
              'nodemgr.config_database_nodemgr',
              'nodemgr.vrouter_nodemgr',
              'nodemgr.common',
              'nodemgr.common.cri',
              'nodemgr.common.sandesh',
              'nodemgr.common.sandesh.nodeinfo',
              'nodemgr.common.sandesh.nodeinfo.cpuinfo',
              'nodemgr.common.sandesh.nodeinfo.process_info',
              'nodemgr.common.sandesh.supervisor_events'],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Nodemgr Implementation",
    entry_points={
        'console_scripts': [
            'contrail-nodemgr = nodemgr.main:main',
        ],
    },
)
