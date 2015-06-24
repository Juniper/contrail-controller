#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import os, sys, re

from setuptools import setup, find_packages, Command


class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = []
    def initialize_options(self):
        self.cwd = None
    def finalize_options(self):
        self.cwd = os.getcwd()
    def run(self):
        os.system('./run_tests.sh -V')
        with open('test.log') as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

setup(
    name='vnc_openstack',
    version='0.1dev',
    packages=find_packages(),
    include_package_data=True,
    zip_safe=False,
    long_description="VNC openstack interface package",
    entry_points={
        'vnc_cfg_api.resync': [
            'xxx = vnc_openstack:OpenstackDriver',
        ],
        'vnc_cfg_api.resourceApi': [
            'xxx = vnc_openstack:ResourceApiDriver',
        ],
        'vnc_cfg_api.neutronApi': [
            'xxx = vnc_openstack:NeutronApiDriver',
        ],
    },
    install_requires=[
    ],
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
