#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, Command

import os, sys, re

class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = []
    def initialize_options(self):
        pass
    def finalize_options(self):
        pass
    def run(self):
        os.system('./run_tests.sh -V')
        with open('test.log') as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

setup(
    name='vnc_api',
    version='0.1dev',
    packages=['vnc_api',
              'vnc_api.gen',
             ],
    long_description="VNC API Library Package",
    install_requires=[
        'requests>=1.1.0'
    ],
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
