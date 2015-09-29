#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import re, setuptools, os, distutils
from setuptools import setup, find_packages, Command

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

setuptools.setup(
        name='contrail_topology',
        version='0.1.0',
        description='contrail topology package.',
        long_description=open('README.txt').read(),
        packages=setuptools.find_packages(),

        # metadata
        author="OpenContrail",
        author_email="dev@lists.opencontrail.org",
        license="Apache Software License",
        url="http://www.opencontrail.org/",

        install_requires=requirements('requirements.txt'),
        entry_points = {
          'console_scripts' : [
            'contrail-topology = contrail_topology.main:main',
            ],
        },
        cmdclass={
           'run_tests': RunTestsCommand,
        },
    )

