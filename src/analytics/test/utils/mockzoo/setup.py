#!/usr/bin/env python

from distutils.core import setup

setup(name='mockzoo',
      version='1.0',
      description='Zookeeper Distribution Utilities for systest',
      author='contrail',
      author_email='contrail-sw@juniper.net',
      url='http://opencontrail.org/',
      packages=['mockzoo', ],
      data_files=[('lib/python2.7/site-packages/mockzoo', ['zookeeper-3.4.5.tar.gz', ]),],
     )
