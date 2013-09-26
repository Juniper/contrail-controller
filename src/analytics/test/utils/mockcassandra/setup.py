#!/usr/bin/env python

from distutils.core import setup

setup(name='mockcassandra',
      version='1.0',
      description='Redis Distribution Utilities for systest',
      author='contrail',
      author_email='contrail-sw@juniper.net',
      url='http://opencontrail.org/',
      packages=['mockcassandra', ],
      data_files=[('lib/python2.7/site-packages/mockcassandra', ['apache-cassandra-1.1.7-bin.tar.gz', ]),],
     )
