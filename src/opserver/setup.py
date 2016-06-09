#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import re
from setuptools import setup, find_packages, Command
import distutils


class RunTestsCommand(Command):
    description = "Test command to run testr in virtualenv"
    user_options = [
        ('coverage', 'c', "Generate code coverage report"),
        ('testrun=', None, "Run a specific test"),
        ]
    boolean_options = ['coverage']
    def initialize_options(self):
        self.cwd = None
        self.coverage = False
        self.testrun = None

    def finalize_options(self):
        self.cwd = os.getcwd()
        if self.testrun:
            self.announce('Running test: %s' % str(self.testrun),
                            level=distutils.log.INFO)

    def run(self):
        logfname = 'test.log'
        args = '-V'
        if self.coverage:
            logfname = 'coveragetest.log'
            args += ' -c'
        if self.testrun:
            logfname = self.testrun + '.log'
            args = self.testrun
        os.system('./run_tests.sh %s' % args)
        with open(logfname) as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))

setup(
    name='opserver',
    version='0.1dev',
    packages=find_packages(exclude=["node_mgr", "node_mgr.*", "test.*",
                                    "build.*", "plugins.*"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    include_package_data=True,
    long_description="VNC Analytics API Implementation",
    install_requires=requirements('requirements.txt'),
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
            'contrail-alarm-notify = opserver.alarm_notify:main'
        ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
