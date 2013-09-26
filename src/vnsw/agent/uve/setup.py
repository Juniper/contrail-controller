#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='vrouter',
    version='0.1dev',
    packages=['vrouter',
              'vrouter.vrouter', 
              'vrouter.sandesh',
              'vrouter.vrouter.cpuinfo',
              'vrouter.sandesh.vns'
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
        'psutil==0.4.1'
    ]
)
