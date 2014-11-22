#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, find_packages, Command
import os

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
    name='device_manager',
    version='0.1dev',
    packages=['device_manager',
              'device_manager.sandesh',
              'device_manager.sandesh.dm_introspect',
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Physical Router Configuration Manager",
    entry_points = {
         # Please update sandesh/common/vns.sandesh on process name change
         'console_scripts' : [
             'contrail-device-manager = device_manager.device_manager:server_main',
         ],
    },
    cmdclass={
       'run_tests': RunTestsCommand,
    },
)
