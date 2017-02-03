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
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
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
        CreateVmportWithEcmp(input1, 2);
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
        vm1_mac = vmi->vm_mac();
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

        DeleteVmportEnv(input1, 2, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        DelVn(VN2);
        DelVrf(VRF2);
        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer);
        client->WaitForIdle();
        EXPECT_FALSE(VrfFind("vrf1", true));
        EXPECT_FALSE(VrfFind("vrf2", true));
        WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
        client->WaitForIdle();
    }
public:

    void AddLocalEcmpFip() {
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "2.1.1.1", "1.1.1.1");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
                VN2);
        client->WaitForIdle();
        AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
        client->WaitForIdle();
    }

    void DeleteLocalEcmpFip() {
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelLink("floating-ip-pool", "fip-pool1",
                "virtual-network", VN2);
        DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool1");
        client->WaitForIdle();
    }

    void AddRemoteEcmpFip() {
        //If there is a local route, include that always
        Ip4Address fip = Ip4Address::from_string("2.1.1.1");

        ComponentNHKeyList comp_nh_list;
        SecurityGroupList sg_id_list;
        ComponentNHKeyPtr comp_nh(new ComponentNHKey(
                    16, Agent::GetInstance()->fabric_vrf_name(),
                    Agent::GetInstance()->router_id(),
                    Ip4Address(0x64010101),
                    false, TunnelType::AllType()));
        ComponentNHKeyPtr comp_nh1(new ComponentNHKey(vm1_label,
                    MakeUuid(1), InterfaceNHFlags::INET4, vm1_mac));
        comp_nh_list.push_back(comp_nh1);

        EcmpTunnelRouteAdd(bgp_peer, VRF2, fip, 32,
                comp_nh_list, -1, VN2, sg_id_list,
                PathPreference());
    }

    void DeleteRemoteEcmpFip() {
        Ip4Address fip = Ip4Address::from_string("2.1.1.1");
        agent_->fabric_inet4_unicast_table()->
            DeleteReq(bgp_peer, VRF2, fip, 32,
                    new ControllerVmRoute(bgp_peer));
        client->WaitForIdle();
    }

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
    BgpPeer *bgp_peer;
    FlowProto *flow_proto_;
    AgentXmppChannel *channel;
    char router_id[80];
    char MX_0[80];
    char MX_1[80];
    char MX_2[80];
    char MX_3[80];
    int vm1_label;
    int ecmp_label;
    int eth_intf_id;
    MacAddress vm1_mac;
};

//Packet from VM with ECMP FIP to destination ECMP
//ECMP FIP resides on remote compute node
TEST_F(FipEcmpTest, Test_1) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);
    AddRemoteEcmpFip();

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "8.8.8.8", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "8.8.8.8", 1, 0, 0,
                               GetFlowKeyNH(1));

    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(
        RouteGet(VRF2, Ip4Address::from_string("2.1.1.1"), 32));
 
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetLocalNextHop());


    rt = static_cast<InetUnicastRouteEntry *>( 
        RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0));
    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 0, bgp_peer);
    DeleteRemoteEcmpFip();
    client->WaitForIdle(); 
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Packet from external ECMP source to fip in ECMP
//ECMP FIP resides on differnt compute node
TEST_F(FipEcmpTest, Test_2) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);
    //Add 2.1.1.1 route with ECMP of local and remote compute node
    AddRemoteEcmpFip();

    TxIpMplsPacket(eth_intf_id, MX_2, router_id, vm1_label,
                   "8.8.8.8", "2.1.1.1", 1, 10);
    client->WaitForIdle();

    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(
        RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "8.8.8.8", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetLocalNextHop());

    rt = static_cast<InetUnicastRouteEntry *>( 
        RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0));

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx == 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 0, bgp_peer);
    DeleteRemoteEcmpFip();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Packet from VM with ECMP FIP to destination ECMP
//Both FIP instance reside on same compute node
TEST_F(FipEcmpTest, Test_3) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);
    AddLocalEcmpFip();

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "8.8.8.8", 1);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "8.8.8.8", 1, 0, 0,
                               GetFlowKeyNH(1));

    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(
        RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
 
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                    CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetLocalNextHop());

    rt = static_cast<InetUnicastRouteEntry *>( 
        RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0));
    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 32, bgp_peer);
    DeleteLocalEcmpFip();
    client->WaitForIdle(); 
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Packet from external ECMP source to fip in ECMP
//Both FIP reside on same compute node
TEST_F(FipEcmpTest, Test_4) {
    AddRemoteEcmpRoute(VRF2, "0.0.0.0", 0, VN2, 4);
    AddRemoteEcmpFip();

    InetUnicastRouteEntry *rt = static_cast<InetUnicastRouteEntry *>(
            RouteGet(VRF2, Ip4Address::from_string("2.1.1.1"), 32));
    uint16_t ecmp_label = rt->GetActivePath()->GetActiveLabel();

    TxIpMplsPacket(eth_intf_id, MX_2, router_id, ecmp_label,
                   "8.8.8.8", "2.1.1.1", 1, 10);
    client->WaitForIdle();

    rt = static_cast<InetUnicastRouteEntry *>(
            RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
            "1.1.1.1", "8.8.8.8", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().rpf_nh.get() == rt->GetLocalNextHop());

    rt = static_cast<InetUnicastRouteEntry *>( 
        RouteGet(VRF2, Ip4Address::from_string("0.0.0.0"), 0));

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().rpf_nh.get() == rt->GetActiveNextHop());

    DeleteRoute(VRF2, "0.0.0.0", 0, bgp_peer);
    DeleteLocalEcmpFip();
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
