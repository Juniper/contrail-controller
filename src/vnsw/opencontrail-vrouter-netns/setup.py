#
# Copyright (c) 2014 Juniper Networks, Inc.
#

import setuptools


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    return lines

setuptools.setup(
    name='opencontrail-vrouter-netns',
    version='0.1',
    packages=setuptools.find_packages(),

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",
    long_description="Script to manage Linux network namespaces",

    install_requires=requirements('requirements.txt'),
#    dependency_links = [
#        "http://github.com/Juniper/contrail-controller/tarball/master/src/vnsw/contrail-vrouter-api#egg=contrail-vrouter-api-1.0",
#    ],
#    test_suite='opencontrail_vrouter_netns.tests',
#    tests_require=requirements('test-requirements.txt'),

    entry_points = {
        'console_scripts': [
            'opencontrail-vrouter-netns = opencontrail_vrouter_netns.vrouter_netns:main'
        ],
    },
)
