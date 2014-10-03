#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, Command

import os, sys

class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = []
    def initialize_options(self):
        self.cwd = None
    def finalize_options(self):
        self.cwd = os.getcwd()
    def run(self):
        os.system('./run_tests.sh -V')

setup(
    name='vnc_api',
    version='0.1dev',
    packages=['vnc_api',
              'vnc_api.gen',
              'vnc_api.common',
             ],
    long_description="VNC API Library Package",
    install_requires=[
        'requests>=1.1.0'
    ],
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
