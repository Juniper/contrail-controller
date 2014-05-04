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
              'opserver.sandesh.analytics_cpuinfo',
              'opserver.sandesh.analytics_cpuinfo.cpuinfo',
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
        'console_scripts' : [
            'contrail-analytics-api = opserver.opserver:main',
            'contrail-logs = opserver.log:main',
            'contrail-stats = opserver.stats:main',
            'contrail-flows = opserver.flow:main',
        ],
    },
)
