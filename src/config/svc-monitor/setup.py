#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import setuptools

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    return lines

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
    tests_require=requirements('test-requirements.txt'),

    test_suite='svc_monitor.tests',

    entry_points = {
        'console_scripts' : [
            'contrail-svc-monitor = svc_monitor.svc_monitor:server_main',
        ],
    },
)
