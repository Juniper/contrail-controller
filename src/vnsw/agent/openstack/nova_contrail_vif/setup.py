#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from setuptools import setup

setup(
    name='nova_contrail_vif',
    version='0.1dev',
    packages=['nova_contrail_vif',
              'nova_contrail_vif.gen_py',
              'nova_contrail_vif.gen_py.instance_service',
              ],
    package_data={'': ['*.html', '*.css', '*.xml']},
    zip_safe=False,
    long_description="Contrail nova vif plugin",
)
