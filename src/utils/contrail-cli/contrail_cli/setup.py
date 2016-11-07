#!/usr/bin/env python

PROJECT = 'ContrailCli'

VERSION = '0.1'

from setuptools import setup, find_packages

import os,sys,inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0,parentdir)

from entry_points import entry_points_dict

cli_entry_points_dict = dict()
cli_entry_points_dict["ContrailCli"] = []

def _populate_cli_entry_points_dict():
    for key, values in entry_points_dict.iteritems():
        if key != "console_scripts":
            cli_entry_points_dict["ContrailCli"].append(values)

_populate_cli_entry_points_dict()



setup( name=PROJECT,
        version=VERSION,
        description='Contrail Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff>=2.2.0'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=cli_entry_points_dict,
        zip_safe=False,
    )

