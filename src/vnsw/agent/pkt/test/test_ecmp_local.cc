/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "test_ecmp.h"
#include "pkt/flow_proto.h"

class LocalEcmpTest : public EcmpTest {
public:
    LocalEcmpTest() : EcmpTest() { }
    virtual ~LocalEcmpTest() { }

    virtual void SetUp() {
        EcmpTest::SetUp();
    }

    virtual void TearDown() {
        EcmpTest::TearDown();
    }
};

// Ping from non-ECMP to ECMP
// All members of dest-ecmp on same host
TEST_F(LocalEcmpTest, NonEcmpToLocalEcmp_1) {
    for (int i = 0; i < 16; i++) {
        TxTcpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.10", 100, i, false);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10",
                                  6, 100, i, GetFlowKeyNH(1));
        EXPECT_TRUE(flow != NULL);
        EXPECT_TRUE(flow->data().component_nh_idx
                    != CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);
        const VmInterface *out_vmi = GetOutVmi(flow);
        EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    }
}

// Ping from non-ECMP to ECMP
// ECMP has both local and remote members
TEST_F(LocalEcmpTest, NonEcmpToHybridEcmp_1) {
    int local_count = 0;
    int remote_count = 0;
    for (int i = 0; i < 16; i++) {
        TxTcpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.30", 100, i, false);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.30",
                                  6, 100, i, GetFlowKeyNH(1));
        EXPECT_TRUE(flow != NULL);
        EXPECT_TRUE(flow->data().component_nh_idx
                    != CompositeNH::kInvalidComponentNHIdx);
        EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow->data().component_nh_idx ==
                    CompositeNH::kInvalidComponentNHIdx);

        const NextHop *nh = GetOutMemberNh(flow);
        if (dynamic_cast<const InterfaceNH *>(nh)) {
            local_count++;
            const VmInterface *out_vmi = GetOutVmi(flow);
            EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
        } else {
            remote_count++;
            // RPF in reverse flow points to composite-nh
            EXPECT_TRUE(rflow->rpf_nh() == GetOutNh(flow));
        }
    }

    EXPECT_TRUE(local_count > 0);
    EXPECT_TRUE(remote_count > 0);
}

// Ping from non-ECMP to ECMP
// All members of dest-ecmp on same host
// ECMP transitions to Non-ECMP
TEST_F(LocalEcmpTest, NonEcmpToLocalEcmp_EcmpTransition_1) {
    struct PortInfo input10_1[] = {
        {"vif11", 11, "1.1.1.10", "00:01:01:01:01:11", 1, 11},
    };
    struct PortInfo input10_2[] = {
        {"vif12", 12, "1.1.1.10", "00:01:01:01:01:12", 1, 12},
    };
    struct PortInfo input10_3[] = {
        {"vif13", 13, "1.1.1.10", "00:01:01:01:01:13", 1, 13},
    };

    // Delete vif13 as tests expects only 2 member ECMP
    DeleteVmportEnv(input10_3, 1, false);

    TxIpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());

    // Delete interface other than one being used
    if (out_vmi->name() == "vif11")
        DeleteVmportEnv(input10_2, 1, false);

    if (out_vmi->name() == "vif12")
        DeleteVmportEnv(input10_1, 1, false);

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10", 1, 0, 0,
                   GetFlowKeyNH(1));
    // Flow not ECMP anymore
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
}

// Ping from non-ECMP to non-ECMP
// Destination transitions from non-ECMP to ECMP
TEST_F(LocalEcmpTest, NonEcmpToLocalEcmp_EcmpTransition_2) {
    struct PortInfo input10_1[] = {
        {"vif11", 11, "1.1.1.10", "00:01:01:01:01:11", 1, 11},
        {"vif12", 12, "1.1.1.10", "00:01:01:01:01:12", 1, 12},
    };

    // Delete vif11 and vif12
    DeleteVmportEnv(input10_1, 2, false);
    client->WaitForIdle();

    TxIpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[13]->flow_key_nh());

    // Restore 2 more interfaces in ECMP
    CreateVmportWithEcmp(input10, 3);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10", 1, 0, 0,
                   GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    // Flow transitioned from Non-ECMP to ECMP. ecmp-index must be set to 0
    EXPECT_EQ(0U, flow->data().component_nh_idx);
    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[13]->flow_key_nh());
}

// Ping from non-ECMP to ECMP
// All members of dest-ecmp on same host
// Delete interface being used in ECMP. Flow must be deleted
TEST_F(LocalEcmpTest, NonEcmpToLocalEcmp_EcmpDel_1) {
    struct PortInfo input10_1[] = {
        {"vif11", 11, "1.1.1.10", "00:01:01:01:01:11", 1, 11},
    };
    struct PortInfo input10_2[] = {
        {"vif12", 12, "1.1.1.10", "00:01:01:01:01:12", 1, 12},
    };
    struct PortInfo input10_3[] = {
        {"vif13", 13, "1.1.1.10", "00:01:01:01:01:13", 1, 13},
    };

    TxIpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());

    // Delete interface other than one being used
    if (out_vmi->name() == "vif11")
        DeleteVmportEnv(input10_1, 1, false);

    if (out_vmi->name() == "vif12")
        DeleteVmportEnv(input10_2, 1, false);

    if (out_vmi->name() == "vif13")
        DeleteVmportEnv(input10_3, 1, false);

    WAIT_FOR(1000, 1000, (0 == flow_proto_->FlowCount()));
}

// Ping from non-ECMP to ECMP
// All members of dest-ecmp on same host
// Delete interface not being used in ECMP. Flow must be revaluated
TEST_F(LocalEcmpTest, NonEcmpToLocalEcmp_EcmpDel_2) {
    struct PortInfo input10_1[] = {
        {"vif11", 11, "1.1.1.10", "00:01:01:01:01:11", 1, 11},
    };
    struct PortInfo input10_2[] = {
        {"vif12", 12, "1.1.1.10", "00:01:01:01:01:12", 1, 12},
    };
    struct PortInfo input10_3[] = {
        {"vif13", 13, "1.1.1.10", "00:01:01:01:01:13", 1, 13},
    };

    TxIpPacket(vmi_[1]->id(), "1.1.1.1", "1.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    uint32_t ecmp_idx = flow->data().component_nh_idx;
    EXPECT_TRUE(ecmp_idx != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[1]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());

    // Delete interface other than one being used
    if (out_vmi->name() == "vif11")
        DeleteVmportEnv(input10_2, 1, false);

    if (out_vmi->name() == "vif12")
        DeleteVmportEnv(input10_3, 1, false);

    if (out_vmi->name() == "vif13")
        DeleteVmportEnv(input10_1, 1, false);

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.1", "1.1.1.10", 1, 0, 0,
                   GetFlowKeyNH(1));
    // On revaluation component_nh_idx must be retained
    EXPECT_EQ(ecmp_idx, flow->data().component_nh_idx);
    EXPECT_TRUE(flow->IsEcmpFlow());
    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    // out-vmi and rpf-nh must not change for reverse flow
    EXPECT_TRUE(rflow->intf_entry() == out_vmi);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
}

// Ping from Local only ECMP to non-ECMP
// Test repeated for all local members in ECMP
TEST_F(LocalEcmpTest, LocalEcmpToNonEcmp_1) {
    TxIpPacket(vmi_[11]->id(), "1.1.1.10", "1.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "1.1.1.1", 1, 0, 0,
                              GetFlowKeyNH(11));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[11]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-12
    TxIpPacket(vmi_[12]->id(), "1.1.1.10", "1.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "1.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(12));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[12]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat for vmi-13
    TxIpPacket(vmi_[13]->id(), "1.1.1.10", "1.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "1.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(13));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[13]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();
}

// Ping from Hybrid ECMP to non-ECMP.
// Test repeated for all local members in ECMP
TEST_F(LocalEcmpTest, HybridEcmpToNonEcmp_1) {
    TxIpPacket(vmi_[31]->id(), "1.1.1.30", "1.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.30", "1.1.1.1",
                              1, 0, 0, GetFlowKeyNH(31));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[31]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat test for vmi-32
    TxIpPacket(vmi_[32]->id(), "1.1.1.30", "1.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.30", "1.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(32));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[32]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Repeat test for vmi-33
    TxIpPacket(vmi_[33]->id(), "1.1.1.30", "1.1.1.1", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.30", "1.1.1.1", 1, 0, 0,
                   GetFlowKeyNH(33));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                == CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[33]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(rflow->rpf_nh() == vmi_[1]->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();
}

// Ping from ECMP source to ECMP destination in same VRF
// RPF-NH in both flow must be set as interface-nh
TEST_F(LocalEcmpTest, EcmpToEcmp_1) {
    TxIpPacket(vmi_[11]->id(), "1.1.1.10", "1.1.1.20", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "1.1.1.20",
                              1, 0, 0, GetFlowKeyNH(11));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[11]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
}

// Ping from ECMP source in vrf-1 to ECMP destination in vrf-2
// RPF-NH in both flow must be set as interface-nh
TEST_F(LocalEcmpTest, EcmpToEcmp_2) {
    AddIPAM("vn2", ipam_info_2, 1);
    CreateVmportWithEcmp(input22, 3);
    client->WaitForIdle();

    // Leak route for 1.1.1.10 into vrf-2. But with changed order of components
    LeakRoute("vrf1", "1.1.1.10", "vrf2");

    // Leak route for 2.1.1.10 into vrf-1. But with changed order of components
    LeakRoute("vrf2", "2.1.1.10", "vrf1");
    client->WaitForIdle();

    // Tx packet from port-11
    TxIpPacket(vmi_[11]->id(), "1.1.1.10", "2.1.1.10", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "2.1.1.10",
                              1, 0, 0, GetFlowKeyNH(11));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[11]->flow_key_nh());

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Tx packet from port-12
    TxIpPacket(vmi_[12]->id(), "1.1.1.10", "2.1.1.10", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "2.1.1.10", 1, 0, 0,
                   GetFlowKeyNH(12));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[12]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    // Tx packet from port-13
    TxIpPacket(vmi_[13]->id(), "1.1.1.10", "2.1.1.10", 1);
    client->WaitForIdle();

    flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "2.1.1.10", 1, 0, 0,
                   GetFlowKeyNH(13));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh() == vmi_[13]->flow_key_nh());

    rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
    FlushFlowTable();
    client->WaitForIdle();

    DeleteVmportEnv(input22, 3, true);
    DelIPAM("vn2");
    client->WaitForIdle();
}

// Ping from ECMP source-ip but with wrong VMI to ECMP destination in
// same VRF RPF-NH in both flow must be set as composite-nh
TEST_F(LocalEcmpTest, EcmpToEcmp_RpfFail_1) {
    TxIpPacket(vmi_[1]->id(), "1.1.1.10", "1.1.1.20", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "1.1.1.20",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx
                != CompositeNH::kInvalidComponentNHIdx);
    EXPECT_TRUE(flow->rpf_nh()->GetType() == NextHop::COMPOSITE);

    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow->data().component_nh_idx ==
                CompositeNH::kInvalidComponentNHIdx);
    const VmInterface *out_vmi = GetOutVmi(flow);
    EXPECT_TRUE(rflow->rpf_nh() == out_vmi->flow_key_nh());
}

TEST_F(LocalEcmpTest, Metadata_Ecmp_1) {
    const VmInterface *vhost =
        static_cast<const VmInterface *>(agent_->vhost_interface());

    TxTcpPacket(vhost->id(), vhost->primary_ip_addr().to_string().c_str(),
                vmi_[11]->mdata_ip_addr().to_string().c_str(), 100, 100, false,
                0);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, vhost->primary_ip_addr().to_string().c_str(),
                              vmi_[11]->mdata_ip_addr().to_string().c_str(),
                              IPPROTO_TCP, 100, 100,
                              vhost->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->data().component_nh_idx !=
                CompositeNH::kInvalidComponentNHIdx);

    InetUnicastRouteEntry *rt = RouteGet("vrf1", vmi_[11]->primary_ip_addr(),
                                         32);
    const CompositeNH *cnh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(cnh->GetNH(flow->data().component_nh_idx) ==
                vmi_[11]->l3_interface_nh_no_policy());

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
