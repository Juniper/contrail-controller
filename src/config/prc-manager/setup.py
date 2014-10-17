#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup

setup(
    name='prc_manager',
    version='0.1dev',
    packages=['prc_manager',
              'prc_manager.sandesh',
              'prc_manager.sandesh.prcm_introspect',
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Physical Router Configuration Manager",
    entry_points = {
         # Please update sandesh/common/vns.sandesh on process name change
         'console_scripts' : [
             'contrail-prc-manager = prc_manager.prc_manager:server_main',
         ],
    },
)
