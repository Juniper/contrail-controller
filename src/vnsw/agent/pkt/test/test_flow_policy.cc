/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#include "test_flow_base.cc"

TEST_F(FlowTest, Flow_Source_Vn_1) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, "17.1.1.1", 1, 0, 0, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyVn("vn5", unknown_vn_),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
}

/* Create flow for a VN which has ACL attached to it.
 * Make sure that ACL does not have any matching entries for flow
 * Very that network policy UUID is set to IMPLICIT_DENY and SG UUID
 * is set to NOT_EVALUATED
 */
TEST_F(FlowTest, FlowPolicyUuid_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
}

TEST_F(FlowTest, FlowPolicyUuid_2) {
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2");
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Create a Local flow for a VN which does not have any ACL attached.
 * Verify that network ACE UUID is set to IMPLICIT_ALLOW.
 * Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is not set
 */
TEST_F(FlowTest, FlowPolicyUuid_3) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send traffic (SIP Y and DIP X) in only one direction from port 'B'
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_4) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward flow
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only INGRESS SG ACL on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_5) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set
 */
TEST_F(FlowTest, FlowPolicyUuid_6) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send traffic (SIP Y and DIP X) in only one direction from port 'B'
 * Verify that SG UUID is NOT set
 */
TEST_F(FlowTest, FlowPolicyUuid_7) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure only EGRESS SG ACL on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is NOT set
 */
TEST_F(FlowTest, FlowPolicyUuid_8) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure SG ACLs (both direction) on port 'A'
 * Send bidirectional traffic from both directions
 * Verify that SG UUID is set
 */
// TODO need to be fixed.
// expected uuid is fe6a4dcb-dde4-48e6-8957-856a7aacb2d2
// but actual is fe6a4dcb-dde4-48e6-8957-856a7aacb2e2
TEST_F(FlowTest, DISABLED_FlowPolicyUuid_9) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", BIDIRECTION,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg1");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'deny'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to deny sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_10) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 1, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Send traffic (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to EGRESS sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_11) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to deny sg rule uuid
 */
TEST_F(FlowTest, FlowPolicyUuid_12) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 11, 6, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    const FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'deny'
 * (Because of above config, flows on port A (X - Y) will be marked as
 * 'implicit_deny'. It is implicity denied because port 'B' does not have
 * any matching 'ingress' rule
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to IMPLICIT_DENY
 */
TEST_F(FlowTest, FlowPolicyUuid_13) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e2", "sg_acl_e2", 11, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    client->WaitForIdle();
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_DENY),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_e2", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_e2");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure EGRESS SG ACL on port 'A' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'deny'
 * Configure INGRESS SG ACL on port 'A' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to first pass UUID
 */
TEST_F(FlowTest, FlowPolicyUuid_14) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    AddSgEntry("sg_i", "sg_acl_i", 11, 6, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d3", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    AddSgEntry("sg_e2", "sg_acl_e2", 12, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    AddSgEntry("sg_i2", "sg_acl_i2", 13, 6, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e3", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_i2");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 2U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 2U);

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    const FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d3",
                 rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_e2");
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_i2");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_e2", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i2", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_e2");
    DelNode("security-group", "sg_i");
    DelNode("security-group", "sg_i2");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* For link_local flows verify network policy UUID and SG UUID are set to
 * the value of link_local UUID value
 */
TEST_F(FlowTest, FlowPolicyUuid_15) {
    Agent::GetInstance()->set_router_id(Ip4Address::from_string(vhost_ip_addr));
    std::string fabric_ip("1.2.3.4");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back("1.2.3.4");
    TestLinkLocalService service = { "test_service", linklocal_ip, linklocal_port,
                                     "", fabric_ip_list, fabric_port };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TestFlow nat_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, linklocal_ip, IPPROTO_TCP, 3000,
                        linklocal_port, "vrf5", flow0->id(), 1),
            {
                new VerifyNat(fabric_ip, vhost_ip_addr, IPPROTO_TCP, fabric_port, 0)
            }
        }
    };

    CreateFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());
    const FlowEntry *fe = nat_flow[0].pkt_.FlowFetch();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::LINKLOCAL_FLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::LINKLOCAL_FLOW),
                 fe->sg_rule_uuid().c_str());

    //Delete forward flow and expect both flows to be deleted
    DeleteFlow(nat_flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));

    DelLinkLocalConfig();
    client->WaitForIdle();
}

/* For multicast flows verify network policy UUID and SG UUID are set to
 * the value of multicast UUID value
 */
TEST_F(FlowTest, FlowPolicyUuid_16) {
    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };
    AddIPAM("vn5", ipam_info, 1);
    client->WaitForIdle();

    if (!RouteFind("vrf5", "11.1.1.255", 32)) {
        return;
    }
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm1_ip, "11.1.1.255", 1, 0, 0, "vrf5", 
                       flow0->id()),
        {}
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev_fe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(rev_fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::MULTICAST_FLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::MULTICAST_FLOW),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
    DelIPAM("vn5");
    client->WaitForIdle();
}

//Local with both egress and ingress SG allowing the traffic
//Verify SG uuid of forward flow is set to that of Egress SG of flow5
//Verify SG uuid of reverse flow is set to that of Ingress SG of flow6
TEST_F(FlowTest, FlowPolicyUuid_17) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();

    AddSgEntry("sg_i", "sg_acl_i", 11, 1, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i");
    client->WaitForIdle();


    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                 rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);
    DelNode("security-group", "sg1");

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);
    DelNode("security-group", "sg_i");

    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

//Add a remote route and verify that
//both forward and reverse flow are set to that of egress SG uuid of flow5
TEST_F(FlowTest, FlowPolicyUuid_18) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();

    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

    TunnelRouteAdd("10.1.1.2", "1.1.1.3", "vrf6", 16, "vn6");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, "1.1.1.3", 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");

    strncpy(acl_name, "sg_acl1", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg1", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);
    DelNode("security-group", "sg1");
    ::DeleteRoute("vrf6", "1.1.1.3", 32, bgp_peer_);

    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}


/* Local flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure INGRESS SG ACL on port 'A' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'pass'
 * Configure INGRESS SG ACL on port 'B' with action 'deny'
 * Configure ERESS SG ACL on port 'A' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to first pass UUID
 */
TEST_F(FlowTest, FlowPolicyUuid_19) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_i", "sg_acl_i", 10, 6, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_i");
    AddSgEntry("sg_e", "sg_acl_e", 11, 6, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d3", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_e");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 1U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 1U);

    AddSgEntry("sg_i2", "sg_acl_i2", 12, 6, "deny", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", "0");
    AddLink("virtual-machine-interface", "flow6", "security-group", "sg_i2");
    AddSgEntry("sg_e2", "sg_acl_e2", 13, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e3", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e2");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 2U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 2U);

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 6,
                                  1, 2, flow5->flow_key_nh()->id());
    const FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2",
                 fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2d3",
                 rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_i");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_i2");
    DelLink("virtual-machine-interface", "flow6", "security-group", "sg_ei");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_e2", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i2", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i2", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_e2");
    DelNode("security-group", "sg_i");
    DelNode("security-group", "sg_i2");
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

/* Remote flow. Port 'A' has IP 'X' and port 'B' has IP 'Y'
 * Configure INGRESS SG ACL on port 'A' with action 'pass'
 * Configure EGRESS SG ACL on port 'B' with action 'deny'
 * Send TCP Ack pkt (SIP X and DIP Y) in only one direction from port 'A'
 * Verify that SG UUID is set to first pass UUID
 */
TEST_F(FlowTest, FlowPolicyUuid_20) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    FlowSetup();

    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    EXPECT_EQ(flow6->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 6, "deny", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    
    AddSgEntry("sg_i", "sg_acl_i", 13, 6, "pass", INGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2e3", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_i");
    client->WaitForIdle();
    EXPECT_EQ(flow5->sg_list().list_.size(), 2U);

    TunnelRouteAdd("10.1.1.2", "1.1.1.3", "vrf6", 16, "vn6");
    client->WaitForIdle();

    //Send TCP Ack pkt
    TxTcpPacket(flow5->id(), vm_a_ip, "1.1.1.3", 1, 2, true, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, "1.1.1.3", 6,
                                  1, 2, flow5->flow_key_nh()->id());
    const FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::IMPLICIT_ALLOW),
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e3",
                 fe->sg_rule_uuid().c_str());
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e3",
                 rfe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_i");

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    strncpy(acl_name, "sg_acl_i", max_len);
    strncat(acl_name, "ingress-access-control-list", max_len);
    DelLink("security-group", "sg_i", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    DelNode("security-group", "sg_i");
    ::DeleteRoute("vrf6", "1.1.1.3", 32, bgp_peer_);
    client->WaitForIdle();
    FlowTeardown();
    client->WaitForIdle(5);
}

//Create a l2 flow and verify l3 route
//priority gets increased
TEST_F(FlowTest, TrafficPriority) {
    TxL2Packet(flow0->id(),input[0].mac, input[1].mac,
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string(vm1_ip);
    InetUnicastRouteEntry *rt = RouteGet("vrf5", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->path_preference().wait_for_traffic()
            == false);
    FlushFlowTable();
    client->WaitForIdle();
}

//Create a layer2 flow and verify that we dont add layer2 route
//in prefix length manipulation
TEST_F(FlowTest, Layer2PrefixManipulation) {
    DelLink("virtual-network", "vn5", "access-control-list", "acl1");
    //Add a vrf translate acl, so that packet is forced to go thru l3 processing
    AddVrfAssignNetworkAcl("Acl", 10, "vn5", "vn5", "pass", "vrf5");
    AddLink("virtual-network", "vn5", "access-control-list", "Acl");
    client->WaitForIdle();

    TxL2Packet(flow0->id(),input[0].mac, input[1].mac,
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();

    int nh_id = flow0->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, vm2_ip, 1, 0, 0, nh_id);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->data().flow_source_plen_map.size() == 0);

    FlushFlowTable();
    client->WaitForIdle();
    DelLink("virtual-network", "vn5", "access-control-list", "Acl");
    DelAcl("Acl");
    DelLink("virtual-network", "vn5", "access-control-list", "acl1");
    client->WaitForIdle();
}

TEST_F(FlowTest, AclRuleUpdate) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;

    FlowSetup();
    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();

    AclDBEntry *acl = AclGet(10);
    EXPECT_TRUE(acl != NULL);
    EXPECT_TRUE(acl->IsRulePresent("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2"));

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);

    //Update the ACL with new rule
    AddAclEntry(acl_name, 10, 1, "pass", "ge6a4dcb-dde4-48e6-8957-856a7aacb2d3");
    client->WaitForIdle();
    EXPECT_FALSE(acl->IsRulePresent("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2"));
    EXPECT_TRUE(acl->IsRulePresent("ge6a4dcb-dde4-48e6-8957-856a7aacb2d3"));

    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    FlowTeardown();
    client->WaitForIdle(5);
}

TEST_F(FlowTest, FlowPolicyLogAction_1) {
    AddAclLogActionEntry("acl4", 4, 1, "deny", "true",
                         "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2");
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                 fe->nw_ace_uuid().c_str());
    uint32_t action = fe->match_p().action_info.action;

    bool log_action = false, alert_action = false;
    if (action & (1 << TrafficAction::LOG)) {
        log_action = true;
    }
    EXPECT_TRUE(log_action);

    if (action & (1 << TrafficAction::ALERT)) {
        alert_action = true;
    }
    EXPECT_FALSE(alert_action);

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match using deprecated tag 'subnet' works fine */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_1) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match using new tag 'subnet-list' works fine.
 * Verify with subnet-list of only 1 element */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_2) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet-list> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match using new tag 'subnet-list' works fine.
 * Verify with subnet-list of 2 elements and the matched element is first
 * entry */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_3) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet-list> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>17.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match using new tag 'subnet-list' works fine.
 * Verify with subnet-list of 3 elements and the matched element is last
 * entry */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_4) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet-list> <ip-prefix>18.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>17.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match using new tag 'subnet-list' works fine.
 * Verify with subnet-list of 2 elements and there is no matching entry */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_5) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet-list> <ip-prefix>18.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>17.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STRNE("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match works when both 'subnet' and new tag
 * 'subnet-list' are configured.
 * Verify with subnet-list of 2 elements where matching entry is present in
 * 'subnet' */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_6) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet>\n"
            "<subnet-list> <ip-prefix>18.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>17.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2", fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match works when both 'subnet' and new tag
 * 'subnet-list' are configured.
 * Verify with subnet-list of 2 elements where matching entry is present in
 * 'subnet-list' */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_7) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet> <ip-prefix>19.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet>\n"
            "<subnet-list> <ip-prefix>18.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>16.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STREQ("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

/* Verify that ACL rule CIDR match works when both 'subnet' and new tag
 * 'subnet-list' are configured.
 * Verify with subnet-list of 2 elements and there is no matching entry */
TEST_F(FlowTest, FlowPolicyUuid_Subnet_8) {
    char subnet_str[1000];
    sprintf(subnet_str,
            "<subnet> <ip-prefix>19.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet>\n"
            "<subnet-list> <ip-prefix>18.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n"
            "<subnet-list> <ip-prefix>17.1.1.0</ip-prefix>"
            "<ip-prefix-len>24</ip-prefix-len></subnet-list>\n");
    AddAclEntry("acl4", 4, 1, "deny", "fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                subnet_str, subnet_str);
    FlowSetup();
    AddLink("virtual-network", "vn6", "access-control-list", "acl4");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm_a_ip, vm_b_ip, 1, 0, 0, "vrf6",
                       flow5->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        },
        {  TestFlowPkt(Address::INET, vm_b_ip, vm_a_ip, 1, 0, 0, "vrf6",
                       flow6->id()),
        {
            new VerifyVn("vn6", "vn6"),
            new VerifyVrf("vrf6", "vrf6")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify the network policy uuid and SG rule UUID for flow.
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_STRNE("fe6a4dcb-dde4-48e6-8957-856a7aacb2e2",
                 fe->nw_ace_uuid().c_str());
    EXPECT_STREQ(FlowEntry::FlowPolicyStateStr.at(FlowEntry::NOT_EVALUATED),
                 fe->sg_rule_uuid().c_str());

    //cleanup
    FlushFlowTable();
    client->WaitForIdle();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    DelLink("virtual-network", "vn6", "access-control-list", "acl4");
    FlowTeardown();
    DelNode("access-control-list", "acl4");
    client->WaitForIdle(5);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();

    TestShutdown();
    delete client;
    return ret;
}
