#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='nodemgr',
    version='0.1dev',
    packages=['nodemgr'],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Nodemgr Implementation",
    entry_points={
        'console_scripts': [
            'contrail-nodemgr = nodemgr.main:main',
        ],
    },
)
