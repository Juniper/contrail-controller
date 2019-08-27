import re
from setuptools import setup, find_packages


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))


setup(
    name="contrail_stats_client",
    version="0.1dev",
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
        "contrail-stats-client = contrail_stats_client.main:main"
        ]
    }
)
