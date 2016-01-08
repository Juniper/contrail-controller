#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, Command

import os, sys, re

class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = [
        ('coverage', 'c',
         "Generate code coverage report"),
        ]
    boolean_options = ['coverage']
    def initialize_options(self):
        self.coverage = False
    def finalize_options(self):
        pass
    def run(self):
        logfname = 'test.log'
        args = '-V'
        if self.coverage:
            logfname = 'coveragetest.log'
            args += ' -c'
        os.system('./run_tests.sh %s' % args)
        with open(logfname) as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

setup(
    name='vnc_api',
    version='0.1dev',
    packages=['vnc_api',
              'vnc_api.gen',
              'vnc_api.gen.heat',
              'vnc_api.gen.heat.resources',
              'vnc_api.gen.heat.template',
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
