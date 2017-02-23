#
# Copyright (c) 2014 Juniper Networks, Inc.
#

import setuptools


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    return lines

setuptools.setup(
    name='contrail-vrouter-api',
    version='1.0',
    packages=setuptools.find_packages(),

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",

    install_requires=requirements('requirements.txt'),

    test_suite='contrail_vrouter_api.tests',
    tests_require=requirements('test-requirements.txt'),
)
