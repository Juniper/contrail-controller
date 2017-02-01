/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

#define AGE_TIME 10*1000

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input1[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2}
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
        CreateVmportWithEcmp(input1, 2);
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

        const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(1));
        vm1_label = vmi->label();
        vmi = static_cast<const VmInterface *>(VmPortGet(2));
        vm2_label = vmi->label();
        eth_intf_id = EthInterfaceGet("vnet0")->id();
    }
 
    virtual void TearDown() {
        DeleteVmportEnv(input1, 2, true);
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
    int vm2_label;
    int agg_label;
    int eth_intf_id;
};

//Send packet from ECMP VM to ECMP MX
//Verify component index is set and correspnding
//rpf nexthop
TEST_F(EcmpTest, EcmpTest_1) {
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4); 
    AddRemoteEcmpRoute("vrf1", "1.1.1.1", 32, "vn1", 4);

    TxIpPacket(VmPortGetId(1), "1.1.1.1", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);
    InetUnicastRouteEntry *src_rt = static_cast<InetUnicastRouteEntry *>(
        RouteGet("vrf1", Ip4Address::from_string("1.1.1.1"), 32));

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.1", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().nh.get() == src_rt->GetLocalNextHop());

    //Reverse flow is no ECMP
    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx != 
            CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().nh.get() == rt->GetActiveNextHop());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    sleep(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

//Send packet from ECMP of AAP and verify ECMP index and
//RPF nexthop is set fine
TEST_F(EcmpTest, EcmpTest_2) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.10");
    std::string mac("0a:0b:0c:0d:0e:0f");

    AddEcmpAap("vnet1", 1, ip, mac);
    AddEcmpAap("vnet2", 2, ip, mac);
    AddRemoteEcmpRoute("vrf1", "0.0.0.0", 0, "vn1", 4);
    client->WaitForIdle();

    TxIpPacket(VmPortGetId(1), "1.1.1.10", "2.1.1.1", 1);
    client->WaitForIdle();

    AgentRoute *rt = RouteGet("vrf1", Ip4Address::from_string("0.0.0.0"), 0);
    InetUnicastRouteEntry *src_rt = static_cast<InetUnicastRouteEntry *>(
            RouteGet("vrf1", Ip4Address::from_string("1.1.1.10"), 32));

    FlowEntry *entry = FlowGet(VrfGet("vrf1")->vrf_id(),
                               "1.1.1.10", "2.1.1.1", 1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(entry->data().nh.get() == src_rt->GetLocalNextHop());

    FlowEntry *rev_entry = entry->reverse_flow_entry();
    EXPECT_TRUE(rev_entry->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rev_entry->data().nh.get() == rt->GetActiveNextHop());

    DeleteRoute("vrf1", "0.0.0.0", 0, bgp_peer);
    client->WaitForIdle();
    sleep(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0));
}

TEST_F(EcmpTest, EcmpTest_3) {
    boost::scoped_ptr<InetInterfaceKey> key(new InetInterfaceKey("vhost0"));
    const InetInterface *vhost = static_cast<InetInterface *>(
            agent_->interface_table()->FindActiveEntry(key.get()));
    const VmInterface *vmi = static_cast<const VmInterface *>(VmPortGet(1));

    TxTcpPacket(vhost->id(), vhost->ip_addr().to_string().c_str(),
                vmi->mdata_ip_addr().to_string().c_str(), 100, 100, false, 0);
    client->WaitForIdle();

    FlowEntry *entry = FlowGet(0, vhost->ip_addr().to_string().c_str(),
                           vmi->mdata_ip_addr().to_string().c_str(), 
                           IPPROTO_TCP, 100, 100, vhost->flow_key_nh()->id());
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(entry->data().component_nh_idx !=
            CompositeNH::kInvalidComponentNHIdx);
    
    InetUnicastRouteEntry *rt = RouteGet("vrf1", vmi->primary_ip_addr(), 32);
    const CompositeNH *cnh = 
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->GetNH(entry->data().component_nh_idx) == 
                vmi->l3_interface_nh_no_policy());
   
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
