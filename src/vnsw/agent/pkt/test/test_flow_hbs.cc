/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include "test_cmn_util.h"
#include "test_flow_util.h"


TestTag src[] = {
    {"App1",  1, (TagTable::APPLICATION << TagEntry::kTagTypeBitShift) | 0x1},
    {"Tier1", 2, (TagTable::TIER << TagEntry::kTagTypeBitShift) | 0x1},
    {"Site1", 3, (TagTable::SITE << TagEntry::kTagTypeBitShift) | 0x1},
    {"NFWG1",  4, (TagTable::NEUTRON_FWAAS << TagEntry::kTagTypeBitShift) | 0x1},
};

TestTag dst[] = {
    {"App2",  5, (TagTable::APPLICATION << TagEntry::kTagTypeBitShift) | 0x2},
    {"Tier2", 6, (TagTable::TIER << TagEntry::kTagTypeBitShift) | 0x2},
    {"Site2", 7, (TagTable::SITE << TagEntry::kTagTypeBitShift) | 0x2},
};

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};
IpamInfo ipam_info[] = { {"1.1.1.0", 24, "1.1.1.10"}, };

class HbsFlowTest : public ::testing::Test {
public:
    virtual void SetUp() {
        client->WaitForIdle();
        //Create VM Ports
        CreateVmportEnv(input, 2);
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        CreateTags(src, 4);
        CreateTags(dst, 3);

        AddLink("virtual-machine-interface", "intf1", "tag", "App1");
        AddLink("virtual-machine-interface", "intf1", "tag", "Tier1");
        AddLink("virtual-machine-interface", "intf1", "tag", "Site1");
        AddLink("virtual-machine-interface", "intf1", "tag", "NFWG1");
        client->WaitForIdle();

        AddLink("virtual-machine-interface", "intf2", "tag", "App2");
        AddLink("virtual-machine-interface", "intf2", "tag", "Tier2");
        AddLink("virtual-machine-interface", "intf2", "tag", "Site2");
        client->WaitForIdle();


        std::vector<std::string> match;
        AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "false");
        client->WaitForIdle();

        AddNode("firewall-policy", "fw1", 1);
        AddNode("application-policy-set", "aps1", 1);
        AddLink("tag", "App1", "application-policy-set", "aps1");
        AddFwRuleTagLink("MatchAllTag", src, 3);
        AddFwRuleTagLink("MatchAllTag", dst, 3);
        client->WaitForIdle();
        AddFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag", "1");
        AddPolicySetFirewallPolicyLink("link1", "aps1", "fw1", "1");
    }

    virtual void TearDown() {
        DelIPAM("vn1");
        DeleteVmportEnv(input, 2, true);
        DeleteTags(src, 4);
        DeleteTags(dst, 3);

        DelLink("virtual-machine-interface", "intf1", "tag", "App1");
        DelLink("virtual-machine-interface", "intf1", "tag", "Tier1");
        DelLink("virtual-machine-interface", "intf1", "tag", "Site1");
        DelLink("virtual-machine-interface", "intf1", "tag", "NFWG1");
        DelLink("virtual-machine-interface", "intf2", "tag", "App2");
        DelLink("virtual-machine-interface", "intf2", "tag", "Tier2");
        DelLink("virtual-machine-interface", "intf2", "tag", "Site2");
        client->WaitForIdle();

        DelNode("firewall-rule", "MatchAllTag");
        client->WaitForIdle();

        DelNode("firewall-policy", "fw1");
        DelNode("application-policy-set", "aps1");
        DelLink("tag", "App1", "application-policy-set", "aps1");
        DelLink("tag", "App2", "application-policy-set", "aps1");
        DelFwRuleTagLink("MatchAllTag", src, 3);
        DelFwRuleTagLink("MatchAllTag", dst, 3);
        client->WaitForIdle();
        DelPolicySetFirewallPolicyLink("link1", "aps1", "fw1");
        DelFirewallPolicyRuleLink("fpfr1", "fw1", "MatchAllTag");

    }
};

//Verify HBS flag is not set in fwd and reverse flows if host based services
//are not enabled
TEST_F(HbsFlowTest, basic_10) {
    std::vector<std::string> match;

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    const VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));


    //Define ICMP flow between two VM interfaces
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    //Verify the R flag in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);
}

//Enable Host based services. Verify HBS Flag in the forward and reverse flow 
//between two Vms on same compute

TEST_F(HbsFlowTest, basic_11) {

    std::vector<std::string> match;
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "true");
    client->WaitForIdle();

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    const VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));


    //Define ICMP flow between two VM interfaces
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_LEFT);
    //Verify the R flag in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_RIGHT);
    //Disable HBS policy and check if the flow is reevaluated and HBS info
    //is reset in flow
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "false");
    client->WaitForIdle();

    //Verify if HBS flags are reset
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);
    client->WaitForIdle();
}

//Enable Host based services. Verify HBS Flag in the forward and reverse flow
//between VM and service VM

TEST_F(HbsFlowTest, basic_12) {
    std::vector<std::string> match;
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "true");
    client->WaitForIdle();

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));
    VmInterface *intf2 = static_cast<VmInterface *>(VmPortGet(2));

    //Mark the second interface as "left" service interface
    intf2->set_service_intf_type("left");
    //Define ICMP flow between VM and service interface
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_LEFT);
    //Verify HBS flag not set in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    //Disable HBS policy and check if the flow is reevaluated and HBS info
    //is reset in flow
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "false");
    client->WaitForIdle();

    //Verify if HBS flags are reset
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);
    client->WaitForIdle();

    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "true");
    client->WaitForIdle();
    intf2->set_service_intf_type("right");
    //Define ICMP flow between VM and service interfaces
    TestFlow flow1[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow1, 1);

    //Verify the R flag in forward flow
    fe = flow1[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_RIGHT);
    //Verify HBS flag not set in reverse flow
    rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    //Disable HBS policy and check if the flow is reevaluated and HBS info
    //is reset in flow
    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "false");
    client->WaitForIdle();

    //Verify if HBS flags are reset
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);
    client->WaitForIdle();

    AddFirewall("MatchAllTag", 1, match, src, 3, dst, 3, "pass", "<>", "true");
    client->WaitForIdle();
    intf2->set_service_intf_type("right");
    intf1->set_service_intf_type("left");
    //Define ICMP flow between two service interfaces
    TestFlow flow2[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow2, 1);

    //Verify the HBS flag not in forward flow
    fe = flow2[0].pkt_.FlowFetch();
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    //Verify HBS flag not set in reverse flow
    rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
}

int main(int argc, char **argv) {
    GETUSERARGS();

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
