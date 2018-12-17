/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

VmInterface *vnet[16];
Interface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];

PhysicalInterface *eth;
int hash_id;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:01:01:01:02", 1, 2},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

TestTag src[] = {
    {"App1",  1, (TagTable::APPLICATION << TagEntry::kTagTypeBitShift) | 0x1},
    {"Tier1", 2, (TagTable::TIER << TagEntry::kTagTypeBitShift) | 0x1},
    {"Site1", 3, (TagTable::SITE << TagEntry::kTagTypeBitShift) | 0x1},
};

TestTag dst[] = {
    {"App2",  4, (TagTable::APPLICATION << TagEntry::kTagTypeBitShift) | 0x2},
    {"Tier2", 5, (TagTable::TIER << TagEntry::kTagTypeBitShift) | 0x2},
    {"Site2", 6, (TagTable::SITE << TagEntry::kTagTypeBitShift) | 0x2},
};

class TestPolicySet : public ::testing::Test {
public:
    virtual void SetUp() {
        client->WaitForIdle();

        agent_ = Agent::GetInstance();
        CreateVmportEnv(input1, 2);
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        CreateTags(src, 3);
        CreateTags(dst, 3);

        AddLink("virtual-machine-interface", "vnet1", "tag", "App1");
        AddLink("virtual-machine-interface", "vnet1", "tag", "Tier1");
        AddLink("virtual-machine-interface", "vnet1", "tag", "Site1");
        client->WaitForIdle();

        AddLink("virtual-machine-interface", "vnet2", "tag", "App2");
        AddLink("virtual-machine-interface", "vnet2", "tag", "Tier2");
        AddLink("virtual-machine-interface", "vnet2", "tag", "Site2");
        client->WaitForIdle();

        vnet[1] = VmInterfaceGet(1);
        vnet[2] = VmInterfaceGet(2);

        //Add couple of fw rules
        std::vector<std::string> match;

        AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass");
        client->WaitForIdle();

        AddFirewall("AppOnly", 2, match, src, 1, dst, 1, "pass");
        client->WaitForIdle();

        AddFirewall("SiteOnly", 3, match, src+1, 1, dst+1, 1, "pass");
        client->WaitForIdle();

        match.push_back("Tier");
        match.push_back("Site");
        AddFirewall("MatchCondition", 4, match, src, 3, dst, 3, "pass");
        client->WaitForIdle();

        AddNode("firewall-policy", "fw1", 1);
        AddNode("firewall-policy", "fw2", 2);

        AddNode("application-policy-set", "aps1", 1);
        AddNode("application-policy-set", "aps2", 2);

        AddLink("tag", "App1", "application-policy-set", "aps1");
        AddLink("tag", "App2", "application-policy-set", "aps2");

        AddFwRuleTagLink("MatchAllTag", src, 3);
        AddFwRuleTagLink("MatchAllTag", dst, 3);
        AddFwRuleTagLink("AppOnly", src, 3);
        AddFwRuleTagLink("AppOnly", dst, 3);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM("vn1");
        DeleteVmportEnv(input1, 2, true);
        DeleteTags(src, 3);
        DeleteTags(dst, 3);

        DelLink("virtual-machine-interface", "vnet1", "tag", "App1");
        DelLink("virtual-machine-interface", "vnet1", "tag", "Tier1");
        DelLink("virtual-machine-interface", "vnet1", "tag", "Site1");
        DelLink("virtual-machine-interface", "vnet2", "tag", "App2");
        DelLink("virtual-machine-interface", "vnet2", "tag", "Tier2");
        DelLink("virtual-machine-interface", "vnet2", "tag", "Site2");
        client->WaitForIdle();

        DelNode("firewall-rule", "MatchAllTag");
        DelNode("firewall-rule", "AppOnly");
        DelNode("firewall-rule", "SiteOnly");
        DelNode("firewall-rule", "MatchCondition");
        client->WaitForIdle();

        DelNode("firewall-policy", "fw1");
        DelNode("firewall-policy", "fw2");
        DelNode("application-policy-set", "aps1");
        DelNode("application-policy-set", "aps2");
        DelLink("tag", "App1", "application-policy-set", "aps1");
        DelLink("tag", "App2", "application-policy-set", "aps1");

        DelFwRuleTagLink("MatchAllTag", src, 3);
        DelFwRuleTagLink("MatchAllTag", dst, 3);
        DelFwRuleTagLink("AppOnly", src, 3);
        DelFwRuleTagLink("AppOnly", dst, 3);
        client->WaitForIdle();
    }
protected:
    Agent *agent_;
};

TEST_F(TestPolicySet, Allow) {
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    client->WaitForIdle();
}

TEST_F(TestPolicySet, Deny) {
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    std::vector<std::string> protocol;
    protocol.push_back("17");
    std::vector<uint16_t> port;
    port.push_back(1000);
    AddServiceGroup("sg1", 1, protocol, port);
    AddLink("firewall-rule", "MatchAllTag", "service-group", "sg1");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1, 1);
    TxUdpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 10, 1000, 10);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::DENY));

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                   17, 10, 1000, GetFlowKeyNH(1));
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    DelLink("firewall-rule", "MatchAllTag", "service-group", "sg1");
    DelNode("service-group", "sg1");
    client->WaitForIdle();

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));
    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                   1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));
}

TEST_F(TestPolicySet, ChangeOfTag) {
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    //Change Tag associated with route
    DelLink("virtual-machine-interface", "vnet1", "tag", "Site1");
    client->WaitForIdle();

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    client->WaitForIdle();
}

//Remove application policy set on egress interface and verify that
//action is set properly
TEST_F(TestPolicySet, NoPolicyOnEgressInterface) {
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "SiteOnly", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "vnet1", "tag", "App2");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    //no application-policy-set on either interface
    DelLink("virtual-machine-interface", "vnet1", "tag", "App1");
    client->WaitForIdle();

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "SiteOnly");
    client->WaitForIdle();
}

//Unidirection policy-set
TEST_F(TestPolicySet, Unidirectional_Forward) {
    std::vector<std::string> match;
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", ">");
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    FlowEntry* rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    client->WaitForIdle();
}

TEST_F(TestPolicySet, Unidirectional_Reverse) {
    std::vector<std::string> match;
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<");
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::DENY));

    FlowEntry* rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::DENY));
    EXPECT_TRUE(rflow->is_flags_set(FlowEntry::Trap));

    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "deny", "<");
    client->WaitForIdle();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::DENY));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    client->WaitForIdle();
}

TEST_F(TestPolicySet, Unidirectional_Reverse_Udp) {
    std::vector<std::string> match;

    std::vector<std::string> protocol;
    protocol.push_back("17");
    std::vector<uint16_t> port;
    port.push_back(1000);

    AddServiceGroup("sg1", 1, protocol, port);
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<");
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    AddLink("firewall-rule", "MatchAllTag", "service-group", "sg1");
    client->WaitForIdle();

    //Service group allows only UDP flow
    TxUdpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1000, 10, 10);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               17, 1000, 10, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::DENY));

    FlowEntry* rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::DENY));
    EXPECT_TRUE(rflow->is_flags_set(FlowEntry::Trap));

    TxUdpPacket(vnet[2]->id(), "1.1.1.2", "1.1.1.1", 10, 1000, rflow->flow_handle());
    client->WaitForIdle();
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelLink("firewall-rule", "MatchAllTag", "service-group", "sg1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    client->WaitForIdle();
}

TEST_F(TestPolicySet, MultiRule) {
    std::vector<std::string> match;
    match.push_back("Application");
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass");

    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::DENY));

    FlowEntry* rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::DENY));

    AddFirewallPolicyRuleLink("fpfr2", "fw2", "SiteOnly", "2");
    AddPolicySetFirewallPolicyLink("link2", "aps1", "fw2", "2");
    client->WaitForIdle();

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::PASS));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    DelFirewallPolicyRuleLink("fpfr2", "fw2", "SiteOnly");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fw2");
    client->WaitForIdle();
}

//More Specific rule additon
TEST_F(TestPolicySet, MoreSpecificRule) {
    AddFirewallPolicyRuleLink("fpfr2", "fw2", "SiteOnly", "2");
    AddPolicySetFirewallPolicyLink("link2", "aps1", "fw2", "2");
    client->WaitForIdle();

    //Service group allows only UDP flow deny ICMP flow
    TxIpPacket(vnet[1]->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.2",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::PASS));

    FlowEntry* rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::PASS));

    std::vector<std::string> match;
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "deny");
    AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    client->WaitForIdle();

    EXPECT_TRUE(flow->match_p().action_info.action & (1 << TrafficAction::DENY));
    EXPECT_TRUE(rflow->match_p().action_info.action & (1 << TrafficAction::DENY));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
    DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");
    DelFirewallPolicyRuleLink("fpfr2", "fw2", "SiteOnly");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fw2");
    client->WaitForIdle();
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
