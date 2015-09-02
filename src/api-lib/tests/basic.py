#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import unittest

import vnc_api
from vnc_api import Subnet, IpVer
from vnc_api import SGPeerType, SGPeer, SGDir, SGAction, SGApp, SGAppType
from vnc_api import PortParam, SGRule
from vnc_api import PeEndPt, PeEndPtType, PeAppType, PeAction, PeApp, PEntry

class CRUDTestCase2(unittest.TestCase):
    def setUp(self):
        self.conn = vnc_api.Connection('user1', 'password1', 'infra')

    def test_vpc_ops_policy_entries(self):
        vpc_name = "vpc200"
        vn_name = "vn200"
        pol_name1 = "pol201"
        pol_name2 = "pol202"

        vpc_id = self.conn.vpc_create(vpc_name)
        vn_id = self.conn.vn_create(vpc_id, vn_name)

        pol_id1 = self.conn.policy_create(vpc_id, pol_name1)
        mypolicy = self.conn.policy_read(policy_id = pol_id1)
        self.assertEqual(mypolicy.p_name, pol_name1)

        pol_id2 = self.conn.policy_create(vpc_id, pol_name2)
        mypolicy = self.conn.policy_read(policy_id = pol_id2)
        self.assertEqual(mypolicy.p_name, pol_name2)

        # create a policy entry: 2 endpts, first app
        from_subnet1 = Subnet(IpVer.IPV4, "50.1.1.0", sn_prefix_len = 24)
        from_endpt1 = PeEndPt(PeEndPtType.PEP_VN, vn_id, from_subnet1)
        to_subnet1 = Subnet(IpVer.IPV4, "50.1.2.0", sn_prefix_len = 24)
        to_endpt1 = PeEndPt(PeEndPtType.PEP_VN, vn_id, to_subnet1)

        udp_param_obj = PortParam(0, 0, 35, 35)
        pe_app_obj1 = PeApp(PeAppType.HTTP, udp_param = udp_param_obj)
        pe_obj1 = PEntry(1, from_endpt1, to_endpt1, pe_app_obj1, PeAction.ALLOW)
        self.conn.policy_entry_create(pol_id1, pe_obj1)

        # create second policy entry: 2 endpts, second app
        udp_param_obj = PortParam(0, 0, 40, 40)
        pe_app_obj2 = PeApp(PeAppType.HTTP, udp_param = udp_param_obj)
        pe_obj2 = PEntry(1, from_endpt1, to_endpt1, pe_app_obj2, PeAction.ALLOW)
        self.conn.policy_entry_create(pol_id1, pe_obj2)

        # delete policy-entries
        self.conn.policy_entry_delete(pol_id1, pe_obj2)
        self.conn.policy_entry_delete(pol_id1, pe_obj1)

        # delete id-2 and read. read should fail
        self.conn.policy_delete(pol_id2)
        try:
            self.conn.policy_read(policy_id = pol_id2)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 404)
        else:
            self.assertTrue(False)

        # delete id-1 and read. read should fail
        self.conn.policy_delete(pol_id1)
        try:
            self.conn.policy_read(policy_id = pol_id1)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 404)
        else:
            self.assertTrue(False)

class CRUDTestCase(unittest.TestCase):
    def setUp(self):
        # TODO add tenant and users programmatically
        self.conn = vnc_api.Connection('user1', 'password1', 'infra')

    def test_vpc_ops(self):
        vpc_name = "vpc100"
        vn_name = "vn100"
        # Verify vpc name doesn't exist
        # TBI

        # Create Tests
        vpc_id = self.conn.vpc_create(vpc_name)

        # Read Tests
        vpc = self.conn.vpc_read(vpc_id = vpc_id)
        self.assertEqual(vpc.vpc_name, vpc_name)

        # Update Tests
        vn_id = self.conn.vn_create(vpc_id, vn_name)
        vpc = self.conn.vpc_read(vpc_id = vpc_id)

        found = False
        for vn in vpc.vpc_vnets:
            if vn.vn_name == vn_name:
                found = True
                break

        self.assertTrue(found)

        # Delete Tests -
        # this should fail...
        try:
            self.conn.vpc_delete(vpc_id)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 409)
        else:
            self.assertTrue(False)

        # delete children and retry...
        self.conn.vn_delete(vn_id)
        self.conn.vpc_delete(vpc_id)

        # ...it should be gone
        try:
            vpc = self.conn.vpc_read(vpc_id = vpc_id)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 404)
        else:
            self.assertTrue(False)

    def test_vn_ops(self):
        vpc_name = "vpc100"
        vn_name = "vn100"

        # Setup - Verify vpc name doesn't exist TBI
        vpc_id = self.conn.vpc_create("vpc100")

        # Create Tests
        vn_id = self.conn.vn_create(vpc_id, vn_name)

        #
        # Read Tests
        #
        vn = self.conn.vn_read(vn_id = vn_id)
        self.assertEqual(vn.vn_name, vn_name)

        #
        # Update Tests
        #
        sn_obj = Subnet(IpVer.IPV4, "10.1.1.0", sn_prefix_len = 24)
        self.conn.subnet_create(vn_id, sn_obj)

        # read back and verify
        vn = self.conn.vn_read(vn_id = vn_id)
        self.assertTrue(sn_obj in vn.vn_subnets)

        # TODO duplicate add should fail

        #
        # Delete Tests
        #
        # this should fail...
        try:
            self.conn.vn_delete(vn_id)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 409)
        else:
            self.assertTrue(False)

        # delete children and retry...
        self.conn.subnet_delete(vn_id, sn_obj)
        self.conn.vn_delete(vn_id)

        # ...it should be gone
        try:
            vn = self.conn.vn_read(vn_id = vn_id)
        except vnc_api.InvalidOp as e:
            self.assertEqual(e.what, 404)
        else:
            import pdb; pdb.set_trace()
            self.assertTrue(False)

        # Teardown
        self.conn.vpc_delete(vpc_id)

    def test_auth(self):
        # split into separate test cases:
        # test with unsupported authentication method
        # test first auth with success
        # test first auth with failure
        # test reauth after token expiry with success
        # test reauth after token expiry with failure
        # test reauth after failed, on next api request
        pass

class CRUDTestCase1(unittest.TestCase):
    def setUp(self):
        self.conn = vnc_api.Connection('user1', 'password1', 'infra')

    def _test_vpc_ops_sg_rules(self):
        vpc_name = "vpc200"
        vn_name = "vn200"
        sg_name = "sg200"

        vpc_id = self.conn.vpc_create(vpc_name)
        vn_id = self.conn.vn_create(vpc_id, vn_name)
        sg_id = self.conn.sg_create(vpc_id, sg_name)
        sg = self.conn.sg_read(sg_id = sg_id)
        self.assertEqual(sg.sg_name, sg_name)

        # 1. create first sg_rule
        # create sg_peer obj for SG rule
        subnet_obj = Subnet(IpVer.IPV4, "10.1.1.0", sn_prefix_len = 24)
        sg_peer_obj = SGPeer(SGPeerType.SECURITY_GROUP, vn_id, subnet_obj);

        # create application_t obj for SG rule
        tcp_param_obj = PortParam(0, 0, 22, 22)
        sg_app_obj = SGApp(SGAppType.HTTP, tcp_param = tcp_param_obj)

        # create first SG rule object
        sg_rule_obj1 = SGRule(SGDir.INBOUND, sg_peer_obj, sg_app_obj,
                              SGAction.ALLOW)
        self.conn.sg_rule_create(sg_id, sg_rule_obj1)

        # 2. create second sg_rule
        # use sg_peer_obj from above
        # create application_t obj for SG rule
        udp_param_obj = PortParam(0, 0, 22, 22)
        sg_app_obj = SGApp(SGAppType.HTTP, udp_param = udp_param_obj)

        # create second SG rule object
        sg_rule_obj2 = SGRule(SGDir.INBOUND, sg_peer_obj, sg_app_obj,
                              SGAction.ALLOW)
        self.conn.sg_rule_create(sg_id, sg_rule_obj2)

        # 3. create third sg_rule
        # use sg_peer_obj from above
        # create application_t obj for SG rule
        udp_param_obj = PortParam(0, 0, 23, 23)
        sg_app_obj = SGApp(SGAppType.HTTP, udp_param = udp_param_obj)

        # create third SG rule object
        sg_rule_obj3 = SGRule(SGDir.OUTBOUND, sg_peer_obj, sg_app_obj,
                              SGAction.ALLOW)
        self.conn.sg_rule_create(sg_id, sg_rule_obj3)

        # delete the second object
        self.conn.sg_rule_delete(sg_id, sg_rule_obj2)
        # delete the first object
        self.conn.sg_rule_delete(sg_id, sg_rule_obj1)
        # delete the third object
        self.conn.sg_rule_delete(sg_id, sg_rule_obj3)

if __name__ == '__main__':
    unittest.main()
