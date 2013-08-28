#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='opserver',
    version='0.1dev',
    packages=['opserver',
              'opserver.sandesh',
              'opserver.sandesh.mirror',
              'opserver.sandesh.vns',
              'opserver.sandesh.viz',
              'opserver.sandesh.analytics_cpuinfo',
              'opserver.sandesh.analytics_cpuinfo.cpuinfo'
             ],
    package_data={'':['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC OpServer Implementation",
    install_requires=[
        'lxml',
        'gevent',
        'geventhttpclient',
        'redis',
        'xmltodict',
        'prettytable',
        'psutil==0.4.1'
    ]
)
