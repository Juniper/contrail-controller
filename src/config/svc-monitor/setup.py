#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import setuptools, re
import os

class RunTestsCommand(setuptools.Command):
    description = "Test command to run testr in virtualenv"
    user_options = []
    def initialize_options(self):
        self.cwd = None
    def finalize_options(self):
        self.cwd = os.getcwd()
    def run(self):
        rc_sig = os.system('./run_tests.sh -V')
        if rc_sig >> 8:
            os._exit(rc_sig>>8)
        with open('test.log') as f:
            if not re.search('\nOK', ''.join(f.readlines())):
                os._exit(1)

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))

setuptools.setup(
    name='svc-monitor',
    version='0.1dev',
    packages=setuptools.find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml']},

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",

    long_description="VNC Service Monitor",

    install_requires=requirements('requirements.txt'),

    test_suite='svc_monitor.tests',

    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-svc-monitor = svc_monitor.svc_monitor:server_main',
        ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
