#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

PROJECT = 'ContrailCli'

VERSION = '0.1'

from setuptools import setup, find_packages

from entry_points import entry_points_dict

setup( name=PROJECT,
        version=VERSION,
        description='Contrail Command Line Interface',
        packages=find_packages(),
        platforms=['Any'],
        install_requires=['cliff'],
        entry_points=entry_points_dict,
        zip_safe=False,
    )

