#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='vrouter',
    version='0.1dev',
    packages=['vrouter',
              'vrouter.vrouter', 
              'vrouter.vrouter.cpuinfo', 
              'vrouter.vrouter.process_info', 
              'vrouter.vrouter.derived_stats_results',
              'vrouter.loadbalancer',
              'vrouter.sandesh',
              'vrouter.sandesh.virtual_machine',
              'vrouter.sandesh.virtual_machine.port_bmap',
              'vrouter.sandesh.virtual_network',
              'vrouter.sandesh.flow',
              'vrouter.sandesh.interface',
              'vrouter.sandesh.interface.port_bmap',
             ],
    package_data={'':['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Vrouter Sandesh",
    install_requires=[
        'lxml',
        'gevent',
        'geventhttpclient',
        'redis',
        'xmltodict',
        'prettytable',
        'psutil>=0.6.0'
    ]
)
