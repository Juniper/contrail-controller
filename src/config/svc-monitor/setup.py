#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
# 

from setuptools import setup

setup(
    name='svc_monitor',
    version='0.1dev',
    packages=['svc_monitor',
              'svc_monitor.sandesh',
              'svc_monitor.sandesh.svc_mon_introspect',
              
             ],
    package_data={'':['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Service Monitor",
    install_requires=[
        'zc_zookeeper_static',
        'zope.interface',
    ]
)
