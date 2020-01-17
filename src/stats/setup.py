"""
Setup script for setuptools.

Setup script for setuptolls to build python package of statistics client.
"""

from setuptools import find_packages, setup


setup(
    name="contrail_stats_client",
    version="0.1dev",
    description="contrail statistics package.",
    packages=find_packages(),
    author="OpenContrail",
    author_email="dev@lists.tungsten.io",
    license="Apache Software License",
    url="https://tungsten.io/",
    test_suite='stats.tests',
    install_requires=['six'],
    entry_points={
      "console_scripts": [
        "contrail-stats-client = stats.main:main"
        ]
    }
)
