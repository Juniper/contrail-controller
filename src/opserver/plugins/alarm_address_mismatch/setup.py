#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_address_mismatch',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectVRouter = alarm_address_mismatch.main:AddressMismatchCompute',
            'ObjectBgpRouter = alarm_address_mismatch.main:AddressMismatchControl',
        ],
    },
    zip_safe=False,
    long_description="AddressMismatch Alarm"
)
