#!/usr/bin/env python

PROJECT = 'ContrailConfigCli'

VERSION = '0.1'

from setuptools import setup, find_packages

import subprocess

import os,sys,inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0,parentdir) 

from entry_points import entry_points_dict

print "setting up contrail config cli"
config_entry_points_dict = dict()
config_entry_points_dict['console_scripts'] = []
config_entry_points_dict["contrailCli"] = []

def _populate_config_entry_points_dict():
    for key,values in entry_points_dict.iteritems():
        if key == "console_scripts":
            config_entry_points_dict['console_scripts'].append(entry_points_dict['console_scripts']['supervisord_config_files'])
        else:
            config_entry_points_dict["contrailCli"].append(values)

_populate_config_entry_points_dict()



setup( name=PROJECT,
        version=VERSION,
        description='Contrail Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff>=2.2.0'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=config_entry_points_dict,
        zip_safe=False,
    )

