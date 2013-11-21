#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='discoveryclient',
    version='0.1dev',
    packages=[
        'discoveryclient',
    ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Discovery Client Implementation",
    install_requires=[
        'gevent',
        'pycassa',
    ]
)
