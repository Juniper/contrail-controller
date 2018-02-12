#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
# Place holder setup.py for fabric-ansible

from setuptools import setup, find_packages, Command
import os
import re

class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = [
        ('coverage', 'c',
         "Generate code coverage report"),
        ]
    boolean_options = ['coverage']
    def initialize_options(self):
        self.cwd = None
        self.coverage = False
    def finalize_options(self):
        self.cwd = os.getcwd()
    def run(self):
        logfname = 'test.log'
        args = '-V'
        if self.coverage:
            logfname = 'coveragetest.log'
            args += ' -c'
        rc_sig = os.system('./run_tests.sh %s' % args)
        if rc_sig >> 8:
            os._exit(rc_sig>>8)
        with open(logfname) as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

setup(
    name='fabric_ansible_playbooks',
    version='0.1dev',
    packages=find_packages(exclude=["*.tests", "*.tests.*", "tests.*", "tests"]),
    package_data={'': ['*.html', '*.css', '*.xml', '*.*']},
    zip_safe=False,
    long_description="Fabric Ansible Playbooks",
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
