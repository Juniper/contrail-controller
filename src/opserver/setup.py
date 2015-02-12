#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='opserver',
    version='0.1dev',
    packages=find_packages(exclude=["node_mgr", "node_mgr.*", "test.*",
                                    "build.*", "plugins.*"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    include_package_data=True,
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
            'contrail-alarm-gen = opserver.alarmgen:main',
            'contrail-logs = opserver.log:main',
            'contrail-stats = opserver.stats:main',
            'contrail-flows = opserver.flow:main',
            'contrail-logs-api-audit = opserver.api_log:main',
        ],
    },
)
