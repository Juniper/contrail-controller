#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='Control-Node',
    version='0.1dev',
    packages=['control_node',
              'control_node.control_node', 
              'control_node.control_node.cpuinfo',
              'control_node.control_node.ifmap_server_show',
              'control_node.control_node.process_info'
             ],
    package_data={'':['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Control Node Sandesh",
    install_requires=[
        'lxml',
        'gevent',
        'geventhttpclient',
        'redis',
        'xmltodict',
        'prettytable',
        'psutil==0.4.1'
    ]
)
