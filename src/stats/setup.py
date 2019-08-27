from setuptools import find_packages, setup

setup(
    name="contrail_stats_client",
    version="0.1dev",
    description="contrail statistics package.",
    packages=find_packages(),
    author="OpenContrail",
    author_email="dev@lists.opencontrail.org",
    license="Apache Software License",
    url="http://www.opencontrail.org/",
    entry_points={
      "console_scripts": [
        "contrail-stats-client = contrail_stats_client.main:main"
        ]
    }
)
