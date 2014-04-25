/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_flow_util.h"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
};

struct PortInfo input1[] = {
    {"intf3", 3, "2.1.1.1", "00:00:00:01:01:01", 2, 3},
    {"intf4", 4, "2.1.1.2", "00:00:00:01:01:02", 2, 4},
};

struct PortInfo input2[] = {
    {"intf5", 5, "2.1.1.1", "00:00:00:01:01:01", 3, 5},
    {"intf6", 6, "2.1.1.2", "00:00:00:01:01:02", 3, 6},
};

class TestVrfAssignAclFlow : public ::testing::Test {
public:
    void AddAddressVrfAssignAcl(const char *intf_name, int intf_id, 
            const char *sip, const char *dip, int proto, int sport_start, 
            int sport_end, int dport_start, int dport_end, const char *vrf,
            const char *ignore_acl) {
        char buf[3000];
        sprintf(buf,
                "    <vrf-assign-table>\n"
                "        <vrf-assign-rule>\n"  
                "            <match-condition>\n"
                "                 <protocol>\n"
                "                     %d\n"
                "                 </protocol>\n"
                "                 <src-address>\n"
                "                     <subnet>\n"
                "                        <ip-prefix>\n"
                "                         %s\n"
                "                        </ip-prefix>\n"
                "                        <ip-prefix-len>\n"
                "                         24\n"
                "                        </ip-prefix-len>\n"
                "                     </subnet>\n"
                "                 </src-address>\n"
                "                 <src-port>\n"
                "                     <start-port>\n"
                "                         %d\n"
                "                     </start-port>\n"
                "                     <end-port>\n"
                "                         %d\n"
                "                     </end-port>\n"
                "                 </src-port>\n"
                "                 <dst-address>\n"
                "                     <subnet>\n"
                "                        <ip-prefix>\n"
                "                         %s\n"
                "                        </ip-prefix>\n"
                "                        <ip-prefix-len>\n"
                "                         24\n"
                "                        </ip-prefix-len>\n"
                "                     </subnet>\n"
                "                 </dst-address>\n"
                "                 <dst-port>\n"
                "                     <start-port>\n"
                "                        %d\n"
                "                     </start-port>\n"
                "                     <end-port>\n"
                "                        %d\n"
                "                     </end-port>\n"         
                "                 </dst-port>\n"
                "             </match-condition>\n"
                "             <vlan-tag>0</vlan-tag>\n"
                "             <routing-instance>%s</routing-instance>\n"
                "             <ignore-acl>%s</ignore-acl>\n"
                "         </vrf-assign-rule>\n"
                "    </vrf-assign-table>\n", 
        proto, sip, sport_start, sport_end, dip, dport_start, dport_end, vrf,
        ignore_acl);
        AddNode("virtual-machine-interface", intf_name, intf_id, buf);
        client->WaitForIdle();  
    }

protected:
    virtual void SetUp() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        CreateVmportEnv(input, 2);
        CreateVmportEnv(input1, 2);
        CreateVmportEnv(input2, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        EXPECT_TRUE(VmPortActive(3));
        EXPECT_TRUE(VmPortActive(4));
        EXPECT_TRUE(VmPortActive(5));
        EXPECT_TRUE(VmPortActive(6));
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        DeleteVmportEnv(input1, 2, true);
        DeleteVmportEnv(input2, 2, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VmPortFindRetDel(2));
        EXPECT_FALSE(VmPortFindRetDel(3));
        EXPECT_FALSE(VmPortFindRetDel(4));
        EXPECT_FALSE(VmPortFindRetDel(5));
        EXPECT_FALSE(VmPortFindRetDel(6));
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }
};

//Add an VRF translate ACL to send all ssh traffic to "2.1.1.1" via VN2
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl1) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf2", "yes");

    TestFlow flow[] = {
        {  TestFlowPkt("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20, "vrf1",
                VmPortGet(1)->id()),
        {
            new VerifyVn("vn1", "vn2"),
            new VerifyVrf("vrf1", "vrf2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS))
        }
        }
    };
    CreateFlow(flow, 1);
}

//Add an VRF tranlsate ACL to send all traffic destined to "2.1.1.1" via VN3
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl2) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf3", "yes");
    TestFlow flow[] = {
        {  TestFlowPkt("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20, "vrf1",
                VmPortGet(1)->id()),
        {
            new VerifyVn("vn1", "vn3"),
            new VerifyVrf("vrf1", "vrf3"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS))
        }
        }
    };
    CreateFlow(flow, 1);
}

//Add an VRF translate ACL to send all ssh traffic to "2.1.1.1" via VN4
//which is not present, make sure flow is marked as short flow
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl3) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf4", "yes");
    TestFlow flow[] = {
        {  TestFlowPkt("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20, "vrf1",
                VmPortGet(1)->id()),
        {
            new ShortFlow()
        }
        }
    };
    CreateFlow(flow, 1);
}

//Add a VRF translate ACL to send all ssh traffic to "2.1.1.1" via VN2 
//with ignore acl option set, add an ACL to deny all traffic from VN1 to VN2
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl4) {
    AddAcl("Acl", 10, "vn1", "vn2", "drop");
    AddLink("virtual-network", "vn1", "access-control-list", "Acl");
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf2", "no");
    client->WaitForIdle();
    TestFlow flow[] = {
        {  TestFlowPkt("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20, "vrf1",
                VmPortGet(1)->id()),
        {
            new VerifyVn("vn1", "vn2"),
            new VerifyVrf("vrf1", "vrf2"),
            new VerifyAction((1 << TrafficAction::DROP) | 
                             (1 << TrafficAction::IMPLICIT_DENY) | 
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::DROP) | 
                             (1 << TrafficAction::IMPLICIT_DENY))
        }
        }
    };
    CreateFlow(flow, 1);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    return RUN_ALL_TESTS();
}
