#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, find_packages, Command
import os, re

class RunTestsCommand(Command):
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

setup(
    name='vnc_cfg_api_server',
    version='0.1dev',
    packages=find_packages(exclude=["*.tests", "*.tests.*", "tests.*", "tests"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration API Server Implementation",
    install_requires=[
        'lxml>=2.3.2',
        'gevent>=0.13.6',
        'geventhttpclient>=1.0a',
        'pycassa>=1.7.2',
        'netaddr>=0.7.5',
        'bitarray>=0.8.0',
        'psutil>=0.4.1',
    ],
    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-api = vnc_cfg_api_server.vnc_cfg_api_server:server_main',
            'contrail-db-check = vnc_cfg_api_server.db_manage:db_check',
        ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
