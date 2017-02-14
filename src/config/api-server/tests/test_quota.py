#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import os
import sys
import uuid
import logging
import coverage

import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException

from vnc_api.vnc_api import *
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
import test_utils
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestQuota(test_case.ApiServerTestCase):

    def __init__(self, *args, **kwargs):
        super(TestQuota, self).__init__(*args, **kwargs)
        self._port_quota = 3
        self._fip_quota = 3

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestQuota, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestQuota, cls).tearDownClass(*args, **kwargs)

    def test_create_vmi_with_quota_in_parallels(self):
        proj_name = 'admin' + self.id()
        vn_name = 'test-net'
        kwargs = {'quota':{'virtual_machine_interface': self._port_quota}}
        project = Project(proj_name, **kwargs)
        self._vnc_lib.project_create(project)
        vn = VirtualNetwork(vn_name, project)
        self._vnc_lib.virtual_network_create(vn)
        vmi=[]
        for i in xrange(self._port_quota+1):
            vmi.insert(i,VirtualMachineInterface(str(uuid.uuid4()), project))
            vmi[i].uuid = vmi[i].name
            vmi[i].set_virtual_network(vn)
        result = {'ok': 0, 'exception': 0}
        def create_port(vmi):
            try:
                self._vnc_lib.virtual_machine_interface_create(vmi)
            except OverQuota:
                result['exception'] +=1
                return
            result['ok'] +=1
        threads_vmi = [gevent.spawn(create_port, vmi[i]) for i in xrange(self._port_quota+1)]
        gevent.joinall(threads_vmi)
        self.assertEqual(result['ok'], self._port_quota)
        self.assertEqual(result['exception'], 1)

    def test_create_fip_with_quota_in_parallels(self):
        proj_name = 'admin' + self.id()
        vn_name = 'test-net'
        kwargs = {'quota':{'floating_ip': self._fip_quota}}
        project = Project(proj_name, **kwargs)
        self._vnc_lib.project_create(project)
        ipam_obj = NetworkIpam('ipam-%s' %(self.id()), project)
        self._vnc_lib.network_ipam_create(ipam_obj)
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        vn = VirtualNetwork('vn-%s' %(self.id()), project)
        vn.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        fip_pool_obj = FloatingIpPool(
            'fip-pool-%s' %(self.id()), parent_obj=vn)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip=[]
        for i in xrange(self._fip_quota+1):
            fip.append(FloatingIp(str(uuid.uuid4()), fip_pool_obj))
            fip[i].add_project(project)
            fip[i].uuid = fip[i].name
        result = {'ok': 0, 'exception': 0}
        def create_fip(fip):
            try:
                self._vnc_lib.floating_ip_create(fip)
            except OverQuota:
                result['exception'] +=1
                return
            result['ok'] +=1
        threads_fip = [gevent.spawn(create_fip, fip[i]) for i in xrange(self._fip_quota+1)]
        gevent.joinall(threads_fip)
        self.assertEqual(result['ok'], self._fip_quota)
        self.assertEqual(result['exception'], 1)
    # end test_example
# class TestPermissions
