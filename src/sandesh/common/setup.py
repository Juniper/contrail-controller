#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='sandesh-common',
    version='0.1dev',
    packages = [
        'sandesh_common',
        'sandesh_common.vns'
        ],
    package_data = {
        'sandesh_common.vns':['*.html', '*.css', '*.xml']
        }
)
