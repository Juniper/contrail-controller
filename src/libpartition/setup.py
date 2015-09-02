#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='libpartition',
    version='0.1dev',
    packages=[
        'libpartition',
    ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Partition Library Implementation",
    install_requires=[
        'gevent',
        'pycassa',
        'kazoo',
        'consistent_hash'
    ]
)
