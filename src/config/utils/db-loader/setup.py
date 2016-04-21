# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, find_packages


install_requires = [
    'pyyaml',
    'python-keystoneclient',
    'kazoo',
]

setup(
    name='contrail-db-loader',
    version='0.1b1',
    description="Script to load data in Contrail database for scaling tests",
    long_description=open('README.md').read(),
    author="Édouard Thuleau",
    author_email="ethuleau@juniper.net",
    packages=find_packages(),
    install_requires=install_requires,
    scripts=[],
    license="Apache Software License",
    entry_points={
        'console_scripts': [
            'contrail-db-loader = contrail_db_loader.main:main'
        ],
    },
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: Software Development :: User Interfaces',
        'License :: OSI Approved :: Apache Software License',
        'Programming Language :: Python :: 2.7',
    ],
    keywords='contrail db loader',
)
