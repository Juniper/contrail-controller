#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import setuptools, re
import os

class RunTestsCommand(setuptools.Command):
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

setuptools.setup(
    name='mesos_cni',
    version='0.1dev',
    packages=setuptools.find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml', '*.yml']},

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",

    long_description="Mesos CNI Plugin",

    test_suite='mesos_cni.tests',

    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-mesos-cni = mesos_cni.mesos_cni:main',
        ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
