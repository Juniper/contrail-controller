/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "test_ecmp.h"
#include "pkt/flow_proto.h"

#define FIP_NON_ECMP_1  "2.1.1.5"
#define FIP_ECMP_1  "2.1.1.20"
#define FIP_REMOTE_ECMP_1  "2.1.1.40"
#define FIP_REMOTE_NON_ECMP_1  "2.1.1.101"

class EcmpFipTest : public EcmpTest {
public:
    EcmpFipTest() : EcmpTest() { }
    virtual ~EcmpFipTest() { }

    virtual void SetUp() {
        EcmpTest::SetUp();
        AddIPAM("vn2", ipam_info_2, 1);
        CreateVmportEnv(input21, 1);
        CreateVmportWithEcmp(input22, 3);
        client->WaitForIdle();

        // Configure floating-ip
        AddFloatingIpPool("fip-pool20", 20);
        AddFloatingIp("fip20", 1, FIP_ECMP_1);
        AddLink("floating-ip", "fip20", "floating-ip-pool", "fip-pool20");
        AddLink("floating-ip-pool", "fip-pool20", "virtual-network", "vn2");

        AddLink("virtual-machine-interface", "vif21", "floating-ip", "fip20");
        AddLink("virtual-machine-interface", "vif22", "floating-ip", "fip20");
        AddLink("virtual-machine-interface", "vif23", "floating-ip", "fip20");

        AddFloatingIp("fip21", 1, FIP_NON_ECMP_1);
        AddLink("floating-ip", "fip21", "floating-ip-pool", "fip-pool20");
        AddLink("virtual-machine-interface", "vif2", "floating-ip", "fip21");
        client->WaitForIdle();

        // Add ECMP Route members to FIP_REMOTE_ECMP_1
        AddRemoteEcmpRoute("vrf2", FIP_REMOTE_ECMP_1, 32, "vn2", 3,
                           ComponentNHKeyList());

        Inet4TunnelRouteAdd(bgp_peer_, "vrf2",
                            Ip4Address::from_string(FIP_REMOTE_NON_ECMP_1), 32,
                            Ip4Address::from_string(REMOTE_COMPUTE_1),
                            TunnelType::AllType(),
                            16, "vn2", SecurityGroupList(), PathPreference());
        client->WaitForIdle();
        GetInfo();
    }

    virtual void TearDown() {
        DelLink("virtual-machine-interface", "vif21", "floating-ip", "fip20");
        DelLink("virtual-machine-interface", "vif22", "floating-ip", "fip20");
        DelLink("virtual-machine-interface", "vif23", "floating-ip", "fip20");

        // Delete floating-ip
        DelLink("floating-ip", "fip20", "floating-ip-pool", "fip-pool20");
        DelLink("floating-ip-pool", "fip-pool20", "virtual-network", "vn2");
        DelFloatingIpPool("fip-pool20");
        DelFloatingIp("fip20");

        // Add ECMP Route members to FIP_REMOTE_ECMP_1
        DeleteRemoteRoute("vrf2", FIP_REMOTE_ECMP_1, 32);
        DeleteRemoteRoute("vrf2", FIP_REMOTE_NON_ECMP_1, 32);

        DeleteVmportEnv(input21, 1, false);
        DeleteVmportEnv(input22, 3, true);
        DelIPAM("vn2");
        client->WaitForIdle();

        EcmpTest::TearDown();
    }
};

// Ping from non-ecmp to ECMP address in vrf2.
// Source has FIP
// Local flow
TEST_F(EcmpFipTest, Local_Src_Fip_NonEcmpToEcmp_1) {
    for (int i = 0; i < 16; i++) {
        TxTcpPacket(VmPortGetId(2), "1.1.1.2", "2.1.1.10", 1000, i, false);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.2", "2.1.1.10",
                                  6, 1000, i, GetFlowKeyNH(2));
        EXPECT_TRUE(flow);
        EXPECT_TRUE(flow->data().component_nh_idx
                    != CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(flow->rpf_nh() == vmi_[2]->flow_key_nh());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        const VmInterface *out_vmi = GetOutVmi(flow);
        EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    }
}

// Ping from non-ecmp to ECMP address in vrf2.
// Source has FIP
// Flow from VMI to fabric
TEST_F(EcmpFipTest, Remote_Src_Fip_NonEcmpToEcmp_1) {
    for (int i = 0; i < 16; i++) {
        TxTcpPacket(VmPortGetId(2), "1.1.1.2", FIP_REMOTE_ECMP_1, 1000, i,
                    false);
        client->WaitForIdle();

        FlowEntry *flow =
            FlowGet(GetVrfId("vrf1"), "1.1.1.2", FIP_REMOTE_ECMP_1, 6, 1000, i,
                    GetFlowKeyNH(2));
        EXPECT_TRUE(flow);
        EXPECT_TRUE(flow->data().component_nh_idx
                    != CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(flow->rpf_nh() == vmi_[2]->flow_key_nh());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        InetUnicastRouteEntry *rt =
            RouteGet("vrf2", Ip4Address::from_string(FIP_REMOTE_ECMP_1), 32);
        EXPECT_TRUE(rflow->rpf_nh() == rt->GetActiveNextHop());
    }
}

// Ping from ecmp member in vrf1 to non-ECMP address in vrf2.
// Source has FIP
// Local flow
TEST_F(EcmpFipTest, Local_Src_Fip_EcmpToNonEcmp_1) {
    TxIpPacket(VmPortGetId(21), "1.1.1.20", "2.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "2.1.1.1",
                              1, 0, 0, GetFlowKeyNH(21));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[21]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[101]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-22
    TxIpPacket(VmPortGetId(22), "1.1.1.20", "2.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "2.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(22));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[22]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[101]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-23
    TxIpPacket(VmPortGetId(23), "1.1.1.20", "2.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "2.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(23));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[23]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[101]->flow_key_nh());
}

// Ping from ecmp member in vrf1 to non-ECMP address in vrf2.
// Source has FIP
// Flow from VMI to fabric
TEST_F(EcmpFipTest, Remote_Src_Fip_EcmpToNonEcmp_1) {
    TxIpPacket(VmPortGetId(21), "1.1.1.20", FIP_REMOTE_NON_ECMP_1, 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20",
                              FIP_REMOTE_NON_ECMP_1, 1, 0, 0, GetFlowKeyNH(21));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[21]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const TunnelNH *tnh = NULL;
    tnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(tnh != NULL);
    if (tnh) {
        EXPECT_TRUE(*tnh->GetDip() == Ip4Address::from_string(REMOTE_COMPUTE_1));
    }

    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-22
    TxIpPacket(VmPortGetId(22), "1.1.1.20", FIP_REMOTE_NON_ECMP_1, 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", FIP_REMOTE_NON_ECMP_1, 1, 0, 0,
                   GetFlowKeyNH(22));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[22]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    tnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(tnh != NULL);
    if (tnh) {
        EXPECT_TRUE(*tnh->GetDip() == Ip4Address::from_string(REMOTE_COMPUTE_1));
    }

    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-23
    TxIpPacket(VmPortGetId(23), "1.1.1.20", FIP_REMOTE_NON_ECMP_1, 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", FIP_REMOTE_NON_ECMP_1, 1, 0, 0,
                   GetFlowKeyNH(23));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[23]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    tnh = dynamic_cast<const TunnelNH *>(rflow->rpf_nh());
    EXPECT_TRUE(tnh != NULL);
    if (tnh) {
        EXPECT_TRUE(*tnh->GetDip() == Ip4Address::from_string(REMOTE_COMPUTE_1));
    }

}

// VMI-23 is in vrf1 and has FIP in vrf2
// Route for 3.1.1.1 leaked from vrf3 to vrf2. Order of members reversed
// Route for 2.1.1.20 leaked from vrf2 to vrf3. Order of members reversed
// Ping from 1.1.1.20 to 3.1.1.1
// Local Flow
TEST_F(EcmpFipTest, Local_Src_Fip_EcmpToNonEcmp_3) {
    // Add config for vrf3
    struct PortInfo input[] = {
        {"vnet200", 200, "3.1.1.1", "00:03:01:01:01:01", 3, 200},
        {"vnet201", 201, "3.1.1.1", "00:03:01:01:01:02", 3, 201},
        {"vnet202", 202, "3.1.1.1", "00:03:01:01:01:03", 3, 202},
    };
    IpamInfo ipam_info_3[] = {
        {"3.1.1.0", 24, "3.1.1.254"},
    };
    AddIPAM("vn3", ipam_info_3, 1);
    CreateVmportWithEcmp(input, 3);
    client->WaitForIdle();

    // Leak route for 2.1.1.20 into vrf-3. But with changed order of components
    LeakRoute("vrf2", "2.1.1.20", "vrf3");

    // Leak route for 3.1.1.1 into vrf-2. But with changed order of components
    LeakRoute("vrf3", "3.1.1.1", "vrf2");
    client->WaitForIdle();

    // Tx packet from port-21. Must go thru src-fip
    TxIpPacket(vmi_[21]->id(), "1.1.1.20", "3.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "3.1.1.1",
                              1, 0, 0, GetFlowKeyNH(21));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[21]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for port-22
    TxIpPacket(vmi_[22]->id(), "1.1.1.20", "3.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "3.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(22));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[22]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for port-23
    TxIpPacket(vmi_[23]->id(), "1.1.1.20", "3.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.20", "3.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(23));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[23]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    DelIPAM("vn3");
    DeleteVmportEnv(input, 3, true);
    client->WaitForIdle();
}

// Ping from non-ecmp IP address to address in ECMP.
// Dest has FIP
// Local Flow
TEST_F(EcmpFipTest, Local_Dst_Fip_NonEcmpToEcmp_1) {
    TxIpPacket(VmPortGetId(101), "2.1.1.1", "2.1.1.20", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf2"), "2.1.1.1", "2.1.1.20",
                              1, 0, 0, GetFlowKeyNH(101));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[101]->flow_key_nh());
    EXPECT_TRUE(flow->IsNatFlow());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
}

// Ping from non-ecmp to ECMP address.
// Destination has FIP
// Local flow
TEST_F(EcmpFipTest, Remote_Dst_Fip_EcmpToNonEcmp_1) {
    TxIpMplsPacket(eth_intf_id_, REMOTE_COMPUTE_1,
                   agent_->router_id().to_string().c_str(),
                   vmi_[2]->label(), FIP_REMOTE_ECMP_1, FIP_NON_ECMP_1, 1);
    client->WaitForIdle();

    FlowEntry *flow =
        FlowGet(GetVrfId("vrf1"), FIP_REMOTE_ECMP_1, FIP_NON_ECMP_1, 1, 0, 0,
                GetFlowKeyNH(2));
    EXPECT_TRUE(flow);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    InetUnicastRouteEntry *rt =
        RouteGet("vrf2", Ip4Address::from_string(FIP_REMOTE_ECMP_1), 32);
    EXPECT_TRUE(flow->rpf_nh() == rt->GetActiveNextHop());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[2]->flow_key_nh());
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
