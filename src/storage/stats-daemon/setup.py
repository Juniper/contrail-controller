#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='storage_stats',
    version='0.1dev',
    packages=['storage_stats',
              'storage_stats.sandesh',
              'storage_stats.sandesh.storage',

              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Storage Statistics",
    install_requires=[
        'gevent',
    ],
)
