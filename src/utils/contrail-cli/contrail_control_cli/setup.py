#!/usr/bin/env python

PROJECT = 'ContrailControlCli'

VERSION = '0.1'

from setuptools import setup, find_packages

import os,sys,inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0,parentdir)

from entry_points import entry_points_dict

control_entry_points_dict = dict()
control_entry_points_dict['console_scripts'] = []
control_entry_points_dict["contrailCli"] = []

def _populate_control_entry_points_dict():
    for key,values in entry_points_dict.iteritems():
        if key == "console_scripts":
            control_entry_points_dict['console_scripts'].append(entry_points_dict['console_scripts']['supervisord_control_files'])
        else:
            control_entry_points_dict["contrailCli"].append(values)

_populate_control_entry_points_dict()

setup( name=PROJECT,
        version=VERSION,
        description='Contrail Control Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff>=2.2.0'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=control_entry_points_dict,
        zip_safe=False,
    )

