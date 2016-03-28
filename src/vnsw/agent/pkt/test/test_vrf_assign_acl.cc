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
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        CreateVmportFIpEnv(input, 2);
        CreateVmportFIpEnv(input1, 2);
        CreateVmportFIpEnv(input2, 2);
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
                               PathPreference(), Ip4Address(0), EcmpLoadBalance());
        agent_->fabric_inet4_unicast_table()->
            AddLocalVmRouteReq(agent_->local_peer(),
                               "default-project:vn3:vn3", ip2, 24, MakeUuid(1),
                               vn_list,
                               16, SecurityGroupList(),
                               CommunityList(), false,
                               PathPreference(), Ip4Address(0), EcmpLoadBalance());
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
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VmPortFindRetDel(2));
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
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
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
                             (1 << TrafficAction::DENY) |
                             (1 << TrafficAction::IMPLICIT_DENY))
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


TEST_F(TestVrfAssignAclFlow, FloatingIp) {
    struct PortInfo input[] = {
        {"intf7", 7, "4.1.1.1", "00:00:00:01:01:01", 4, 7},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    Ip4Address ip2 = Ip4Address::from_string("1.1.1.100");
    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
    client->WaitForIdle();

    //Add an ACL, such that for traffic from vn4:vn4 to default-project:vn2,
    //route lookup happens in internal VRF
    //(assume default-project:vn3 in test case)
    AddVrfAssignNetworkAcl("Acl", 10, "default-project:vn1",
                           "default-project:vn2", "pass",
                           "default-project:vn3:vn3");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    client->WaitForIdle();

    //Configure Floating-IP for intf7 in default-project:vn1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    AddLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    client->WaitForIdle();

    VnListType vn_list1;
    vn_list.insert(std::string("default-project:vn1"));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                "default-project:vn3:vn3", ip2, 32, MakeUuid(7),
                vn_list1, 16, SecurityGroupList(),
                CommunityList(), false, PathPreference(), Ip4Address(0),
                EcmpLoadBalance());
    client->WaitForIdle();

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "4.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "vrf4", VmPortGet(7)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn3"),
            new VerifyVrf("vrf4", "default-project:vn3:vn3"),
            new VerifyNat("2.1.1.1", "1.1.1.100", IPPROTO_TCP, 20, 10)
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn1");
    DelLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    DelLink("virtual-network", "default-project:vn1",
            "access-control-list", "Acl");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    agent_->fabric_inet4_unicast_table()->DeleteReq(
            agent_->local_peer(), "default-project:vn3:vn3", ip2, 32, NULL);
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

//Verify flow becomes short flow, when
//vrf assigned ACL doesnt have the routes
TEST_F(TestVrfAssignAclFlow, FloatingIp1) {
    struct PortInfo input[] = {
        {"intf7", 7, "4.1.1.1", "00:00:00:01:01:01", 4, 7},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
    client->WaitForIdle();

    AddVrf("vrf9");
    client->WaitForIdle();
    //Add an ACL, such that for traffic from vn4:vn4 to default-project:vn2,
    //route lookup happens in internal VRF
    //(assume default-project:vn3 in test case)
    AddVrfAssignNetworkAcl("Acl", 10, "default-project:vn1",
                           "default-project:vn2", "pass",
                           "vrf9");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    client->WaitForIdle();


    //Configure Floating-IP for intf7 in default-project:vn1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    AddLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    client->WaitForIdle();

     TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "4.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "vrf4", VmPortGet(7)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn2"),
            new VerifyVrf("vrf4", "default-project:vn3:vn3"),
            new VerifyNat("2.1.1.1", "1.1.1.100", IPPROTO_TCP, 20, 10)
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "default:vn4");
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    DeleteVmportEnv(input, 1, true);
    DelVrf("vrf9");
    client->WaitForIdle();
}

//Verify Interface VRF assign rule doesnt get applied for
//floating IP traslation
TEST_F(TestVrfAssignAclFlow, FloatingIp2) {
    struct PortInfo input[] = {
        {"intf7", 7, "4.1.1.1", "00:00:00:01:01:01", 4, 7},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    AddVrf("vrf9");
    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn1"));
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
    client->WaitForIdle();

    AddAddressVrfAssignAcl("intf7", 7, "4.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "vrf9", "true");
    client->WaitForIdle();

    //Configure Floating-IP for intf7 in default-project:vn1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    AddLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    client->WaitForIdle();

     TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "4.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "vrf4", VmPortGet(7)->id()),
        {
            new VerifyVn("default-project:vn1", "default-project:vn1"),
            new VerifyAction(1 << TrafficAction::PASS,
                             1 << TrafficAction::PASS),
            new VerifyNat("2.1.1.1", "1.1.1.100", IPPROTO_TCP, 20, 10)
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn1");
    DelLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    DelLink("virtual-network", "default-project:vn1",
            "access-control-list", "Acl");
    DeleteVmportEnv(input, 1, true);
    DelVrf("vrf9");
    client->WaitForIdle();
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
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
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
                             (1 << TrafficAction::DENY) | (1 << TrafficAction::IMPLICIT_DENY))
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DelAcl("Acl");
    client->WaitForIdle();
}

TEST_F(TestVrfAssignAclFlow, FloatingIp_1) {
    struct PortInfo input[] = {
        {"intf7", 7, "4.1.1.1", "00:00:00:01:01:01", 4, 7},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VnListType vn_list;
    vn_list.insert(std::string("default-project:vn2"));
    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                           "default-project:vn1:vn1", ip1, 24, MakeUuid(3),
                           vn_list, 16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0), EcmpLoadBalance());
    client->WaitForIdle();

    //Add an ACL, such that for traffic from vn4:vn4 to default-project:vn2,
    //route lookup happens in internal VRF, in this case VRF doesnt exist
    //and hence flow should be short flow
    AddVrfAssignNetworkAcl("Acl", 10, "default-project:vn1",
                           "default-project:vn2", "pass",
                           "default-project:vn3:xyz");
    AddLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    client->WaitForIdle();


    //Configure Floating-IP for intf7 in default-project:vn1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn1");
    AddLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    client->WaitForIdle();

     TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "4.1.1.1", "2.1.1.1", IPPROTO_TCP, 10, 20,
                       "vrf4", VmPortGet(7)->id()),
        {
            new ShortFlow()
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn1");
    DelLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    DelLink("virtual-network", "default-project:vn1",
            "access-control-list", "Acl");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

//Ensure DNAT packet doesnt get VRF translated
TEST_F(TestVrfAssignAclFlow, FloatingIp3) {
    struct PortInfo input[] = {
        {"intf7", 7, "4.1.1.1", "00:00:00:01:01:01", 4, 7},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    char FIP_VN[80] = "default-project:vn1";
    char FIP_VRF[80] = "default-project:vn1:vn1";
    char EXT_IP_VN[80] = "default-project:vn2";
    char ACL_ASSIGNED_VRF[80] = "default-project:vn3:vn3";

    //Leak route for 2.1.1.0 to default-project:vn1:vn1
    Ip4Address ip1 = Ip4Address::from_string("2.1.1.0");
    Ip4Address ip2 = Ip4Address::from_string("1.1.1.100");
    VnListType vn_list;
    vn_list.insert(std::string(EXT_IP_VN));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(), FIP_VRF,
                           ip1, 24, MakeUuid(3), vn_list,
                           16, SecurityGroupList(),
                           CommunityList(), false,
                           PathPreference(), Ip4Address(0),
                           EcmpLoadBalance());
    client->WaitForIdle();

    AddVrfAssignNetworkAcl("Acl", 10, EXT_IP_VN, FIP_VN, "pass",
                           ACL_ASSIGNED_VRF);
    AddLink("virtual-network", FIP_VN, "access-control-list", "Acl");
    client->WaitForIdle();

    //Configure Floating-IP for intf7 in default-project:vn1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", FIP_VN);
    AddLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    client->WaitForIdle();

    vn_list.clear();
    vn_list.insert(std::string(FIP_VN));
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                "default-project:vn3:vn3", ip2, 32, MakeUuid(7),
                vn_list, 16, SecurityGroupList(),
                CommunityList(), false, PathPreference(), Ip4Address(0),
                EcmpLoadBalance());
    client->WaitForIdle();

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "2.1.1.1", "1.1.1.100", IPPROTO_TCP, 10,
                       20, "vrf4", "10.1.1.1", VmPortGet(7)->label()),
        {
            new VerifyNat("4.1.1.1", "2.1.1.1", IPPROTO_TCP, 20, 10),
            new VerifyVrf("vrf4", "vrf4"),
            //Network ACL rule doesnt allow reverse traffic, hence
            //deny for reverse flow
            new VerifyAction(1 << TrafficAction::PASS,
                            (1 << TrafficAction::DENY) |
                            (1 << TrafficAction::IMPLICIT_DENY))
        }
        }
    };
    CreateFlow(flow, 1);

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn1");
    DelLink("virtual-machine-interface", "intf7", "floating-ip", "fip1");
    DelLink("virtual-network", "default-project:vn1",
            "access-control-list", "Acl");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    agent_->fabric_inet4_unicast_table()->DeleteReq(
            agent_->local_peer(), "default-project:vn3:vn3", ip2, 32, NULL);
    DelLink("virtual-network", "default-project:vn1", "access-control-list", "Acl");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
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
