#!/usr/bin/env python

PROJECT = 'ContrailAnalyticsCli'

VERSION = '0.1'

from setuptools import setup, find_packages

from entry_points import entry_points_dict

setup( name=PROJECT,
        version=VERSION,
        description='Contrail Analytics Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff>=2.2.0'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=entry_points_dict,
        zip_safe=False,
    )

