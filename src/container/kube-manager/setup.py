#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from setuptools import setup, find_packages
import sys

PY2 = sys.version_info.major < 3


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    result = list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))
    if PY2 and 'gevent<1.5.0' in result:
        # current UT doesn't work with gevent==1.4.0 for python2
        # and gevent==1.1.2 can't be used with python3
        # Apply this workaround as markers are not supported in requirements.txt
        result.remove('gevent<1.5.0')
        result.append('gevent==1.1.2')
    return result


setup(
    name='kube_manager',
    version='0.1dev',
    packages=find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml', '*.yml']},

    # metadata
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",

    long_description="Kubernetes Network Manager",

    test_suite='kube_manager.tests',

    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),

    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-kube-manager{} = kube_manager.kube_manager:main'.format("" if PY2 else "-py3"),
        ],
    },
)
