/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "ksync/ksync_sock_user.h"

#define AGE_TIME 10*1000

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class EcmpTest : public ::testing::Test {
    virtual void SetUp() {
        agent_ = Agent::GetInstance();

        boost::system::error_code ec;
        bgp_peer = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        client->WaitForIdle();

        flow_proto_ = agent_->pkt()->get_flow_proto();
        CreateVmportWithEcmp(input1, 1);
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        AddVn("vn2", 2);
        AddVrf("vrf2");
        client->WaitForIdle();
        strcpy(router_id, agent_->router_id().to_string().c_str());
        strcpy(MX_0, "100.1.1.1");
        strcpy(MX_1, "100.1.1.2");
        strcpy(MX_2, "100.1.1.3");
        strcpy(MX_3, "100.1.1.4");

        vmi = static_cast<VmInterface *>(VmPortGet(1));
        vm1_label = vmi->label();
        eth_intf_id = EthInterfaceGet("vnet0")->id();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input1, 1, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        DelVn("vn2");
        DelVrf("vrf2");
        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer);
        client->WaitForIdle();
        EXPECT_FALSE(VrfFind("vrf1", true));
        EXPECT_FALSE(VrfFind("vrf2", true));
        WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
        client->WaitForIdle();
    }
public:
    void AddRemoteEcmpRoute(const string vrf_name, const string ip,
            uint32_t plen, const string vn, int count, bool reverse = false,
            bool same_label = false) {
        //If there is a local route, include that always
        Ip4Address vm_ip = Ip4Address::from_string(ip);

        ComponentNHKeyList comp_nh_list;
        int remote_server_ip = 0x64010101;
        int label = 16;
        SecurityGroupList sg_id_list;

        for(int i = 0; i < count; i++) {
            ComponentNHKeyPtr comp_nh(new ComponentNHKey(
                        label, Agent::GetInstance()->fabric_vrf_name(),
                        Agent::GetInstance()->router_id(),
                        Ip4Address(remote_server_ip++),
                        false, TunnelType::AllType()));
            comp_nh_list.push_back(comp_nh);
            if (!same_label) {
                label++;
            }
        }
        if (reverse) {
            std::reverse(comp_nh_list.begin(), comp_nh_list.end());
        }

        EcmpTunnelRouteAdd(bgp_peer, vrf_name, vm_ip, plen,
                           comp_nh_list, -1, vn, sg_id_list, TagList(),
                           PathPreference());
    }

    FlowProto *get_flow_proto() const { return flow_proto_; }
    Agent *agent_;
    BgpPeer *bgp_peer;
    FlowProto *flow_proto_;
    AgentXmppChannel *channel;
    char router_id[80];
    char MX_0[80];
    char MX_1[80];
    char MX_2[80];
    char MX_3[80];
    int vm1_label;
    int eth_intf_id;
    VmInterface *vmi;
};

//Send packet from VM to ECMP MX
//Verify component index is set and correspnding
//rpf nexthop
TEST_F(EcmpTest, EcmpTest_1) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4);

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from ECMP MX to VM
//Verify:
//    Forward flow is non-ECMP
//    Reverse flow is ECMP. ECMP Index is not set
//    Reverse flow rpf next is Composite NH
TEST_F(EcmpTest, EcmpTest_2) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4);

    TxIpMplsPacket(eth_intf_id, MX_2, router_id, vm1_label,
                   "8.8.8.8", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "8.8.8.8", "1.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->IsEcmpFlow());
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    //Reverse flow is ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->IsEcmpFlow());
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh->id() == GetFlowKeyNH(1));

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from MX3 to VM
//Verify that index are set fine
TEST_F(EcmpTest, EcmpTest_3) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4);

    TxIpMplsPacket(eth_intf_id, MX_3, router_id, vm1_label,
                   "8.8.8.8", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "8.8.8.8", "1.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->IsEcmpFlow());
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    //Reverse flow is ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->IsEcmpFlow());
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh->id() == GetFlowKeyNH(1));

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from MX1 to VM
//Send one more flow setup message from MX2 to VM
//verify that component index gets update
TEST_F(EcmpTest, EcmpTest_4) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4);

    TxIpMplsPacket(eth_intf_id, MX_0, router_id, vm1_label,
                   "8.8.8.8", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "8.8.8.8", "1.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->IsEcmpFlow());
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());


    //Reverse flow is ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->IsEcmpFlow());
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh->id() == GetFlowKeyNH(1));

    TxIpMplsPacket(eth_intf_id, MX_2, router_id, vm1_label,
                   "8.8.8.8", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    EXPECT_EQ(2U, flow_proto_->FlowCount());
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());
    EXPECT_TRUE(rev_entry->data().rpf_nh->id() == GetFlowKeyNH(1));

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from MX to VM
//  Fwd flow has no vrf assign ACL
//  Reverese flow has vrf assign ACL
//  RPF NH must be based on reverse-flow
TEST_F(EcmpTest, EcmpTest_5) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn2", 4);
    //Reverse all the nexthop in vrf2
    AddRemoteEcmpRoute("vrf2", "0.0.0.0", 0, "vn2", 4, true);

    VnListType vn_list;
    vn_list.insert("vn1");
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(agent_->local_peer(),
                "vrf2", ip, 32, MakeUuid(1), vn_list,
                vm1_label, SecurityGroupList(), TagList(),
                CommunityList(), false, PathPreference(),
                Ip4Address(0), EcmpLoadBalance(),
                false, false, false);
    client->WaitForIdle();

    AddVrfAssignNetworkAcl("Acl", 10, "vn1", "vn2", "pass", "vrf2");
    AddLink("virtual-network", "vn1", "access-control-list", "Acl");
    client->WaitForIdle();

    TxIpMplsPacket(eth_intf_id, MX_3, router_id, vmi->label(),
                   "8.8.8.8", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf2", Ip4Address::from_string("0.0.0.0"), 0);
    // RPF-NH in forward flow is based on translated VRF in reverse flow
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(), "8.8.8.8", "1.1.1.1",
                               1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == vmi->flow_key_nh());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    DeleteRoute("vrf2", "0.0.0.0", 0, bgp_peer);
    DeleteRoute("vrf2", "1.1.1.1", 32, agent_->local_peer());
    DelLink("virtual-network", "vn1", "access-control-list", "Acl");
    DelAcl("Acl");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Floating IP traffic from VM going to ECMP MX
TEST_F(EcmpTest, EcmpTest_6) {
    //Setup
    //Add IP 2.1.1.1 as floating IP to 1.1.1.1
    //Make address 8.8.8.8 reachable on 4 MX
    //Send traffic from MX3 to FIP
    //Verify RPF nh and component index
    AddVn("fip", 3);
    AddVrf("fip:fip");
    AddLink("virtual-network", "fip", "routing-instance", "fip:fip");
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.1");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "fip");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    AddRemoteEcmpRoute("fip:fip", "0.0.0.0", 0, "fip", 4);
    client->WaitForIdle();

    TxIpMplsPacket(eth_intf_id, MX_3, router_id, vmi->label(),
                   "8.8.8.8", "2.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("fip:fip", Ip4Address::from_string("0.0.0.0"), 0);
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "8.8.8.8", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    //Clean up
    DeleteRoute("fip:fip", "0.0.0.0", 0, bgp_peer);
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "fip");
    DelNode("floating-ip", "fip1");
    DelNode("floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "fip", "routing-instance", "fip:fip");
    DelVrf("fip:fip");
    DelVn("fip");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Floating IP traffic from VM going to ECMP MX with
//VRF translation to vrf2
TEST_F(EcmpTest, EcmpTest_7) {
    //Setup
    //Add IP 2.1.1.1 as floating IP to 1.1.1.1
    //Make address 8.8.8.8 reachable on 4 MX
    //Send traffic from MX3 to FIP with vrf translation
    //Verify RPF nh and component index
    AddVn("fip", 3);
    AddVrf("fip:fip");
    AddLink("virtual-network", "fip", "routing-instance", "fip:fip");
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.1");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "fip");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    AddRemoteEcmpRoute("fip:fip", "0.0.0.0", 0, "fip", 4);
    //Add the routes in vrf2 in reverese order
    AddRemoteEcmpRoute("vrf2", "0.0.0.0", 0, "fip", 4, true);
    client->WaitForIdle();

    AddVrfAssignNetworkAcl("Acl", 10, "fip", "fip", "pass", "vrf2");
    AddLink("virtual-network", "fip", "access-control-list", "Acl");
    client->WaitForIdle();

    TxIpMplsPacket(eth_intf_id, MX_3, router_id, vmi->label(),
                   "8.8.8.8", "2.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf2", Ip4Address::from_string("0.0.0.0"), 0);
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "8.8.8.8", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    //Clean up
    DeleteRoute("fip:fip", "0.0.0.0", 0, bgp_peer);
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "fip");
    DelNode("floating-ip", "fip1");
    DelNode("floating-ip-pool", "fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "fip", "routing-instance", "fip:fip");
    DelVrf("fip:fip");
    DelVn("fip");
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Flow move from remote destination to local compute node
//1> Initially flow are setup from MX to local source VM
//2> Move the flow from MX to local destination VM to local source VM
//3> Verify that old flow from MX would be marked as short flow
//   and new set of local flows are marked as forward
TEST_F(EcmpTest, EcmpTest_8) {
    struct PortInfo input[] = {
        {"vnet2", 2, "1.1.1.100", "00:00:00:01:01:02", 1, 1},
    };
    CreateVmportWithEcmp(input, 1);
    client->WaitForIdle();

    AddRemoteEcmpRoute("vrf1", "1.1.1.100", 32, "vn1", 4);
    TxIpMplsPacket(eth_intf_id, MX_0, router_id, vm1_label,
                   "1.1.1.100", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "1.1.1.100", 1, 0, 0, GetFlowKeyNH(1));
    FlowEntry *rev_entry = entry->reverse_flow_entry();

    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->IsShortFlow() == false);
    EXPECT_TRUE(rev_entry->IsShortFlow() == false);

    TxIpPacket(VmPortGetId(2), "1.1.1.100", "1.1.1.1", 1);
    client->WaitForIdle();

    entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                    "1.1.1.1", "1.1.1.100", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->IsShortFlow() == false);
    EXPECT_TRUE(rev_entry->IsShortFlow() == true);
    EXPECT_TRUE(entry->reverse_flow_entry()->IsShortFlow() == false);

    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    DeleteRoute("vrf1", "1.1.1.100", 32, bgp_peer);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Test to simulate continous stream of flow packets getting evicted
//To run the test, please modify KSyncUserSockFlowContext::Process
//to return same flow handle and incremental gen_id
//
//+  static uint8_t gen_id = 0;
//+  fwd_flow_idx = 100;
//   req_->set_fr_index(fwd_flow_idx);
//-  req_->set_fr_gen_id((fwd_flow_idx % 255));
//+  req_->set_fr_gen_id(gen_id++);
//
//and to pass ttl as gen id in below API
#if 0
TEST_F(EcmpTest, TEST_1) {
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");
    Ip4Address zero = Ip4Address::from_string("0.0.0.0");
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    PathPreference path_preference(0, PathPreference::HIGH, false, false);
    Inet4TunnelRouteAdd(bgp_peer, "vrf1", zero, 0, server_ip, bmap,
                        16, "vn1", SecurityGroupList(), path_preference);
    client->WaitForIdle();

    uint32_t gen_id = 0;
    for (uint32_t i = 0; i < 10000; i++) {
        gen_id++;
        TxTcpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1",
                    10, 15, false, 1, VrfGet("vrf1")->vrf_id(), gen_id);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                                   "1.1.1.1", "2.1.1.1", 6, 10, 15,
                                   GetFlowKeyNH(1));
        client->WaitForIdle();

        if (entry) {
            entry->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
            KSyncSockTypeMap::SetEvictedFlag(entry->reverse_flow_entry()->
                                             flow_handle());
        }
        client->WaitForIdle();

        if (gen_id % 10 == 0) {
            WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
        }
    }

    WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
}
#endif

//In case of ECMP in fabric mode egress packet comes without any
//label in that case ecmp index has to be set such that only
//local nexthops are chosen. Verify that happens
TEST_F(EcmpTest, FabricVmi) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.10", "00:00:00:01:01:01", 1, 2},
        {"intf3", 3, "1.1.1.10", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    AddVn(agent_->fabric_vn_name().c_str(), 100);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
        InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry*>(RouteGet(agent_->fabric_vrf_name(),
                    ip, 32));

    Ip4Address remote_address = Ip4Address::from_string("10.10.10.100");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent_->fabric_vrf_name(),
                agent_->router_id(), remote_address, false, TunnelType::NativeType()));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(bgp_peer, agent_->fabric_vrf_name(), ip, 32,
                       comp_nh_list, -1, "vn1",
                       sg_list, TagList(), PathPreference(), true);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(agent_->fabric_interface_name());
    Interface *intf = static_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    for (uint32_t i = 0; i < 100; i++) {
        TxTcpPacket(0, "2.1.1.1", "1.1.1.10", i, i, false, i, 0);
        client->WaitForIdle();

        FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                "2.1.1.1", "1.1.1.10", 6, i, i, intf->flow_key_nh()->id());
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->IsShortFlow() == false);
        EXPECT_TRUE(entry->data().component_nh_idx != 2);
    }


    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
    agent_->fabric_inet4_unicast_table()->DeleteReq(bgp_peer,
            agent_->fabric_vrf_name().c_str(), ip, 32,
            new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "virtual-network",
            agent_->fabric_vn_name().c_str());
    client->WaitForIdle();

    DelNode("virtual-network", agent_->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet(agent_->fabric_vrf_name(), ip, 32) == NULL);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
