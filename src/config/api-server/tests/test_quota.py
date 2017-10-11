#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import os
import sys
import socket
import errno
import uuid
import logging
import coverage

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
from lxml import etree
import inspect
import requests
import stevedore

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

    def test_create_vmi_with_quota_in_parallels(self, project=None):
        vn_name = 'test-net' + str(uuid.uuid4())
        if project is None:
            proj_name = 'admin' + self.id()
            kwargs = {'quota':{'virtual_machine_interface': self._port_quota}}
            project = Project(proj_name, **kwargs)
            self._vnc_lib.project_create(project)
        vn = VirtualNetwork(vn_name, project)
        self._vnc_lib.virtual_network_create(vn)
        vmi_list=[]
        for i in xrange(self._port_quota+1):
            vmi_list.insert(i,VirtualMachineInterface(str(uuid.uuid4()), project))
            vmi_list[i].uuid = vmi_list[i].name
            vmi_list[i].set_virtual_network(vn)
        result = {'ok': 0, 'exception': 0}
        def create_port(vmi):
            try:
                self._vnc_lib.virtual_machine_interface_create(vmi)
            except OverQuota:
                result['exception'] +=1
                return
            result['ok'] +=1
        threads_vmi = [gevent.spawn(create_port, vmi_list[i]) for i in xrange(self._port_quota+1)]
        gevent.joinall(threads_vmi)
        self.assertEqual(result['ok'], self._port_quota)
        self.assertEqual(result['exception'], 1)
        # Cleanup
        # Also test for zk counter delete functionality
        for vmi in vmi_list:
            try:
                self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
            except NoIdError:
                pass

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
    # end test_create_fip_with_quota_in_parallels

    def test_update_quota_and_create_resource_negative(self):
        proj_name = 'admin' + self.id()
        kwargs = {'quota':{'virtual_machine_interface': self._port_quota}}
        project = Project(proj_name, **kwargs)
        self._vnc_lib.project_create(project)
        # Creating 4 vmis should fail as the quota is set to 3
        self.test_create_vmi_with_quota_in_parallels(project=project)

        # Update quota of vmi from 3->4
        kwargs = {'virtual_machine_interface': 4}
        quota = QuotaType(**kwargs)
        project.set_quota(quota)
        self._vnc_lib.project_update(project)
        with ExpectedException(MismatchError) as e:
            # Creating 4 vmis should be a success now
            # Expect MismatchError on asserEqual(result['exception'], 1)
            self.test_create_vmi_with_quota_in_parallels(project=project)
    # end test_update_quota_and_create_resource_negative
# class TestQuota


class TestGlobalQuota(test_case.ApiServerTestCase):

    global_quotas = {'security_group_rule' : 2,
            'virtual_network': 5}

    def __init__(self, *args, **kwargs):
        super(TestGlobalQuota, self).__init__(*args, **kwargs)

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        extra_config_knobs = []
        for k, v in cls.global_quotas.items():
            extra_config_knobs.append(('QUOTA', k, v))
        kwargs.update({'extra_config_knobs' : extra_config_knobs})
        super(TestGlobalQuota, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestGlobalQuota, cls).tearDownClass(*args, **kwargs)

    def test_virtual_network_global_quota(self):
        vns = self._vnc_lib.virtual_networks_list()['virtual-networks']
        logger.info("Creating test VN object up to the quota limit.")
        for i in range(len(vns) + 1, self.global_quotas['virtual_network'] + 1):
            vn_name = 'testvn_%s_%s' % ( self.id(), i)
            self.create_virtual_network(vn_name, '%s.0.0.0/24' % i)
            logger.info("Created test VN: %s" % vn_name)

        logger.info("Creating one more VN object.")
        i = self.global_quotas['virtual_network'] + 1
        vn_name =  'testvn_%s_%s' % (self.id(), i)
        with ExpectedException(OverQuota) as e:
            self.create_virtual_network(vn_name, '%s.0.0.0/24' % i)
            logger.info("Created test VN: %s" % vn_name)

        vns = self._vnc_lib.virtual_networks_list()['virtual-networks']
        self.assertEqual(len(vns), self.global_quotas['virtual_network'])

    def test_security_group_rule_global_quota(self):
        logger.info("Creating security group with rules up to the quota limit.")
        sg_name = '%s-sg' % self.id()
        sg_obj = SecurityGroup(sg_name)
        self._vnc_lib.security_group_create(sg_obj)
        for i in range(1, self.global_quotas['security_group_rule'] + 1):
            rule = {'port_min': i,
                    'port_max': i,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, sg_rule)
        self._vnc_lib.security_group_update(sg_obj)

        logger.info("Creating one more security group rule object.")
        with ExpectedException(OverQuota) as e:
            port = self.global_quotas['security_group_rule'] + 1
            rule = {'port_min': port,
                    'port_max': port,
                    'direction': 'egress',
                    'ip_prefix': None,
                    'protocol': 'any',
                    'ether_type': 'IPv4'}
            sg_rule = self._security_group_rule_build(
                    rule, "default-domain:default-project:%s" % sg_name)
            self._security_group_rule_append(sg_obj, sg_rule)
            self._vnc_lib.security_group_update(sg_obj)
    #end TestGlobalQuota
