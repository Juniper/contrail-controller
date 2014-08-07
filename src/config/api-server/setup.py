#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from setuptools import setup, find_packages

setup(
    name='vnc_cfg_api_server',
    version='0.1dev',
    packages=find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="VNC Configuration API Server Implementation",
    install_requires=[
        'lxml>=2.3.2',
        'gevent>=0.13.6',
        'geventhttpclient>=1.0a',
        'pycassa>=1.7.2',
        'netaddr>=0.7.5',
        'bitarray>=0.8.0',
        'psutil>=0.4.1',
    ],
    entry_points = {
        # Please update sandesh/common/vns.sandesh on process name change
        'console_scripts' : [
            'contrail-api = vnc_cfg_api_server.vnc_cfg_api_server:server_main',
        ],
    },
)
