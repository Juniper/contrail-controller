#
# Copyright (c) 2017 Juniper Networks, Inc.
#

import setuptools

setuptools.setup(
    name='contrail-vrouter-provisioning',
    version='0.1dev',
    install_requires=[
        'future',
    ],
    packages=setuptools.find_packages(),

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",
    long_description="Contrail compute provisioning module",
    entry_points={
        'console_scripts': [
            'contrail-compute-setup = contrail_vrouter_provisioning.setup:main',
            'contrail-toragent-setup = contrail_vrouter_provisioning.toragent.setup:main',
            'contrail-toragent-cleanup = contrail_vrouter_provisioning.toragent.cleanup:main',
            ],
    },
)
