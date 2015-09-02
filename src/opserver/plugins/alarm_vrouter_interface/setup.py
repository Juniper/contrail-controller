#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_vrouter_interface',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectVRouter = alarm_vrouter_interface.main:VrouterInterface',
        ],
    },
    zip_safe=False,
    long_description="VrouterInterface alarm"
)
