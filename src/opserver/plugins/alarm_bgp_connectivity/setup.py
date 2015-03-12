#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup, find_packages

setup(
    name='alarm_bgp_connectivity',
    version='0.1dev',
    packages=find_packages(),
    entry_points = {
        'contrail.analytics.alarms': [
            'ObjectBgpRouter = alarm_bgp_connectivity.main:BgpConnectivity',
        ],
    },
    zip_safe=False,
    long_description="BGPConnectivity alarm"
)
