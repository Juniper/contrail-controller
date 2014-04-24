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
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Service Monitor",
    install_requires=[
        'zope.interface',
    ],
    entry_points = {
        'console_scripts' : [
            'contrail-svc-monitor = svc_monitor.svc_monitor:server_main',
        ],
    },
)
