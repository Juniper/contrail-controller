#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import re
from setuptools import setup, find_packages, Command
from pip.req import parse_requirements


requirements = parse_requirements(
    os.path.join(os.path.dirname(os.path.abspath(__file__),
                 'requirements.txt')))


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
        os.system('./run_tests.sh %s' % args)
        with open(logfname) as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

setup(
    name='opserver',
    version='0.1dev',
    packages=find_packages(exclude=["node_mgr", "node_mgr.*", "test.*",
                                    "build.*", "plugins.*"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    include_package_data=True,
    long_description="VNC Analytics API Implementation",
    install_requires=[str(r.req) for r in reqs]
    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-analytics-api = opserver.opserver:main',
            'contrail-alarm-gen = opserver.alarmgen:main',
            'contrail-logs = opserver.log:main',
            'contrail-stats = opserver.stats:main',
            'contrail-flows = opserver.flow:main',
            'contrail-logs-api-audit = opserver.api_log:main',
            'contrail-db = opserver.db:main',
        ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
