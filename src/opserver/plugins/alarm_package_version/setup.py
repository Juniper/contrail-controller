#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_pacakge_version',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectCollectorInfo = alarm_packageversion.main:PackageVersion',
            'ObjectVRouter = alarm_package_version.main:PackageVersion',
            'ObjectConfigNode = alarm_package_version.main:PackageVersion',
            'ObjectBgpRouter = alarm_package_version.main:PackageVersion',
            'ObjectDatabaseInfo = alarm_package_version.main:PackageVersion',
        ],
    },
    zip_safe=False,
    long_description="PackageVersion Alarm"
)
