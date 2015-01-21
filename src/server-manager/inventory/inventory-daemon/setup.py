#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import setuptools

setuptools.setup(
    name='server-inventory-collector',
    version='0.1dev',
    packages=setuptools.find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Server Inventory",
)
