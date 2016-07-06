#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_phyif_bandwidth',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectVRouter = alarm_phyif_bandwidth.main:PhyifBandwidth',
        ],
    },
    zip_safe=False,
    long_description="PhyifBandwidth alarm"
)
