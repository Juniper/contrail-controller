/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "oper/tunnel_nh.h"

#define COMPARE_TRUE(expr) \
    do {\
        EXPECT_TRUE((expr));\
        if ((expr) != true) ret = false;\
    } while(0)

#define COMPARE_FALSE(expr) \
    do {\
        EXPECT_FALSE((expr));\
        if ((expr) != false) ret = false;\
    } while(0)

#define COMPARE_EQ(v1, v2) \
    do {\
        EXPECT_EQ((v1), (v2));\
        if ((v1) != (v2)) ret = false;\
    } while(0)

#define COMPARE_NE(v1, v2) \
    do {\
        EXPECT_NE((v1, v2));\
        if ((v1) == (v2)) ret = false;\
    } while(0)

#define LOCAL_ECMP_IP_1 "1.1.1.3"
#define LOCAL_ECMP_IP_2 "1.1.1.4"
#define LOCAL_NON_ECMP_IP_3 "1.1.1.5"
#define LOCAL_NON_ECMP_IP_4 "1.1.1.6"
#define REMOTE_NON_ECMP_IP_1 "1.1.1.7"
#define REMOTE_ECMP_IP_1 "1.1.1.8"
#define REMOTE_SERVER_1 "100.100.100.1"
#define REMOTE_SERVER_2 "100.100.100.2"
#define REMOTE_SERVER_3 "100.100.100.3"
#define REMOTE_SERVER_4 "100.100.100.4"

#define LOCAL_MAC_1  "00:00:00:01:01:01"
#define LOCAL_MAC_2  "00:00:00:01:01:02"
#define LOCAL_MAC_3  "00:00:00:01:01:03"
#define LOCAL_MAC_4  "00:00:00:01:01:04"
#define LOCAL_MAC_5  "00:00:00:01:01:05"
#define LOCAL_MAC_6  "00:00:00:01:01:06"
#define LOCAL_MAC_7  "00:00:00:01:01:07"
#define LOCAL_MAC_8  "00:00:00:01:01:08"
#define LOCAL_MAC_9  "00:00:00:01:01:09"
#define LOCAL_MAC_10 "00:00:00:01:01:10"

#define REMOTE_MAC_1 "00:00:00:02:01:01"
#define REMOTE_MAC_2 "00:00:00:02:01:02"
#define REMOTE_MAC_3 "00:00:00:02:01:03"
#define REMOTE_MAC_4 "00:00:00:02:01:04"
#define REMOTE_MAC_5 "00:00:00:02:01:05"

struct PortInfo input1[] = {
    {"vnet1", 1, LOCAL_ECMP_IP_1, LOCAL_MAC_1, 1, 1},
    {"vnet2", 2, LOCAL_ECMP_IP_1, LOCAL_MAC_2, 1, 2},
    {"vnet3", 3, LOCAL_ECMP_IP_1, LOCAL_MAC_3, 1, 3},
    {"vnet4", 4, LOCAL_ECMP_IP_1, LOCAL_MAC_4, 1, 4},
};

struct PortInfo input2[] = {
    {"vnet5", 5, LOCAL_ECMP_IP_2, LOCAL_MAC_5, 1, 5},
    {"vnet6", 6, LOCAL_ECMP_IP_2, LOCAL_MAC_6, 1, 6},
    {"vnet7", 7, LOCAL_ECMP_IP_2, LOCAL_MAC_7, 1, 7},
    {"vnet8", 8, LOCAL_ECMP_IP_2, LOCAL_MAC_8, 1, 8},
};

struct PortInfo input3[] = {
    {"vnet9", 9, LOCAL_NON_ECMP_IP_3, LOCAL_MAC_9, 1, 9},
};

struct PortInfo input4[] = {
    {"vnet10", 10, LOCAL_NON_ECMP_IP_4, LOCAL_MAC_10, 1, 10},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.1"},
};

class EcmpTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        eth_intf_id_ = EthInterfaceGet("vnet0")->id();
        strcpy(router_id_, agent_->router_id().to_string().c_str());

        strcpy(remote_server_ip_[0], REMOTE_SERVER_1);
        strcpy(remote_server_ip_[1], REMOTE_SERVER_2);
        strcpy(remote_server_ip_[2], REMOTE_SERVER_3);
        strcpy(remote_server_ip_[3], REMOTE_SERVER_4);
        strcpy(remote_mac_[0], REMOTE_MAC_1);
        strcpy(remote_mac_[1], REMOTE_MAC_2);
        strcpy(remote_mac_[2], REMOTE_MAC_3);
        strcpy(remote_mac_[3], REMOTE_MAC_4);

        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        /******************************************************************
         * Create following interfaces
         * vnet1, vnet2, vnet3, vnet4 in ECMP
         * vnet5, vnet6, vnet7, vnet8 in ECMP
         *****************************************************************/
        CreateVmportWithEcmp(input1, 4);
        CreateVmportWithEcmp(input2, 4);
        CreateVmportEnv(input3, 1);
        CreateVmportEnv(input4, 1);
        client->WaitForIdle();
        for (uint32_t i = 1; i < 9; i++) {
            EXPECT_TRUE(VmPortActive(i));
        }

        vrf_id1_ = VrfGet("vrf1")->vrf_id();
        rt1_ = RouteGet("vrf1", Ip4Address::from_string(LOCAL_ECMP_IP_1), 32);
        ecmp_nh1_ = rt1_->GetActiveNextHop();
        ecmp_label1_ = rt1_->GetActiveLabel();
        EXPECT_TRUE(rt1_ != NULL);

        rt2_ = RouteGet("vrf1", Ip4Address::from_string(LOCAL_ECMP_IP_2), 32);
        ecmp_nh2_ = rt2_->GetActiveNextHop();
        ecmp_label2_ = rt2_->GetActiveLabel();
        EXPECT_TRUE(rt2_ != NULL);

        uc_rt_table_ = agent_->vrf_table()->GetInet4UnicastRouteTable("vrf1");

        // Add remote routes
        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        remote_vm_ip1_ = Ip4Address::from_string(REMOTE_NON_ECMP_IP_1);

        // Add non-ecmp remote VM routes
        AddRemoteVmRoute("vrf1", REMOTE_NON_ECMP_IP_1, 32, 100, "vn1",
                         REMOTE_SERVER_1);
        AddRemoteEvpnVmRoute("vrf1", REMOTE_MAC_1, 101, "vn1",
                             REMOTE_SERVER_1);
        client->WaitForIdle();

        // Add ecmp remote VM routes
        ComponentNHKeyList local_comp_nh;
        AddRemoteEcmpRoute("vrf1", REMOTE_ECMP_IP_1, 32, 200, "vn1", 4,
                           local_comp_nh);
        // Add non-ECMP EVPN route for all peers added above
        AddRemoteEvpnVmRoute("vrf1", REMOTE_MAC_2, 201, "vn1", REMOTE_SERVER_1);
        AddRemoteEvpnVmRoute("vrf1", REMOTE_MAC_3, 202, "vn1", REMOTE_SERVER_2);
        AddRemoteEvpnVmRoute("vrf1", REMOTE_MAC_4, 203, "vn1", REMOTE_SERVER_3);
        AddRemoteEvpnVmRoute("vrf1", REMOTE_MAC_5, 204, "vn1", REMOTE_SERVER_4);
        client->WaitForIdle();

        FlowStatsTimerStartStop(agent_, true);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input1, 4, false);
        DeleteVmportEnv(input2, 4, false);
        DeleteVmportEnv(input3, 1, false);
        DeleteVmportEnv(input4, 1, true);
        client->WaitForIdle();

        // Delete remote routes
        DeleteRemoteRoute("vrf1", REMOTE_NON_ECMP_IP_1, 32);
        DeleteRemoteEvpnRoute("vrf1", REMOTE_MAC_1);
        DeleteRemoteRoute("vrf1", REMOTE_ECMP_IP_1, 32);
        DeleteRemoteEvpnRoute("vrf1", REMOTE_MAC_2);
        DeleteRemoteEvpnRoute("vrf1", REMOTE_MAC_3);
        DeleteRemoteEvpnRoute("vrf1", REMOTE_MAC_4);
        DeleteRemoteEvpnRoute("vrf1", REMOTE_MAC_5);
        client->WaitForIdle();

        DelIPAM("vn1");
        client->WaitForIdle();

        DeleteBgpPeer(bgp_peer_);
        EXPECT_FALSE(VrfFind("vrf1", true));
    }

    const NextHop *GetRouteNh(const char *vrf, const char *ip, uint8_t plen) {
        const InetUnicastRouteEntry *rt =
            RouteGet(vrf, Ip4Address::from_string(ip), plen);
        if (rt == NULL)
            return NULL;
        return rt->GetActiveNextHop();
    }

    void AddRemoteEcmpRoute(const char *vrf, const char *ip, uint32_t plen,
                            uint32_t label, const char *vn, int count,
                            const ComponentNHKeyList &local_list) {
        Ip4Address vm_ip = Ip4Address::from_string(ip);
        ComponentNHKeyList comp_nh_list = local_list;

        for(int i = 0; i < count; i++) {
            Ip4Address server = Ip4Address::from_string(remote_server_ip_[i]);
            ComponentNHKeyPtr comp_nh
                (new ComponentNHKey(label, agent_->fabric_vrf_name(),
                                    agent_->router_id(), server, false,
                                    TunnelType::GREType()));
            comp_nh_list.push_back(comp_nh);
        }
        SecurityGroupList sg_id_list;
        EcmpTunnelRouteAdd(bgp_peer_, vrf, vm_ip, plen, comp_nh_list, -1,
                           vn, sg_id_list, PathPreference());
    }

    void AddRemoteVmRoute(const char *vrf, const char *ip, uint32_t plen,
                          uint32_t label, const char *vn, const char *nh_ip) {
        TunnelRouteAdd(nh_ip, ip, vrf, label, vn);
    }

    void AddRemoteEvpnVmRoute(const char *vrf, const char *mac, uint32_t label,
                              const char *vn, const char *nh_ip) {
        MacAddress mac_addr = MacAddress::FromString(mac);
        BridgeTunnelRouteAdd(bgp_peer_, "vrf1", TunnelType::GREType(),
                             Ip4Address::from_string(remote_server_ip_[0]),
                             label, mac_addr, Ip4Address(), 0);
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip, uint32_t plen) {
        DeleteRoute(vrf, ip, plen, bgp_peer_);
    }

    void DeleteRemoteEvpnRoute(const char *vrf, const char *mac) {
        ControllerVmRoute *data = new ControllerVmRoute(bgp_peer_);
        agent_->fabric_evpn_table()->DeleteReq(bgp_peer_, vrf,
                                               MacAddress::FromString(mac),
                                               Ip4Address(), 0, data);
    }

    bool FlowTableWait(int count) {
        int i = 100000;
        while (i > 0) {
            i--;
            if (flow_proto_->FlowCount() == (size_t) count) {
                return true;
            }
            client->WaitForIdle();
            usleep(100);
        }
        return (flow_proto_->FlowCount() == (size_t) count);
    }

    void FlushFlowTable() {
        client->WaitForIdle();
        client->EnqueueFlowFlush();
        EXPECT_TRUE(FlowTableWait(0));
    }

    bool TxL2MplsPacketFromFabric(VmInterface *vmi, const char *tunnel_sip,
                                  uint32_t label, const char *smac,
                                  const char *dmac, const char *sip,
                                  const char *dip, FlowEntry **flow,
                                  FlowEntry **rflow) {
        bool ret = true;
        // Generate L2 Flow
        TxL2IpMplsPacket(eth_intf_id_, tunnel_sip, router_id_,
                         label, smac, dmac, sip, dip, 1);
        client->WaitForIdle();

        // Forward flow is not ECMP
        *flow = FlowGet(vrf_id1_, sip, dip, 1, 0, 0, vmi->flow_key_nh()->id());
        COMPARE_TRUE(flow != NULL);
        COMPARE_TRUE((*flow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);

        // Reverse flow is not ECMP
        *rflow = (*flow)->reverse_flow_entry();
        COMPARE_TRUE((*rflow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);
        return ret;
    }

    bool TxL3MplsPacketFromFabric(VmInterface *vmi, const char *tunnel_sip,
                                  uint32_t label, const char *sip,
                                  const char *dip, FlowEntry **flow,
                                  FlowEntry **rflow) {
        bool ret = true;
        // Generate L2 Flow
        TxIpMplsPacket(eth_intf_id_, tunnel_sip, router_id_,
                       label, sip, dip, 1);
        client->WaitForIdle();

        // Forward flow is not ECMP
        *flow = FlowGet(vrf_id1_, sip, dip, 1, 0, 0, vmi->flow_key_nh()->id());
        COMPARE_TRUE(flow != NULL);
        COMPARE_TRUE((*flow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);

        // Reverse flow is not ECMP
        *rflow = (*flow)->reverse_flow_entry();
        COMPARE_TRUE((*rflow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);
        return ret;
    }

    // Generate ECMP resolution event for packet fro VMI
    bool EcmpResolveFromVmi(VmInterface *vmi, const char *sip,
                            const char *dip, const FlowEntry *flow,
                            const FlowEntry *rflow, uint32_t count = 2) {
        bool ret = true;
        // Generate ECMP resolution event
        TxIpPacketEcmp(vmi->id(), sip, dip, 1, flow->flow_handle());
        client->WaitForIdle();

        // No new flows must be generated
        COMPARE_EQ(count, flow_proto_->FlowCount());
        COMPARE_FALSE(flow->l3_flow());
        COMPARE_FALSE(rflow->l3_flow());
        return ret;
    }

    bool TxL2PacketFromVmi(VmInterface *vmi, const char *smac,
                           const char *dmac, const char *sip,
                           const char *dip, FlowEntry **flow,
                           FlowEntry **rflow) {
        bool ret = true;
        // Generate L2 Flow
        TxL2Packet(vmi->id(), smac, dmac, sip, dip, 1);
        client->WaitForIdle();

        // Forward flow is not ECMP
        *flow = FlowGet(vrf_id1_, sip, dip, 1, 0, 0, vmi->flow_key_nh()->id());
        COMPARE_TRUE(flow != NULL);
        COMPARE_TRUE((*flow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);

        // Reverse flow is not ECMP
        *rflow = (*flow)->reverse_flow_entry();
        COMPARE_TRUE((*rflow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);
        return ret;
    }

    bool TxL3PacketFromVmi(VmInterface *vmi, const char *sip,
                           const char *dip, FlowEntry **flow,
                           FlowEntry **rflow) {
        bool ret = true;
        // Generate L2 Flow
        TxIpPacket(vmi->id(), sip, dip, 1);
        client->WaitForIdle();

        // Forward flow is not ECMP
        *flow = FlowGet(vrf_id1_, sip, dip, 1, 0, 0, vmi->flow_key_nh()->id());
        COMPARE_TRUE(flow != NULL);
        COMPARE_TRUE((*flow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);

        // Reverse flow is not ECMP
        *rflow = (*flow)->reverse_flow_entry();
        COMPARE_TRUE((*rflow)->data().component_nh_idx ==
                     CompositeNH::kInvalidComponentNHIdx);
        return ret;
    }

    // Generate ECMP resolution event for packet from fabric
    bool EcmpResolveFromFabric(const char *tunnel_sip, uint32_t label,
                               const char *sip, const char *dip,
                               const FlowEntry *flow, const FlowEntry *rflow) {
        bool ret = true;
        // Generate ECMP resolution event
        TxIpMplsPacket(eth_intf_id_, tunnel_sip, router_id_, label,
                       sip, dip, 1, flow->flow_handle(), true);
        client->WaitForIdle();

        // No new flows must be generated
        COMPARE_EQ(2U, flow_proto_->FlowCount());
        COMPARE_FALSE(flow->l3_flow());
        COMPARE_FALSE(rflow->l3_flow());
        return ret;
    }


        // No new flows must be generated
    bool ValidateEcmpAndTunnel(const FlowEntry *flow, bool ecmp,
                               const char *ip, const char *tunnel_ip) {
        bool ret = true;
        if (ecmp == false) {
            COMPARE_TRUE(flow->data().component_nh_idx ==
                   CompositeNH::kInvalidComponentNHIdx);
            return ret;
        }

        uint32_t ecmp_idx = flow->data().component_nh_idx;
        COMPARE_TRUE(ecmp_idx != CompositeNH::kInvalidComponentNHIdx);

        // Validate the ECMP member points back to source of L2 packet
        const CompositeNH *comp_nh = dynamic_cast<const CompositeNH *>
            (GetRouteNh("vrf1", ip, 32));
        COMPARE_TRUE(comp_nh != NULL);

        const TunnelNH *tunnel_nh =
            dynamic_cast<const TunnelNH *>(comp_nh->GetNH(ecmp_idx));
        COMPARE_TRUE(tunnel_nh != NULL);

        COMPARE_TRUE(*tunnel_nh->GetDip() ==
                     Ip4Address::from_string(tunnel_ip));
        return ret;
    }

    bool ValidateEcmpAndVmi(const FlowEntry *flow, bool ecmp,
                            const char *ip, const Interface *intf) {
        bool ret = true;
        if (ecmp == false) {
            ret = (flow->data().component_nh_idx ==
                   CompositeNH::kInvalidComponentNHIdx);
            EXPECT_TRUE(ret);
            return ret;
        }

        uint32_t ecmp_idx = flow->data().component_nh_idx;
        COMPARE_TRUE(ecmp_idx != CompositeNH::kInvalidComponentNHIdx);

        // Validate the ECMP member points back to source of L2 packet
        const CompositeNH *comp_nh = dynamic_cast<const CompositeNH *>
            (GetRouteNh("vrf1", ip, 32));
        COMPARE_TRUE(comp_nh != NULL);

        const InterfaceNH *intf_nh =
            dynamic_cast<const InterfaceNH *>(comp_nh->GetNH(ecmp_idx));
        COMPARE_TRUE(intf_nh != NULL);

        COMPARE_TRUE(intf_nh->GetInterface() == intf);
        return ret;
    }

protected:
    Agent *agent_;
    uint32_t eth_intf_id_;
    Ip4Address remote_vm_ip1_;
    FlowProto *flow_proto_;
    char router_id_[64];
    InetUnicastAgentRouteTable *uc_rt_table_;
    uint32_t vrf_id1_;
    char remote_server_ip_[4][64];
    char remote_mac_[4][64];

    const InetUnicastRouteEntry *rt1_;
    const NextHop *ecmp_nh1_;
    uint32_t ecmp_label1_;

    const InetUnicastRouteEntry *rt2_;
    const NextHop *ecmp_nh2_;
    uint32_t ecmp_label2_;
};

/////////////////////////////////////////////////////////////////////////////
// Egress Flow
// Source : Non-ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Egress_Non_Ecmp_To_Non_Ecmp_1) {
    char sip[64];
    strcpy(sip, REMOTE_NON_ECMP_IP_1);
    char dip[64];
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(9));
    strcpy(dip, vmi->primary_ip_addr().to_string().c_str());

    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;
    // Generate layer-2 packet from fabric
    EXPECT_TRUE(TxL2MplsPacketFromFabric(vmi, remote_server_ip_[0],
                                         vmi->l2_label(), REMOTE_MAC_1,
                                         vmi->vm_mac().ToString().c_str(),
                                         sip, dip, &flow, &rflow));

    FlowEntry *flow_new;
    FlowEntry *rflow_new;
    // Generate layer-3 packet from VMI
    EXPECT_TRUE(TxL3PacketFromVmi(vmi, dip, sip, &rflow_new, &flow_new));
    EXPECT_TRUE(flow_new == flow);
    EXPECT_TRUE(rflow_new == rflow);

    // No new flows must be generated
    EXPECT_EQ(2U, flow_proto_->FlowCount());
    // Flows should be l2_flow still
    EXPECT_FALSE(flow->l3_flow());
    EXPECT_FALSE(rflow->l3_flow());

    // ECMP must not be set on both flows
    EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
    EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, false, NULL, NULL));
    FlushFlowTable();
}

/////////////////////////////////////////////////////////////////////////////
// Egress Flow
// Source : Non-ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Egress_Non_Ecmp_To_Ecmp_1) {
    // Inner iteration for all 4 members of destination
    for (int i = 0; i < 4; i++) {
        char sip[64];
        strcpy(sip, REMOTE_NON_ECMP_IP_1);
        char dip[64];
        VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(i + 1));
        strcpy(dip, vmi->primary_ip_addr().to_string().c_str());

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Generate layer-2 packet from fabric
        EXPECT_TRUE(TxL2MplsPacketFromFabric(vmi, remote_server_ip_[0],
                                             vmi->l2_label(), remote_mac_[0],
                                             vmi->vm_mac().ToString().c_str(),
                                             sip, dip, &flow, &rflow));

        // Generate layer-3 ECMP resolve packet from VMI
        EXPECT_TRUE(EcmpResolveFromVmi(vmi, dip, sip, rflow, flow));
        // Old forward flow must have ECMP index and point to same interface
        EXPECT_TRUE(ValidateEcmpAndVmi(flow, true, dip, vmi));
        // Old reverse flow must not have ECMP
        EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, false, NULL, NULL));
        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Egress Flow
// Source : ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Egress_Ecmp_To_Non_Ecmp_1) {
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(9));

    for (int i = 0; i < 4; i++) {
        char sip[64];
        strcpy(sip, REMOTE_ECMP_IP_1);
        char dip[64];
        strcpy(dip, vmi->primary_ip_addr().to_string().c_str());

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Generate layer-2 packet from fabric
        EXPECT_TRUE(TxL2MplsPacketFromFabric(vmi, remote_server_ip_[i],
                                             vmi->l2_label(), remote_mac_[i],
                                             vmi->vm_mac().ToString().c_str(),
                                             sip, dip, &flow, &rflow));

        // Generate layer-3 ECMP resolve packet from VMI
        EXPECT_TRUE(EcmpResolveFromVmi(vmi, dip, sip, rflow, flow));
        // Old forward flow must not have ECMP Index still
        EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
        // Old reverse flow must have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, true, sip,
                                          remote_server_ip_[i]));

        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Egress Flow
// Source : ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Egress_Ecmp_To_Ecmp_1) {
    // Outer iteration for all 4 members of source
    for (int i = 0; i < 4; i++) {
        // Inner iteration for all 4 members of destination
        for (int j = 0; j < 4; j++) {
            char sip[64];
            strcpy(sip, REMOTE_ECMP_IP_1);
            char dip[64];
            VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(j + 1));
            strcpy(dip, vmi->primary_ip_addr().to_string().c_str());

            FlowEntry *flow = NULL;
            FlowEntry *rflow = NULL;
            string mac = vmi->vm_mac().ToString();
            EXPECT_TRUE(TxL2MplsPacketFromFabric(vmi, remote_server_ip_[i],
                                                 vmi->l2_label(),
                                                 remote_mac_[i], mac.c_str(),
                                                 sip, dip, &flow, &rflow));

            // Generate layer-3 ECMP resolve packet from VMI
            EXPECT_TRUE(EcmpResolveFromVmi(vmi, dip, sip, rflow, flow));
            // Old forward flow must have ECMP Index and point to VMI
            EXPECT_TRUE(ValidateEcmpAndVmi(flow, true, dip, vmi));
            // Old reverse flow must have ECMP Index set and point to
            // tunnel-source
            EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, true, sip,
                                              remote_server_ip_[i]));
            FlushFlowTable();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Ingress Flow
// Source : Non-ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Ingress_Non_Ecmp_To_Non_Ecmp_1) {
    char sip[64];
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(10));
    strcpy(sip, vmi->primary_ip_addr().to_string().c_str());
    string mac = vmi->vm_mac().ToString();
    char dip[64];
    strcpy(dip, REMOTE_NON_ECMP_IP_1);

    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;
    // Send layer-2 packet from VMI
    EXPECT_TRUE(TxL2PacketFromVmi(vmi, mac.c_str(), remote_mac_[0],
                                  sip, dip, &flow, &rflow));

    // Simulate ECMP Resolve from fabric
    EXPECT_TRUE(EcmpResolveFromFabric(remote_server_ip_[0], vmi->label(),
                                      dip, sip, rflow, flow));
    // Old forward flow must not have ECMP Index set
    EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
    // Old reverse flow must not have ECMP Index set
    EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, false, NULL, NULL));

    FlushFlowTable();
}

/////////////////////////////////////////////////////////////////////////////
// Ingress Flow
// Source : Non-ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Ingress_Non_Ecmp_To_Ecmp_1) {
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(9));
    for (uint32_t i = 0; i < 4; i++) {
        char sip[64];
        strcpy(sip, vmi->primary_ip_addr().to_string().c_str());
        string mac = vmi->vm_mac().ToString();
        char dip[64];
        strcpy(dip, REMOTE_ECMP_IP_1);

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Send layer-2 packet from VMI
        EXPECT_TRUE(TxL2PacketFromVmi(vmi, mac.c_str(), remote_mac_[0],
                                      sip, dip, &flow, &rflow));

        // Simulate ECMP Resolve from fabric
        EXPECT_TRUE(EcmpResolveFromFabric(remote_server_ip_[i], vmi->label(),
                                          dip, sip, rflow, flow));
        // Old forward flow must have ECMP Index set and point to tunnel source
        EXPECT_TRUE(ValidateEcmpAndTunnel(flow, true, dip,
                                          remote_server_ip_[i]));
        // Old reverse flow must not have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, false, NULL, NULL));

        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Ingress Flow
// Source : ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Ingress_Ecmp_To_Non_Ecmp_1) {
    for (uint32_t i = 0; i < 4; i++) {
        char sip[64];
        VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(i + 1));
        strcpy(sip, vmi->primary_ip_addr().to_string().c_str());
        string mac = vmi->vm_mac().ToString();
        char dip[64];
        strcpy(dip, REMOTE_NON_ECMP_IP_1);

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Send layer-2 packet from VMI
        EXPECT_TRUE(TxL2PacketFromVmi(vmi, mac.c_str(), remote_mac_[0],
                                      sip, dip, &flow, &rflow));

        // Simulate ECMP Resolve from fabric
        EXPECT_TRUE(EcmpResolveFromFabric(remote_server_ip_[i], vmi->label(),
                                          dip, sip, rflow, flow));
        // Old forward flow must not have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
        // Old reverse flow must have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndVmi(rflow, true, sip, vmi));

        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Ingress Flow
// Source : ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Ingress_Ecmp_To_Ecmp_1) {
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = 0; j < 4; j++) {
            char sip[64];
            VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(i + 1));
            strcpy(sip, vmi->primary_ip_addr().to_string().c_str());
            string mac = vmi->vm_mac().ToString();
            char dip[64];
            strcpy(dip, REMOTE_ECMP_IP_1);

            FlowEntry *flow = NULL;
            FlowEntry *rflow = NULL;
            // Send layer-2 packet from VMI
            EXPECT_TRUE(TxL2PacketFromVmi(vmi, mac.c_str(), remote_mac_[i],
                                          sip, dip, &flow, &rflow));

            // Simulate ECMP Resolve from fabric
            EXPECT_TRUE(EcmpResolveFromFabric(remote_server_ip_[i],
                                              vmi->label(), dip, sip, rflow,
                                              flow));
            // Old forward flow must not have ECMP Index set
            EXPECT_TRUE(ValidateEcmpAndTunnel(flow, true, dip,
                                              remote_server_ip_[i]));
            // Old reverse flow must have ECMP Index set
            EXPECT_TRUE(ValidateEcmpAndVmi(rflow, true, sip, vmi));

            FlushFlowTable();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Local Flow
// Validate flows at source
// Source : Non-ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Local_Non_Ecmp_To_Non_Ecmp_1) {
    VmInterface *vmi1 = static_cast<VmInterface *>(VmPortGet(9));
    VmInterface *vmi2 = static_cast<VmInterface *>(VmPortGet(10));

    char sip[64];
    strcpy(sip, vmi1->primary_ip_addr().to_string().c_str());
    string mac1 = vmi1->vm_mac().ToString();
    char dip[64];
    strcpy(dip, vmi2->primary_ip_addr().to_string().c_str());
    string mac2 = vmi2->vm_mac().ToString();

    FlowEntry *flow = NULL;
    FlowEntry *rflow = NULL;
    // Send layer-2 packet from VMI
    EXPECT_TRUE(TxL2PacketFromVmi(vmi1, mac1.c_str(), mac2.c_str(), sip,
                                  dip, &flow, &rflow));

    // Simulate ECMP Resolve from vmi1
    EXPECT_TRUE(EcmpResolveFromVmi(vmi1, sip, dip, flow, rflow));
    // Old forward flow must not have ECMP Index set
    EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
    // Old reverse flow must not have ECMP Index set
    EXPECT_TRUE(ValidateEcmpAndTunnel(rflow, false, NULL, NULL));

    FlushFlowTable();
}

/////////////////////////////////////////////////////////////////////////////
// Local Flow
// Validate flows at source
// Source : Non-ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Local_Non_Ecmp_To_Ecmp_1) {
    for (uint32_t i = 0; i < 4; i++) {
        VmInterface *vmi1 = static_cast<VmInterface *>(VmPortGet(9));
        VmInterface *vmi2 = static_cast<VmInterface *>(VmPortGet(i+1));
        char sip[64];
        strcpy(sip, vmi1->primary_ip_addr().to_string().c_str());
        string mac1 = vmi1->vm_mac().ToString();
        char dip[64];
        strcpy(dip, vmi2->primary_ip_addr().to_string().c_str());
        string mac2 = vmi2->vm_mac().ToString();

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Send layer-2 packet from VMI
        EXPECT_TRUE(TxL2PacketFromVmi(vmi1, mac1.c_str(), mac2.c_str(),
                                      sip, dip, &flow, &rflow));

        // Simulate ECMP Resolve from vmi2
        EXPECT_TRUE(EcmpResolveFromVmi(vmi2, dip, sip, flow, rflow));
        // Old forward flow will have ECMP Index set and must point to original
        // destination
        EXPECT_TRUE(ValidateEcmpAndVmi(flow, true, dip, vmi2));
        // Old reverse flow must not have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndVmi(rflow, false, NULL, NULL));

        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Local Flow
// Validate flows at source
// Source : ECMP
// Destination : Non-ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Local_Ecmp_To_Non_Ecmp_1) {
    for (uint32_t i = 0; i < 4; i++) {
        char sip[64];
        VmInterface *vmi1 = static_cast<VmInterface *>(VmPortGet(i + 1));
        strcpy(sip, vmi1->primary_ip_addr().to_string().c_str());
        string mac1 = vmi1->vm_mac().ToString();
        char dip[64];
        VmInterface *vmi2 = static_cast<VmInterface *>(VmPortGet(9));
        strcpy(dip, vmi2->primary_ip_addr().to_string().c_str());
        string mac2 = vmi2->vm_mac().ToString();

        FlowEntry *flow = NULL;
        FlowEntry *rflow = NULL;
        // Send layer-2 packet from VMI
        EXPECT_TRUE(TxL2PacketFromVmi(vmi1, mac1.c_str(), mac2.c_str(),
                                      sip, dip, &flow, &rflow));

        // Simulate ECMP Resolve from vmi2
        EXPECT_TRUE(EcmpResolveFromVmi(vmi2, dip, sip, rflow, flow));
        // Old forward flow must not have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndTunnel(flow, false, NULL, NULL));
        // Old reverse flow must have ECMP Index set
        EXPECT_TRUE(ValidateEcmpAndVmi(rflow, true, sip, vmi1));

        FlushFlowTable();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Local Flow
// Validate flows at source
// Source : ECMP
// Destination : ECMP
/////////////////////////////////////////////////////////////////////////////
TEST_F(EcmpTest, Local_Ecmp_To_Ecmp_1) {
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = 0; j < 4; j++) {
            char sip[64];
            VmInterface *vmi1 = static_cast<VmInterface *>(VmPortGet(i + 1));
            strcpy(sip, vmi1->primary_ip_addr().to_string().c_str());
            string mac1 = vmi1->vm_mac().ToString();

            char dip[64];
            VmInterface *vmi2 = static_cast<VmInterface *>(VmPortGet(j + 5));
            strcpy(dip, vmi2->primary_ip_addr().to_string().c_str());
            string mac2 = vmi2->vm_mac().ToString();

            FlowEntry *flow = NULL;
            FlowEntry *rflow = NULL;
            // Send layer-2 packet from VMI
            EXPECT_TRUE(TxL2PacketFromVmi(vmi1, mac1.c_str(), mac2.c_str(),
                                          sip, dip, &flow, &rflow));

            // Simulate ECMP Resolve from vmi
            EXPECT_TRUE(EcmpResolveFromVmi(vmi2, dip, sip, rflow, flow));
            EXPECT_TRUE(ValidateEcmpAndVmi(flow, true, dip, vmi2));
            EXPECT_TRUE(ValidateEcmpAndVmi(rflow, true, sip, vmi1));

            FlushFlowTable();
        }
    }
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
