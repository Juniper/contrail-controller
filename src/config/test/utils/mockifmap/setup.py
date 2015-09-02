#!/usr/bin/env python

from distutils.core import setup

setup(name='mockifmap',
      version='1.0',
      description='IFMAP Distribution Utilities for systest',
      author='contrail',
      author_email='contrail-sw@juniper.net',
      url='http://opencontrail.org/',
      packages=['mockifmap', ],
      data_files=[('lib/python2.7/site-packages/mockifmap', ['ifmap.properties','basicauthusers.properties','publisher.properties']),],
     )
