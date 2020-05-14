"""
Setup script for setuptools.

Setup script for setuptolls to build python package of statistics client.
"""

import re

from setuptools import find_packages, setup


def requirements(filename):
    """Parse requirements file."""
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))


setup(
    name="contrail_stats_client",
    version="0.1dev0",
    description="contrail statistics package.",
    packages=find_packages(),
    zip_safe=False,
    author="OpenContrail",
    author_email="dev@lists.tungsten.io",
    license="Apache Software License",
    url="https://tungsten.io/",
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    test_suite='stats.tests',
    entry_points={
      "console_scripts": [
        "contrail-stats-client = stats.main:main"
        ]
    }
)
