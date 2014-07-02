#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='discovery',
    version='0.1dev',
    packages=[
        'discovery',
        'discovery.sandesh',
        'discovery.sandesh.discovery_introspect',
    ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Discovery Server Implementation",
    install_requires=[
        'gevent',
        'pycassa',
    ],
    entry_points = {
        'console_scripts' : [
            'discovery-server = discovery.disc_server:server_main',
        ],
    },
 
)
