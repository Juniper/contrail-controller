#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import copy
import types
import uuid
import sys
import StringIO
import string
import unittest
from netaddr import *

import test_common

from vnc_addr_mgmt import *
from vnc_api.gen.resource_common import *
from vnc_api.gen.resource_xsd import *

# convert object to dictionary representation


def todict(obj):
    if isinstance(obj, dict):
        obj_dict = {}
        for k in obj.keys():
            obj_dict[k] = todict(obj[k])
    elif hasattr(obj, "__iter__"):
        obj_dict = [todict(v) for v in obj]
    elif hasattr(obj, "__dict__"):
        obj_dict = dict([(key, todict(value))
                         for key, value in obj.__dict__.iteritems()
                         if not callable(value)])
    else:
        obj_dict = obj
    return obj_dict


class Db():

    def __init__(self):
        self.db = {}
    # end __init__

    def subnet_store(self, key, value):
        self.db[key] = value
    # end subnet_store

    def subnet_retrieve(self, key):
        if key in self.db:
            return self.db[key]
        else:
            return None
    # end subnet_retrieve

    def subnet_delete(self, key):
        del self.db[key]
    # end subnet_delete
# end Db


class TestIp(unittest.TestCase):

    """
    vn_obj_dict = {}
    vn_obj_dict['_fq_name'] = ['default-domain', 'default-project',
                               'dss-virtual-network-1']
    vn_obj_dict['_network_ipam_refs'] = \
            [
                {
                'to': ['default-domain', 'dss-project', 'dss-netipam-1'],
                'attr': {u'subnet': [{u'ip_prefix': u'10.4.8.0',
                                      u'ip_prefix_len': 30}]}
                }
            ]
    vn_obj2_dict = {}
    vn_obj2_dict['_fq_name'] = ['default-domain', 'default-project',
                                'dss-virtual-network-1']
    vn_obj2_dict['_network_ipam_refs'] = \
            [
                {
                'to': ['default-domain', 'dss-project', 'dss-netipam-1'],
                'attr': {u'subnet': [{u'ip_prefix': u'192.168.1.0',
                                      u'ip_prefix_len': 24}]}
                }
            ]

    ip_instance_obj = {}
    ip_instance_obj['_virtual_network_refs'] = \
            [
                {'to': ['default-domain', 'default-project',
                 'dss-virtual-network-1'], 'attr': {}
                }
            ]
    ip_instance_obj['_instance_ip_address'] = ''
    """

    def __init__(self, testCaseName):
        unittest.TestCase.__init__(self, testCaseName)
        self._db_conn = Db()
        self.addr_mgmt = AddrMgmt(self)

        self.proj_1 = Project('dss-project')
        self.vn_1 = VirtualNetwork('dss-virtual-network')
        self.ipam_1 = NetworkIpam(
            'dss-netipam-1', self.proj_1, IpamType("dhcp"))
        self.sn_1 = VnSubnetsType(
            [IpamSubnetType(SubnetType('10.4.8.0', 29), '10.4.8.6')])
        self.sn_2 = VnSubnetsType(
            [IpamSubnetType(SubnetType('192.168.1.0', 29), '192.168.1.6')])
        self.ip_1 = InstanceIp(str(uuid.uuid4()))
        self.vn_1.add_network_ipam(self.ipam_1, self.sn_1)
        self.ip_1.set_virtual_network(self.vn_1)

        obj_dict = todict(self.ip_1)
        self.vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        pass

    # test objects
    def testAlloc(self):
        self.addr_mgmt.net_create(todict(self.vn_1))
        ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
        print 'VN=%s, Got IP address %s' % (':'.join(self.vn_fq_name), str(ip))
        ipnet = IPNetwork('10.4.8.0/29')
        self.failUnless(IPAddress(ip) in ipnet)
        self.addr_mgmt.ip_free(ip, self.vn_fq_name)
        self.addr_mgmt.net_delete(todict(self.vn_1))
        pass

    # validate all addresses in the subnet get assigned (except
    # reserved (host, broadcast)
    def testCount(self):
        self.addr_mgmt.net_create(todict(self.vn_1))
        count = 0
        alloclist = []
        while True:
            ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
            if ip == ERROR_IPADDR:
                break
            count += 1
            alloclist.append(ip)
        print 'Got %d IP address' % (count)
        self.failUnless(count == 5)
        # free all addresses
        for ip in alloclist:
            self.addr_mgmt.ip_free(ip, self.vn_fq_name)
        # ok to delete the network now
        self.addr_mgmt.net_delete(todict(self.vn_1))
        pass

    # validate address gets freed up correctly
    def testFree(self):
        self.addr_mgmt.net_create(todict(self.vn_1))
        alloclist = []
        for count in range(5):
            ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
            alloclist.append(ip)

        # next alloc should fail
        ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
        self.assertEqual(ip, ERROR_IPADDR)

        # free all addresses
        for count in range(5):
            ip = alloclist.pop()
            self.addr_mgmt.ip_free(ip, self.vn_fq_name)

        # now this should succeed
        ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
        self.assertNotEqual(ip, ERROR_IPADDR)
        self.addr_mgmt.ip_free(ip, self.vn_fq_name)

        self.addr_mgmt.net_delete(todict(self.vn_1))
        pass

    # attempt to allocate without creating subnet first - should fail
    def testInvalidSubnet(self):
        ip = self.addr_mgmt.ip_alloc(todict(self.ip_1))
        self.assertEqual(ip, ERROR_IPADDR)

    # two allocations from fresh subnets should be identical
    def testPurgeSubnet(self):
        self.addr_mgmt.net_create(todict(self.vn_1))
        ip1 = self.addr_mgmt.ip_alloc(self.vn_fq_name)
        self.addr_mgmt.ip_free(ip1, self.vn_fq_name)
        self.addr_mgmt.net_delete(todict(self.vn_1))

        self.addr_mgmt.net_create(todict(self.vn_1))
        ip2 = self.addr_mgmt.ip_alloc(self.vn_fq_name)
        self.addr_mgmt.ip_free(ip2, self.vn_fq_name)
        self.addr_mgmt.net_delete(todict(self.vn_1))

        self.assertEqual(ip1, ip2)

    def testTwoSubnets(self):
        self.vn_1.add_network_ipam(self.ipam_1, self.sn_2)
        self.addr_mgmt.net_create(todict(self.vn_1))
        count = 0
        alloclist = []
        while True:
            ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
            if ip == ERROR_IPADDR:
                break
            count += 1
            alloclist.append(ip)
        print 'Got %d IP address' % (count)
        self.failUnless(count == 10)
        # free all addresses
        for ip in alloclist:
            self.addr_mgmt.ip_free(ip, self.vn_fq_name)

        self.addr_mgmt.net_delete(todict(self.vn_1))

    def testPersistence(self):
        self.vn_1.set_network_ipam(self.ipam_1, self.sn_1)
        self.addr_mgmt.net_create(todict(self.vn_1))
        alloclist = []
        for i in range(2):
            ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
            self.assertNotEqual(ip, ERROR_IPADDR)
            alloclist.append(ip)

        # simulate restart of api-server
        self.addr_mgmt.net_create(todict(self.vn_1))
        for i in range(2):
            ip = self.addr_mgmt.ip_alloc(self.vn_fq_name)
            self.assertEqual(ip in alloclist, False)
            alloclist.append(ip)

        # free all ip addresses
        for ip in alloclist:
            self.addr_mgmt.ip_free(ip, self.vn_fq_name)

    # end testPersistence

    def testMacAllocation(self):
        # server picks UUID from name for certain identifiers
        port_obj = VirtualMachineInterface(str(uuid.uuid4()))
        port_obj.set_virtual_network(self.vn_1)
        mac_addr = self.addr_mgmt.mac_alloc(todict(port_obj))
        mac_str = mac_addr.replace(':', '')
        name_str = '%s%s%s' % ('02', port_obj.name[0:8], port_obj.name[9:11])
        self.failUnless(mac_str == name_str)


def suite():
    loader = unittest.TestLoader()
    testsuite = loader.loadTestsFromTestCase(TestIp)
    return testsuite


def test_ip():
    testsuite = suite()
    runner = unittest.TextTestRunner(sys.stdout, verbosity=2)
    result = runner.run(testsuite)

if __name__ == "__main__":
    test_ip()
