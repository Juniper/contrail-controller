#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='opserver',
    version='0.1dev',
    packages=['opserver',
              'opserver.sandesh',
              'opserver.sandesh.viz',
              'opserver.sandesh.analytics',
              'opserver.sandesh.analytics.process_info',
              'opserver.sandesh.analytics.cpuinfo',
              'opserver.sandesh.analytics_database',
              'opserver.sandesh.redis',
              'opserver.sandesh.discovery'],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Analytics API Implementation",
    install_requires=[
        'lxml',
        'gevent',
        'geventhttpclient',
        'redis',
        'xmltodict',
        'prettytable',
        'psutil>=0.4.1'
    ],
    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-analytics-api = opserver.opserver:main',
            'contrail-logs = opserver.log:main',
            'contrail-stats = opserver.stats:main',
            'contrail-flows = opserver.flow:main',
        ],
    },
)
