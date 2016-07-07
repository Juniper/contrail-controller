/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

#define AGE_TIME 10*1000
#define VN2 "default-project:vn2"
#define VRF2 "default-project:vn2:vn2"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class FipEcmpTest : public ::testing::Test {
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
        AddVn(VN2, 2);
        AddVrf(VRF2);
        AddLink("virtual-network", VN2, "routing-instance",
                VRF2);
        client->WaitForIdle();
        //Attach floating-ip
        //Add floating IP for vnet1
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "2.1.1.1", "1.1.1.1");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
                VN2);
        client->WaitForIdle();
        AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
        client->WaitForIdle();

        strcpy(router_id, agent_->router_id().to_string().c_str());
        strcpy(MX_0, "100.1.1.1");
        strcpy(MX_1, "100.1.1.2");
        strcpy(MX_2, "100.1.1.3");
        strcpy(MX_3, "100.1.1.4");

        const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(1));
        vm1_label = vmi->label();
        eth_intf_id = EthInterfaceGet("vnet0")->id();
    }
 
    virtual void TearDown() {
        DelLink("virtual-network", VN2, "routing-instance",
                VRF2);
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelLink("floating-ip-pool", "fip-pool1",
                "virtual-network", VN2);
        DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool1");
        client->WaitForIdle();

        DeleteVmportEnv(input1, 1, true);
        DelVn(VN2);
        DelVrf(VRF2);
        client->WaitForIdle();
        DelIPAM("vn1");
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
                           comp_nh_list, -1, vn, sg_id_list,
                           PathPreference());
    }

    FlowProto *get_flow_proto() const { return flow_proto_; }
    Agent *agent_;
    Peer *bgp_peer;
    FlowProto *flow_proto_;
    AgentXmppChannel *channel;
    char router_id[80];
    char MX_0[80];
    char MX_1[80];
    char MX_2[80];
    char MX_3[80];
    int vm1_label;
    int eth_intf_id;
};

//Packet from VM to destination ECMP
TEST_F(FipEcmpTest, Test_1) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "8.8.8.8", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "8.8.8.8", 1, 0, 0,
                               GetFlowKeyNH(1));

    AgentRoute *rt = RouteGet(VRF2, Ip4Address::from_string("2.1.1.1"), 32);
 
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().nh.get() == rt->GetActiveNextHop());


    rt = RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0);
    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle(); 
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Packet from external ECMP source to fip
TEST_F(FipEcmpTest, Test_2) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);

    TxIpMplsPacket(eth_intf_id, MX_2, router_id, vm1_label,
                   "8.8.8.8", "2.1.1.1", 1, 10);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32);
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "8.8.8.8", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx == 2);
    EXPECT_TRUE(entry->data().nh.get() == rt->GetActiveNextHop());

    rt = RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0);
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
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
