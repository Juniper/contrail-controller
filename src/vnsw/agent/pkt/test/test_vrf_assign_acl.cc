 /*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
};

IpamInfo ipam_info1[] = {
    {"1.1.1.0", 24, "1.1.1.10", true},
};

struct PortInfo input1[] = {
    {"intf3", 3, "2.1.1.1", "00:00:00:01:01:01", 2, 3},
    {"intf4", 4, "2.1.1.2", "00:00:00:01:01:02", 2, 4},
};

IpamInfo ipam_info2[] = {
    {"2.1.1.0", 24, "2.1.1.10", true},
};

struct PortInfo input2[] = {
    {"intf5", 5, "2.1.1.1", "00:00:00:01:01:01", 3, 5},
    {"intf6", 6, "2.1.1.2", "00:00:00:01:01:02", 3, 6},
};

IpamInfo ipam_info3[] = {
    {"2.1.1.0", 24, "2.1.1.10", true},
};

class TestVrfAssignAclFlow : public ::testing::Test {
protected:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        CreateVmportFIpEnv(input, 2);
        CreateVmportFIpEnv(input1, 2);
        CreateVmportFIpEnv(input2, 2);
        client->WaitForIdle();

        AddIPAM("default-project:vn1", ipam_info1, 1);
        AddIPAM("default-project:vn2", ipam_info2, 1);
        AddIPAM("default-project:vn3", ipam_info3, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        EXPECT_TRUE(VmPortActive(3));
        EXPECT_TRUE(VmPortActive(4));
        EXPECT_TRUE(VmPortActive(5));
        EXPECT_TRUE(VmPortActive(6));
        client->WaitForIdle();

        //Leak route for 1.1.1.0 to default-project:vn2:vn2 and
        //default-project:vn3:vn3
        VnListType vn_list;
        vn_list.insert(std::string("default-project:vn1"));
        Ip4Address ip2 = Ip4Address::from_string("1.1.1.0");
        agent_->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent_->local_peer(),
                               "default-project:vn2:vn2", ip2, 24, MakeUuid(1),
                               vn_list,
                               16, SecurityGroupList(),
                               CommunityList(), false,
                               PathPreference(), Ip4Address(0),
                               EcmpLoadBalance(), false, false);
        agent_->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent_->local_peer(),
                               "default-project:vn3:vn3", ip2, 24, MakeUuid(1),
                               vn_list,
                               16, SecurityGroupList(),
                               CommunityList(), false,
                               PathPreference(), Ip4Address(0),
                               EcmpLoadBalance(), false, false);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
        agent_->fabric_inet4_unicast_table()->DeleteReq(
                agent_->local_peer(), "default-project:vn1:vn1", ip1, 24, NULL);
        Ip4Address ip2 = Ip4Address::from_string("1.1.1.0");
        agent_->fabric_inet4_unicast_table()->DeleteReq(
                agent_->local_peer(), "default-project:vn2:vn2", ip2, 24, NULL);
        agent_->fabric_inet4_unicast_table()->DeleteReq(
                agent_->local_peer(), "default-project:vn3:vn3", ip2, 24, NULL);
        client->WaitForIdle();
        DeleteVmportFIpEnv(input, 2, true);
        DeleteVmportFIpEnv(input1, 2, true);
        DeleteVmportFIpEnv(input2, 2, true);
        DelIPAM("default-project:vn1");
        DelIPAM("default-project:vn2");
        DelIPAM("default-project:vn3");
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (0U == flow_proto_->FlowCount()));
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VmPortFindRetDel(2));
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(3));
        EXPECT_FALSE(VmPortFindRetDel(4));
        EXPECT_FALSE(VmPortFindRetDel(5));
        EXPECT_FALSE(VmPortFindRetDel(6));
        EXPECT_EQ(0U, flow_proto_->FlowCount());
    }
    Agent *agent_;
    FlowProto *flow_proto_;
};

//Add an VRF translate ACL to send all ssh traffic to "2.1.1.1"
//via default-project:vn2
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl1) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS))
        }
        }
    };
    CreateFlow(flow, 1);
}

//Add an VRF tranlsate ACL to send all traffic destined to "2.1.1.1"
//via default-project:vn3
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl2) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn3:vn3", "true");
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn3"),
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
    client->agent()->flow_stats_manager()->set_delete_short_flow(false);
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance(),
                           false, false);
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf4", "true");
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new ShortFlow()
        }
        }
    };
    CreateFlow(flow, 1);
    int nh_id = InterfaceTable::GetInstance()->FindInterface(VmPortGet(1)->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.1.1.1", IPPROTO_TCP,
                            10, 20, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_SRC_ROUTE);
    client->agent()->flow_stats_manager()->set_delete_short_flow(true);
}

//Add a VRF translate ACL to send all ssh traffic to "2.1.1.1" 
//via default-project:vn2 with ignore acl option set, add an ACL
//to deny all traffic from default-project:vn1 to default-project:vn2
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl4) {
    AddAcl("Acl", 10, "default-project:vn1", "default-project:vn2", "deny");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "false");
    client->WaitForIdle();
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::DENY) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::DENY))
        }
        }
    };
    CreateFlow(flow, 1);
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DelAcl("Acl");
}

//Add a SG acl to deny traffic from 1.1.1.0/24 subnet to 2.2.2.0/24
//and ensure it get applied
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl5) {
    AddAcl("egress-access-control-list-Acl", 10, "default-project:vn1",
           "default-project:vn2", "deny");
    AddSg("sg1", 1);
    AddLink("security-group", "sg1", "access-control-list",
            "egress-access-control-list-Acl");
    AddLink("virtual-machine-interface", "intf1", "security-group", "sg1");
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");
    client->WaitForIdle();
     TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::DENY) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::DENY))
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("virtual-machine-interface", "intf1", "security-group", "sg1");
    DelLink("security-group", "sg1", "access-control-list",
            "egress-access-control-list-Acl");
    DelNode("security-group", "sg1");
    DelAcl("egress-access-control-list-Acl");
}

//Send MplsoUdp packet to 1.1.1.1 from 3.1.1.1
//Assume packet from 3.1.1.1 is routed via SI on remote server
//Verify that RPF information is setup properly
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl6) {
    AddAddressVrfAssignAcl("intf1", 1, "3.1.1.0", "1.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "false");
    //Remote VM IP
    Ip4Address ip = Ip4Address::from_string("3.1.1.1");
    //Add route in vn1 saying 3.1.1.1 is reachable on remote server1
    //Add a route in vn2 saying 3.1.1.1 is reachable on remote server2
    Ip4Address remote_server1 = Ip4Address::from_string("10.1.1.100");
    Ip4Address remote_server2 = Ip4Address::from_string("10.1.1.101");

    Inet4TunnelRouteAdd(agent_->local_peer(), "default-project:vn1:vn1",
                        ip, 32, remote_server1, TunnelType::AllType(), 20,
                        "default-project:vn2", SecurityGroupList(),
                        PathPreference());

    Inet4TunnelRouteAdd(agent_->local_peer(), "default-project:vn2:vn2",
                        ip, 32, remote_server2, TunnelType::AllType(), 20,
                        "default-project:vn2", SecurityGroupList(),
                        PathPreference());
    client->WaitForIdle();

    VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(1));
    Ip4Address dest_vm_rt =  Ip4Address::from_string("1.1.1.0");
    uint32_t fwd_nh_id = RouteGet("default-project:vn2:vn2", ip, 32)->
                                GetActiveNextHop()->id();
    uint32_t rev_nh_id = RouteGet("default-project:vn2:vn2", dest_vm_rt, 24)->
                                  GetActiveNextHop()->id();

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "3.1.1.1", "1.1.1.1", IPPROTO_TCP, 10, 20,
                "default-project:vn1:vn1", "10.1.1.101", vm_intf->label()),
        {
            new VerifyVn("default-project:vn2", "default-project:vn1"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS)),
            new VerifyRpf(fwd_nh_id, rev_nh_id)
        }
        }
    };
    CreateFlow(flow, 1);

    //Delete the leaked routes
    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
            "default-project:vn2:vn2", ip, 32, NULL);
    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
            "default-project:vn1:vn1", ip, 32, NULL);
    client->WaitForIdle();
}

//Send MplsoUdp packet to 1.1.1.1 from 3.1.1.1
//Where 3.1.1.1 is a ecmp source and verify
//that flow becomes short flow since destination VRF is not present
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl7) {
    client->agent()->flow_stats_manager()->set_delete_short_flow(false);
    AddAddressVrfAssignAcl("intf1", 1, "3.1.1.0", "1.1.1.0", 6, 1, 65535,
                           1, 65535, "xyz", "no");
    //Remote VM IP
    Ip4Address ip = Ip4Address::from_string("3.1.1.1");

    ComponentNHKeyList comp_nh;
    Ip4Address server_ip1 = Ip4Address::from_string("10.1.1.100");
    Ip4Address server_ip2 = Ip4Address::from_string("10.1.1.101");

    ComponentNHKeyPtr comp_nh_data1(new ComponentNHKey(
                16, agent_->fabric_vrf_name(),
                agent_->router_id(), server_ip1, false,
                TunnelType::AllType()));
    comp_nh.push_back(comp_nh_data1);

    ComponentNHKeyPtr comp_nh_data2(new ComponentNHKey(
                17, agent_->fabric_vrf_name(),
                agent_->router_id(),
                server_ip2, false, TunnelType::AllType()));
    comp_nh.push_back(comp_nh_data2);

    EcmpTunnelRouteAdd(agent_->local_peer(), "default-project:vn1:vn1",
                       ip, 32, comp_nh, false,
                       "default-project:vn2", SecurityGroupList(),
                       PathPreference());

    EcmpTunnelRouteAdd(agent_->local_peer(), "default-project:vn2:vn2",
                       ip, 32, comp_nh, false,
                       "default-project:vn2", SecurityGroupList(),
                       PathPreference());
    client->WaitForIdle();

    VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(1));
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "3.1.1.1", "1.1.1.1", IPPROTO_TCP, 10, 20,
                "default-project:vn1:vn1", "10.1.1.101", vm_intf->label()),
        {
            new ShortFlow()
        }
        }
    };
    CreateFlow(flow, 1);

    //Delete the leaked routes
    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
            "default-project:vn2:vn2", ip, 32, NULL);
    agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
            "default-project:vn1:vn1", ip, 32, NULL);
    client->WaitForIdle();
    client->agent()->flow_stats_manager()->set_delete_short_flow(true);
}

TEST_F(TestVrfAssignAclFlow, VrfAssignAcl8) {
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.2");
    Ip4Address vm_ip = Ip4Address::from_string("2.1.1.1");
    Inet4TunnelRouteAdd(bgp_peer_, "default-project:vn1:vn1", vm_ip, 32,
                        server_ip, TunnelType::AllType(), 16,
                        "default-project:vn2",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
        }
        }
    };
    CreateFlow(flow, 1);

    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn3:vn3", "true");
    client->WaitForIdle();
    int nh_id = VmPortGet(1)->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.1.1.1", IPPROTO_TCP,
                            10, 20, nh_id);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->acl_assigned_vrf() == "default-project:vn3:vn3");
    DeleteRoute("default-project:vn1:vn1", "2.1.1.1", 32, bgp_peer_);
    client->WaitForIdle();
}

TEST_F(TestVrfAssignAclFlow, VrfAssignAcl9) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
            1, 65535, "default-project:vn3:vn3", "true");
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn3"),
            new VerifyAction((1 << TrafficAction::PASS) |
                    (1 << TrafficAction::VRF_TRANSLATE),
                    (1 << TrafficAction::PASS))
        }
        }
    };
    CreateFlow(flow, 1);

    int nh_id = VmPortGet(1)->flow_key_nh()->id();
    AddAddressVrfAssignAcl("intf1", 1, "2.1.1.0", "2.1.1.0", 7, 1, 65535,
                           1, 65535, "default-project:vn3:vn3", "true");
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.1.1.1", IPPROTO_TCP,
            10, 20, nh_id);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::ShortFlow) == true);
}

//Modify ACL and check if new flow is set with proper action
TEST_F(TestVrfAssignAclFlow, VrfAssignAcl10) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS))
        }
        }
    };
    CreateFlow(flow, 1);

    AddAddressVrfAssignAcl("intf1", 1, "2.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");
    TestFlow flow2[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new ShortFlow()
        }
        }
    };
    CreateFlow(flow2, 1);
}
//Add an VRF translate ACL to send all ssh traffic to "2.1.1.1"
//via default-project:vn2 and also add mirror ACL for VN1
//Verify that final action has mirror action also
TEST_F(TestVrfAssignAclFlow, VrfAssignAclWithMirror1) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");

    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance(),
                           false, false);
    client->WaitForIdle();

    AddMirrorAcl("Acl", 10, "default-project:vn1", "default-project:vn2", "pass",
                 "10.1.1.1");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    client->WaitForIdle();

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE) |
                             (1 << TrafficAction::MIRROR),
                             (1 << TrafficAction::PASS |
                              1 << TrafficAction::MIRROR))
        }
        }
    };
    CreateFlow(flow, 1);
    FlowEntry *entry = FlowGet(VmPortGet(1)->flow_key_nh()->id(),  "1.1.1.1",
                               "2.1.1.1", IPPROTO_TCP, 10, 20);
    EXPECT_TRUE(entry != NULL);
    if (entry != NULL) {
        WAIT_FOR(1000, 1000, (entry->IsOnUnresolvedList() == false));
    }
    EXPECT_TRUE(entry->ksync_entry()->old_first_mirror_index() == 0);
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DelAcl("Acl");
    client->WaitForIdle();
}

TEST_F(TestVrfAssignAclFlow, VrfAssignAclWithMirror2) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");

    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance(),
                           false, false);

    client->WaitForIdle();
    // pushing more DB Request so just before addin mirror entry 
    // so its creation gets delayed
    for (int i =0; i<500; i++) {
        std::ostringstream stream;
        stream << "Vn" << i;
        AddVn(stream.str().c_str(), i, false);
    }

    AddMirrorAcl("Acl", 10, "default-project:vn1", "default-project:vn2", "pass",
                 "10.1.1.1");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
        }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();
    FlowEntry *entry = FlowGet(VmPortGet(1)->flow_key_nh()->id(),  "1.1.1.1",
                               "2.1.1.1", IPPROTO_TCP, 10, 20);
    EXPECT_TRUE(entry != NULL);
    if (entry != NULL) {
        WAIT_FOR(1000, 1000, (entry->IsOnUnresolvedList() == false));
    }
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DelAcl("Acl");
    for (int i =0; i<500; i++) {
        std::ostringstream stream;
        stream<<"Vn"<<i;
        DelVn(stream.str().c_str());
    }
    client->WaitForIdle();
}

TEST_F(TestVrfAssignAclFlow, Vmi_Proxy_Arp_1) {
    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true");
    const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_EQ(vmi->proxy_arp_mode(), VmInterface::PROXY_ARP_NONE);

    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true", "left");
    EXPECT_EQ(vmi->proxy_arp_mode(), VmInterface::PROXY_ARP_UNRESTRICTED);

    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true", "right");
    EXPECT_EQ(vmi->proxy_arp_mode(), VmInterface::PROXY_ARP_UNRESTRICTED);

    AddAddressVrfAssignAcl("intf1", 1, "1.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "default-project:vn2:vn2", "true", "management");
    EXPECT_EQ(vmi->proxy_arp_mode(), VmInterface::PROXY_ARP_NONE);

    CreateVmportFIpEnv(input, 2);
    EXPECT_EQ(vmi->proxy_arp_mode(), VmInterface::PROXY_ARP_NONE);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    delete client;
    return ret;
}
