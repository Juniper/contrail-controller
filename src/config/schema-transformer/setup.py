#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, find_packages, Command

setup(
    name='schema_transformer',
    version='0.1dev',
    packages=find_packages(exclude=["*.test", "*.test.*", "test.*", "test"]),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration Schema Transformer",
    entry_points = {
         # Please update sandesh/common/vns.sandesh on process name change
         'console_scripts' : [
             'contrail-schema = schema_transformer.to_bgp:server_main',
         ],
    },
    install_requires=[
        'jsonpickle'
    ],
)
