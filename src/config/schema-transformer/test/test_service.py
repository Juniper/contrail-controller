#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
sys.path.append("../common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case

from vnc_api.vnc_api import *

class TestPolicy(test_case.STTestCase):
    def test_basic_policy(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                break
                print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        while True:
            ri = self._vnc_lib.routing_instance_read(
                fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                gevent.sleep(2)
            else:
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break

    # end test_basic_policy

    def _test_service_policy(self):
        import ipdb; ipdb.set_trace()
        # create  vn1
        vn1_obj = VirtualNetwork('vn1')
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn1_obj)

        # create vn2
        vn2_obj = VirtualNetwork('vn2')
        ipam_obj = NetworkIpam('ipam2')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn2_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("20.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn2_obj)

        np = self.create_network_policy(vn1_obj, vn2_obj, ["s1"])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1',
                     u'service-default-domain_default-project_vn1-default'
                     '-domain_default-project_vn2-default'
                     '-domain_default-project_s1'])
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', u'vn2',
                             u'service-default-domain_default'
                             '-project_vn2-default-domain_default'
                             '-project_vn1-default-domain_default-project_s1'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                sci = ri.get_service_chain_information()
                if sci is None:
                    print "retrying ... ", test_common.lineno()
                    gevent.sleep(2)
                    continue
                self.assertEqual(sci.prefix[0], '10.0.0.1/24')
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs is None:
                break
            print "retrying ... ", test_common.lineno()
        # end while True
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break

    # end test_service_policy
