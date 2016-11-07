#!/usr/bin/env python

PROJECT = 'ContrailAnalyticsCli'

VERSION = '0.1'

from setuptools import setup, find_packages

import os,sys,inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0,parentdir)

from entry_points import entry_points_dict

analytics_entry_points_dict = dict()
analytics_entry_points_dict['console_scripts'] = []

def _populate_analytics_entry_points_dict():
    for key,values in entry_points_dict.iteritems():
        if key == "console_scripts":
            if 'supervisord_analytics_files' in values.keys():
                analytics_entry_points_dict['console_scripts'].append(values['supervisord_analytics_files'])

_populate_analytics_entry_points_dict()



setup( name=PROJECT,
        version=VERSION,
        description='Contrail Analytics Command Line Interfae',
        platforms=['Any'],
        install_requires=['cliff>=2.2.0'],
        packages=find_packages(),
        package_data={'': ['*.html', '*.css', '*.xml']},
        include_package_data=True,
        entry_points=analytics_entry_points_dict,
        zip_safe=False,
    )

