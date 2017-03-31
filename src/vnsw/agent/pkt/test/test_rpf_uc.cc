/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
// File to test RPF in case of unicast-l3 and unicast-l2 flows

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include <algorithm>

#define VM1_IP "1.1.1.1"
#define VM1_MAC "00:00:00:01:01:01"

#define VM2_IP "1.1.1.2"
#define VM2_MAC "00:00:00:01:01:02"

#define VM3_IP "1.1.1.3"
#define VM3_MAC "00:00:00:01:01:03"

#define REMOTE_VM1_IP "1.1.1.10"
#define REMOTE_VM1_MAC "00:00:00:02:02:01"

#define REMOTE_VM2_IP "1.1.1.100"
#define REMOTE_VM2_MAC "00:00:00:10:10:01"

#define REMOTE_FIP1_IP "2.2.2.101"
#define unknown_ip_1 "1.1.1.10"
#define unknown_mac_1 "00:FF:FF:FF:FF:00"

struct PortInfo input[] = {
    {"vmi0", 1, VM1_IP, VM1_MAC, 1, 1},
    {"vmi1", 2, VM2_IP, VM2_MAC, 1, 2},
};
IpamInfo ipam_info_1[] = {
    {"1.1.1.0", 24, "1.1.1.254"},
};

class FlowRpfTest : public ::testing::Test {
public:
    FlowRpfTest() : agent_(Agent::GetInstance()), peer_(NULL) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        eth0_ = EthInterfaceGet("vnet0");
        strcpy(router_id_, agent_->router_id().to_string().c_str());
    }

    virtual void SetUp() {
        CreateVmportEnv(input, 2, 1);
        AddIPAM("vn1", ipam_info_1, 1);
        client->WaitForIdle();

        for (int i = 0; i < 2; i++) {
            EXPECT_TRUE(VmPortActive(input, i));
            EXPECT_TRUE(VmPortPolicyEnable(input, i));
        }

        EXPECT_EQ(5U, agent_->interface_table()->Size());
        EXPECT_EQ(2U, agent_->vm_table()->Size());
        EXPECT_EQ(1U, agent_->vn_table()->Size());

        vmi0_ = VmInterfaceGet(input[0].intf_id);
        assert(vmi0_);
        vmi1_ = VmInterfaceGet(input[1].intf_id);
        assert(vmi1_);

        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        client->WaitForIdle();

        CreateRemoteRoute("vrf1", REMOTE_VM1_IP, 32, "100.100.100.1", 10,
                          "vn1");
        CreateL2RemoteRoute("vrf1", REMOTE_VM1_MAC, "100.100.100.1", 20,
                            "vn1");
        client->WaitForIdle();

        vn1_ = VnGet(1);
        vrf1_ = VrfGet("vrf1");
        EXPECT_TRUE(vn1_ != NULL);

        FlowStatsTimerStartStop(agent_, true);
    }

    virtual void TearDown() {
        DeleteRemoteRoute("vrf1", REMOTE_VM1_IP, 32);
        DeleteL2RemoteRoute("vrf1", REMOTE_VM1_MAC);
        client->WaitForIdle();

        DelIPAM("vn1");
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();

        FlushFlowTable();
        WAIT_FOR(1000, 100, (flow_proto_->FlowCount() == 0));

        for (int i = 0; i < 3; i++) {
            EXPECT_FALSE(VmPortFind(input, i));
        }
        DeleteBgpPeer(peer_);
        FlushFlowTable();
        client->WaitForIdle();

        EXPECT_EQ(3U, agent_->interface_table()->Size());
        EXPECT_EQ(0U, agent_->vm_table()->Size());
        EXPECT_EQ(0U, agent_->vn_table()->Size());
        EXPECT_EQ(0U, agent_->acl_table()->Size());
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        FlowStatsTimerStartStop(agent_, false);
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           uint8_t plen, const char *srvr, int label,
                           const char *vn) {
        Ip4Address addr = Ip4Address::from_string(remote_vm);
        Ip4Address gw = Ip4Address::from_string(srvr);
        Inet4TunnelRouteAdd(peer_, vrf, addr, plen, gw, TunnelType::MplsType(),
                            label, vn, SecurityGroupList(), PathPreference());
        client->WaitForIdle();
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, plen) == true));
    }

    void CreateRemoteEcmpRoute(const char *vrf_name, const char *ip,
                               uint32_t plen, const char *vn, int count) {
        Ip4Address vm_ip = Ip4Address::from_string(ip);
        ComponentNHKeyList comp_nh_list;
        SecurityGroupList sg_id_list;
        int remote_server_ip = 0x0A0A0A0A;
        int label = 16;

        for(int i = 0; i < count; i++) {
            ComponentNHKeyPtr comp_nh
                (new ComponentNHKey(label, agent_->fabric_vrf_name(),
                                    agent_->router_id(),
                                    Ip4Address(remote_server_ip++),
                                    false, TunnelType::GREType()));
            comp_nh_list.push_back(comp_nh);
            label++;
        }
        EcmpTunnelRouteAdd(peer_, vrf_name, vm_ip, plen, comp_nh_list, -1, vn,
                           sg_id_list, PathPreference());
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip, uint8_t plen) {
        Ip4Address addr = Ip4Address::from_string(ip);
        agent_->fabric_inet4_unicast_table()->DeleteReq
            (peer_, vrf, addr, plen,
             new ControllerVmRoute(static_cast<BgpPeer *>(peer_)));
        client->WaitForIdle();
        WAIT_FOR(1000, 100, (RouteFind(vrf, addr, 32) == false));
    }

    void CreateL2RemoteRoute(const char *vrf, const char *mac,
                             const char *srvr, int label, const char *vn) {
        Ip4Address addr = Ip4Address::from_string(srvr);

        MacAddress m = MacAddress::FromString(mac);
        BridgeTunnelRouteAdd(peer_, vrf, TunnelType::AllType(), addr, label,
                             m, Ip4Address(0), 32);
        client->WaitForIdle();
        WAIT_FOR(1000, 500, (L2RouteFind(vrf, m) == true));
    }

    void DeleteL2RemoteRoute(const char *vrf, const char *mac) {
        MacAddress m = MacAddress::FromString(mac);
        EvpnAgentRouteTable::DeleteReq
            (peer_, vrf, m, Ip4Address(0), 0,
             new ControllerVmRoute(static_cast<BgpPeer *>(peer_)));
        client->WaitForIdle();
        WAIT_FOR(1000, 500, (L2RouteFind(vrf, m) == false));
    }

protected:
    Agent *agent_;
    FlowProto *flow_proto_;
    BgpPeer *peer_;
    PhysicalInterface *eth0_;
    char router_id_[64];
    VmInterface *vmi0_;
    VmInterface *vmi1_;
    VmInterface *flow2_;
    VnEntry *vn1_;
    VrfEntry *vrf1_;
};

// Disable RPF on VN, verify that flows created on VN have RPF disabled
TEST_F(FlowRpfTest, Cfg_DisableRpf_) {
    DisableRpf("vn1", 1);
    EXPECT_FALSE(vn1_->enable_rpf());

    TxIpPacket(vmi0_->id(), VM1_IP, VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_FALSE(flow->data().enable_rpf);
    EXPECT_TRUE(flow->rpf_nh() == NULL);

    EXPECT_FALSE(rflow->data().enable_rpf);
    EXPECT_TRUE(rflow->rpf_nh() == NULL);

    EnableRpf("vn1", 1);
    EXPECT_TRUE(vn1_->enable_rpf());
}

// Setup flow with RPF enabled
// Change the rpf from enable to disable in VN
// Verify that RPF is disabled on flow
TEST_F(FlowRpfTest, Cfg_EnableDisableRpf) {
    TxIpPacket(vmi0_->id(), VM1_IP, VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(flow->data().enable_rpf);
    EXPECT_TRUE(flow->rpf_nh() != NULL);

    EXPECT_TRUE(rflow->data().enable_rpf);
    EXPECT_TRUE(rflow->rpf_nh() != NULL);

    // Disable RPF
    DisableRpf("vn1", 1);
    client->WaitForIdle();

    // Verify RPF disabled on the flow
    WAIT_FOR(1000, 1000, (flow->data().enable_rpf == false));
    WAIT_FOR(1000, 1000, (flow->rpf_nh() == NULL));

    WAIT_FOR(1000, 1000, (rflow->data().enable_rpf == false));
    WAIT_FOR(1000, 1000, (rflow->rpf_nh() == NULL));

    EnableRpf("vn1", 1);
    EXPECT_TRUE(vn1_->enable_rpf());

    // RPF must be enabled on flows again
    WAIT_FOR(1000, 1000, (flow->data().enable_rpf));
    WAIT_FOR(1000, 1000, (flow->rpf_nh() != NULL));

    WAIT_FOR(1000, 1000, (rflow->data().enable_rpf));
    WAIT_FOR(1000, 1000, (rflow->rpf_nh() != NULL));
}

// Setup flow with RPF disabled. Change rpf to enable
// Verify that RPF is enabled on flow
TEST_F(FlowRpfTest, Cfg_DisableEnableRpf) {
    // Disable RPF
    DisableRpf("vn1", 1);
    client->WaitForIdle();

    TxIpPacket(vmi0_->id(), VM1_IP, VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_FALSE(flow->data().enable_rpf);
    EXPECT_TRUE(flow->rpf_nh() == NULL);
    // src_ip_nh must still be set
    EXPECT_TRUE(flow->data().src_ip_nh.get() != NULL);

    EXPECT_FALSE(rflow->data().enable_rpf);
    EXPECT_TRUE(rflow->rpf_nh() == NULL);
    // src_ip_nh must still be set
    EXPECT_TRUE(rflow->data().src_ip_nh.get() != NULL);

    // Disable RPF
    EnableRpf("vn1", 1);
    client->WaitForIdle();

    // Verify RPF enabled on the flow
    WAIT_FOR(1000, 1000, (flow->data().enable_rpf == true));
    WAIT_FOR(1000, 1000, (flow->rpf_nh() != NULL));

    WAIT_FOR(1000, 1000, (rflow->data().enable_rpf == true));
    WAIT_FOR(1000, 1000, (rflow->rpf_nh() != NULL));
}

// Add floating IP for a vmi0_ interface and disable
// rpf on the VN from where floating IP is enabled
// Verify rpf check is disabled
TEST_F(FlowRpfTest, Cfg_FipDisableRpf) {
    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    //Add floating IP for vnet1
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100", VM1_IP);
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vmi0", "floating-ip", "fip1");
    client->WaitForIdle();

    Ip4Address floating_ip = Ip4Address::from_string("2.1.1.100");
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", floating_ip, 32));
    EXPECT_TRUE(VmPortFloatingIpCount(1, 1));

    CreateRemoteRoute("default-project:vn2:vn2", REMOTE_FIP1_IP, 32,
                      "100.100.100.1", 10, "default-project:vn2");
    client->WaitForIdle();

    DisableRpf("default-project:vn2", 2);
    client->WaitForIdle();
    const VnEntry *vn2 = VnGet(2);
    EXPECT_FALSE(vn2->enable_rpf());

    TxIpPacket(vmi0_->id(), VM1_IP, REMOTE_FIP1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_FIP1_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->is_flags_set(FlowEntry::NatFlow));
    EXPECT_TRUE(rflow->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(flow->data().enable_rpf);
    EXPECT_FALSE(rflow->data().enable_rpf);
    EXPECT_TRUE(flow->rpf_nh() == NULL);
    EXPECT_TRUE(rflow->rpf_nh() == NULL);

    EnableRpf("default-project:vn2", 2);
    EXPECT_TRUE(vn2->enable_rpf());

    EXPECT_TRUE(flow->is_flags_set(FlowEntry::NatFlow));
    EXPECT_TRUE(rflow->is_flags_set(FlowEntry::NatFlow));
    EXPECT_TRUE(flow->data().enable_rpf);
    EXPECT_TRUE(rflow->data().enable_rpf);
    EXPECT_TRUE(flow->rpf_nh() != NULL);
    EXPECT_TRUE(rflow->rpf_nh() != NULL);

    DeleteRoute("default-project:vn2:vn2", "0.0.0.0", 0);
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1",
            "virtual-network", "default-project:vn2");
    DelLink("virtual-machine-interface", "vmi0", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    DelFloatingIpPool("fip-pool1");
    client->WaitForIdle();
    DelLink("virtual-network", "vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    client->WaitForIdle();
}

// RPF for L3 Local flow
TEST_F(FlowRpfTest, L3_Local_1) {
    TxIpPacket(vmi0_->id(), VM1_IP, VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const InterfaceNH *rnh = dynamic_cast<const InterfaceNH *>(rflow->rpf_nh());
    EXPECT_TRUE(rnh->GetInterface() == static_cast<Interface *>(vmi1_));
}

// RPF for L3 flow from VM to Fabric
TEST_F(FlowRpfTest, L3_VmToFabric_1) {
    TxIpPacket(vmi0_->id(), VM1_IP, REMOTE_VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM1_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
}

// RPF for L3 flow from Fabric to VM
TEST_F(FlowRpfTest, L3_FabricToVm_1) {
    TxIpMplsPacket(eth0_->id(), "100.100.100.1", router_id_, vmi0_->label(),
                   REMOTE_VM1_IP, VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), REMOTE_VM1_IP, VM1_IP,
                              1, 0, 0, vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(flow->rpf_nh());
    EXPECT_TRUE(*(nh->GetDip()) == Ip4Address::from_string("100.100.100.1"));

    const InterfaceNH *rnh = dynamic_cast<const InterfaceNH *>(rflow->rpf_nh());
    EXPECT_TRUE(rnh != NULL);
    EXPECT_TRUE(rnh->GetInterface() == static_cast<Interface *>(vmi0_));
}

// RPF for L2 Local flow
TEST_F(FlowRpfTest, L2_Local_1) {
    TxL2Packet(vmi0_->id(), VM1_MAC, VM2_MAC, VM1_IP, VM2_IP, 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const InterfaceNH *rnh = dynamic_cast<const InterfaceNH *>(rflow->rpf_nh());
    EXPECT_TRUE(rnh->GetInterface() == static_cast<Interface *>(vmi1_));
}

// RPF for L2 flow from VM to Fabric
TEST_F(FlowRpfTest, L2_VmToFabric_1) {
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM1_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
}

// RPF for L2 flow from Fabric to VM
TEST_F(FlowRpfTest, L2_FabricToVm_1) {
    TxL2IpMplsPacket(eth0_->id(), "100.100.100.1", router_id_,
                     vmi0_->l2_label(), REMOTE_VM1_MAC, VM1_MAC,
                     REMOTE_VM1_IP, VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), REMOTE_VM1_IP, VM1_IP,
                              1, 0, 0, MplsToNextHop(vmi0_->l2_label())->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

        EXPECT_TRUE(flow->data().enable_rpf);
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(flow->rpf_nh());
    EXPECT_TRUE(*(nh->GetDip()) == Ip4Address::from_string("100.100.100.1"));

    const InterfaceNH *rnh = dynamic_cast<const InterfaceNH *>(rflow->rpf_nh());
    EXPECT_TRUE(rnh != NULL);
    EXPECT_TRUE(rnh->GetInterface() == static_cast<Interface *>(vmi0_));
}

// RPF for L2 Local flow based on SrcIp on different port
TEST_F(FlowRpfTest, L2_VmToFabric_SrcIp_1) {
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM2_IP, REMOTE_VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM2_IP, REMOTE_VM1_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi1_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
}

// RPF for L2 Local flow based on SrcIp that doesnt have route
TEST_F(FlowRpfTest, L2_VmToFabric_SrcIp_2) {
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM3_IP, REMOTE_VM1_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM3_IP, REMOTE_VM1_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_EQ(flow->short_flow_reason(), FlowEntry::SHORT_NO_SRC_ROUTE_L2RPF);

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
}

// RPF for L2 Local flow based on DstIp on different port
TEST_F(FlowRpfTest, L2_VmToFabric_DstIp_1) {
    CreateRemoteRoute("vrf1", REMOTE_VM2_IP, 32, "100.100.100.2", 10, "vn1");
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.2"));
    DeleteRemoteRoute("vrf1", REMOTE_VM2_IP, 32);
}

// RPF for L2 Local flow based on DstIp that doesnt have route
TEST_F(FlowRpfTest, L2_VmToFabric_DstIp_2) {
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow != NULL);

    EXPECT_FALSE(flow->IsShortFlow());
    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_FALSE(rflow->IsShortFlow());
    EXPECT_TRUE(rflow->data().enable_rpf);
    EXPECT_TRUE(rflow->rpf_nh() != NULL);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
}

// If source-ip hits subnet route then RPF should be based on L2 Route
TEST_F(FlowRpfTest, L2_VmToFabric_SubnetRoute_1) {
    CreateRemoteRoute("vrf1", REMOTE_VM2_IP, 30, "100.100.100.2", 10, "vn1");
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_FALSE(flow->IsShortFlow());
    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_FALSE(rflow->IsShortFlow());
    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
    DeleteRemoteRoute("vrf1", REMOTE_VM2_IP, 30);
}

// RPF for an egress flow must be based on L2 Route if there is no source-route
TEST_F(FlowRpfTest, L2_VmToFabric_NoRoute_1) {
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_FALSE(flow->IsShortFlow());
    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_FALSE(rflow->IsShortFlow());
    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
    DeleteRemoteRoute("vrf1", REMOTE_VM2_IP, 30);
}

// If source-ip hits ECMP route then RPF should be based on L2 Route
TEST_F(FlowRpfTest, L2_VmToFabric_EcmpRoute_1) {
    CreateRemoteEcmpRoute("vrf1", REMOTE_VM2_IP, 32, "vn1", 3);
    TxL2Packet(vmi0_->id(), VM1_MAC, REMOTE_VM1_MAC, VM1_IP, REMOTE_VM2_IP, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf1_->vrf_id(), VM1_IP, REMOTE_VM2_IP, 1, 0, 0,
                              vmi0_->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(flow->data().enable_rpf);
    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>(flow->rpf_nh());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetInterface() == static_cast<Interface *>(vmi0_));

    EXPECT_TRUE(rflow->data().enable_rpf);
    const TunnelNH *rnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(*(rnh->GetDip()) == Ip4Address::from_string("100.100.100.1"));
    DeleteRemoteRoute("vrf1", REMOTE_VM2_IP, 32);
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
