#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup
import setuptools
import re

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return (filter(bool, map(lambda y: c.sub('', y).strip(), lines)))

setup(
    name='ironic-notification-manager',
    version='0.1dev',
    packages=setuptools.find_packages(),
    zip_safe=False,
    long_description="Ironic Node Notification Management Daemon",
    install_requires=requirements('requirements.txt'),
    entry_points = {
         'console_scripts' : [
             'ironic-notification-manager = ironic_notification_manager.ironic_notification_manager:server_main',
         ],
    },
)
