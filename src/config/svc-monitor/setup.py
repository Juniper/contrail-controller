#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import setuptools, re


setuptools.setup(
    name='svc_monitor',
    version='0.1dev',
    packages=setuptools.find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml', '*.yml']},
    install_requires=[
        'bitarray',
        'contrail-api-client',
        'contrail-config-common',
        'future',
        'kazoo',
        'kombu',
        'requests >= 2.20.0',
        'sandesh==0.1dev',
        'sandesh-common',
        'six'
    ],

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",

    long_description="VNC Service Monitor",

    test_suite='svc_monitor.tests',


    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-svc-monitor = svc_monitor.svc_monitor:server_main',
        ],
    },
)
