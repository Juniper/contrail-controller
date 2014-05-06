#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup

setup(
    name='vnc_api',
    version='0.1dev',
    packages=['vnc_api',
              'vnc_api.gen',
              'vnc_api.common',
             ],
    long_description="VNC API Library Package",
    install_requires=[
        'requests>=1.1.0'
    ]
)
