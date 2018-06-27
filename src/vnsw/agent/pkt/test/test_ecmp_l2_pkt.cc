/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "oper/ecmp.h"
#include <controller/controller_init.h>

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
        vm1_label = vmi->l2_label();
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
    void AddRemoteEcmpRoute(const string vrf_name,
                            const string mac_str,
                            const string ip_str,
                            const string vn,
                            int count,
                            bool reverse = false,
                            bool same_label = false) {
        //If there is a local route, include that always
        Ip4Address prefix = Ip4Address::from_string(ip_str);
        MacAddress mac(mac_str);

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

        DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
        nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                            comp_nh_list, "vrf1"));
        nh_req.data.reset(new CompositeNHData());
        VnListType vn_list;
        vn_list.insert("vn1");
        ControllerEcmpRoute *data =
            new ControllerEcmpRoute(bgp_peer, vn_list, EcmpLoadBalance(),
                                    TagList(), SecurityGroupList(),
                                    PathPreference(100, PathPreference::LOW, false,
                                                   false),
                                    (1 << TunnelType::MPLS_GRE),
                                    nh_req, mac.ToString());

        //ECMP create component NH
        EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer, "vrf1", mac, prefix,
                                             32, 0, data);
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
    AddRemoteEcmpRoute("vrf1", "00:00:00:09:09:09", "9.9.9.9",
                       "vn1", 4);
    client->WaitForIdle();

    TxL2Packet(VmPortGetId(1), "00:00:00:01:01:01", "00:00:00:09:09:09",
               "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = L2RouteGet("vrf1", MacAddress("00:00:00:09:09:09"));

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

    EvpnAgentRouteTable::DeleteReq(bgp_peer, "vrf1",
                                   MacAddress("00:00:00:09:09:09"),
                                   Ip4Address::from_string("9.9.9.9"), 32, 0,
                                   new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from ECMP MX to VM
//Verify:
//    Forward flow is non-ECMP
//    Reverse flow is ECMP. ECMP Index is not set
//    Reverse flow rpf next is Composite NH
TEST_F(EcmpTest, EcmpTest_2) {
    AddRemoteEcmpRoute("vrf1", "00:00:00:09:09:09", "9.9.9.9",
                       "vn1", 4);
    client->WaitForIdle();
    AgentRoute *rt = L2RouteGet("vrf1", MacAddress("00:00:00:09:09:09"));

    TxL2IpMplsPacket(eth_intf_id, MX_2, router_id, vm1_label,
                     "00:00:00:09:09:09", "00:00:00:01:01:01",
                     "2.1.1.1", "1.1.1.1", 1, 10);
    client->WaitForIdle();

    //Entry is keyed with nh corresponding to l2 mpls label and not flowkey nh.
    //Flow key nh is mapped to l3.
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "2.1.1.1", "1.1.1.1", 1, 0, 0,
            agent_->mpls_table()->FindMplsLabel(vm1_label)->nexthop()->id());
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

    EvpnAgentRouteTable::DeleteReq(bgp_peer, "vrf1",
                                   MacAddress("00:00:00:09:09:09"),
                                   Ip4Address::from_string("9.9.9.9"), 32, 0,
                                   new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

#if 0
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

    EXPECT_EQ(2, flow_proto_->FlowCount());
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetActiveNextHop());
    EXPECT_TRUE(rev_entry->data().rpf_nh->id() == GetFlowKeyNH(1));

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
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
#endif

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
