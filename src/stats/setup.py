"""
Setup script for setuptools.

Setup script for setuptolls to build python package of statistics client.
"""

from setuptools import find_packages, setup
from re import compile


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = compile(r'\s*#.*')
    return list(filter(bool, [c.sub('', y).strip() for y in lines]))


setup(
    name="contrail_stats_client",
    version="0.1dev",
    description="contrail statistics package.",
    packages=find_packages(),
    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),
    author="OpenContrail",
    author_email="dev@lists.tungsten.io",
    license="Apache Software License",
    url="https://tungsten.io/",
    test_suite='stats.tests',
    entry_points={
      "console_scripts": [
        "contrail-stats-client = stats.main:main"
        ]
    }
)
