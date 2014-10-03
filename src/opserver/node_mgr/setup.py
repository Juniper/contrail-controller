#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='node_mgr',
    version='0.1dev',
    packages=['analytics',
              'analytics.cpuinfo',
              'analytics.process_info' ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Analytics Node Manager Implementation",
    install_requires=[
        'gevent',
        'geventhttpclient',
        'psutil==0.4.1'
    ]
)
