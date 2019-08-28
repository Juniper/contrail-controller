#
# Copyright (c) 2016 Juniper Networks, Inc.
#

import setuptools

setuptools.setup(
    name='contrail-vrouter-api',
    version='1.0',
    install_requires=[
        'future',
    ],
    packages=setuptools.find_packages(),

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",
)
