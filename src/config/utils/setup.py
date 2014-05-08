#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup

setup(
    name='contrail_utils',
    version='0.1dev',
    author='contrail',
    author_email='contrail-sw@juniper.net',
    url='http://opencontrail.org/',
    packages=['contrail_utils',
              ],
    long_description="Contrail Utility scripts",
    entry_points = {
        'console_scripts': [
            'contrail_provision_vrouter = contrail_utils.provision_vrouter:main',
            'contrail_provision_linklocal = contrail_utils.provision_linklocal:main',
            'contrail_create_floating_pool = contrail_utils.create_floating_pool:main',
            'contrail_veth_port = contrail_utils.contrail_veth_port:main',
            ],
        },
    )
