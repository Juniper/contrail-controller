#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='database',
    version='0.1.dev0',
    packages=['database',
              'database.sandesh',
              'database.sandesh.database',
              'database.sandesh.database.process_info'
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="database package",
)
