#!/usr/bin/env python

from distutils.core import setup

setup(name='mockredis',
      version='1.0',
      description='Redis Distribution Utilities for systest',
      author='contrail',
      author_email='contrail-sw@juniper.net',
      url='http://opencontrail.org/',
      packages=['mockredis', ],
      data_files=[('lib/python2.7/site-packages/mockredis', ['redis.24.conf', 'redis.26.conf']),],
     )
