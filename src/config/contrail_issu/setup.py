#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from setuptools import setup, find_packages


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))


setup(
    name='contrail_issu',
    version='0.1dev',
    packages=find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml', '*.yml']},
    zip_safe=False,
    long_description="VNC Contrail ISSU",

    test_suite='contrail_issu.test',

    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),

    entry_points={
         # Please update sandesh/common/vns.sandesh on process name change
         'console_scripts': [
             'contrail-issu-pre-sync = contrail_issu.issu_contrail_pre_sync:_issu_cassandra_pre_sync_main',
             'contrail-issu-run-sync = contrail_issu.issu_contrail_run_sync:_issu_rmq_main',
             'contrail-issu-post-sync = contrail_issu.issu_contrail_post_sync:_issu_cassandra_post_sync_main',
             'contrail-issu-zk-sync = contrail_issu.issu_contrail_zk_sync:_issu_zk_main',
         ],
    },
)
