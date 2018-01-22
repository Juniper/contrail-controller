/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#include "test_flow_base.cc"


TEST_F(FlowTest, UnderlayWithinSameVn) {
    //Make VN5 as underlay
    AddLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    TxIpPacket(flow0->id(), vm1_ip, vm2_ip, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm1_ip, vm2_ip, IPPROTO_ICMP, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());

    VrfEntry *vrf = VrfGet("vrf5");

    VnListType vn_list;
    vn_list.insert("vn5");

    EXPECT_TRUE(fe->data().src_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().dst_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().source_vn_list == vn_list);
    EXPECT_TRUE(fe->data().dest_vn_list == vn_list);

    DelLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
}

TEST_F(FlowTest, UnderlayFipToInstanceIp) {
    AddLink("virtual-network", "default-project:vn4", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    AddLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    client->WaitForIdle();

    TxIpPacket(flow0->id(), vm1_ip, vm5_ip, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm1_ip, vm5_ip, IPPROTO_ICMP, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));

    VrfEntry *vrf = VrfGet("default-project:vn4:vn4");

    VnListType vn_list;
    vn_list.insert("default-project:vn4");

    EXPECT_TRUE(fe->data().src_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().dst_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().dest_vrf == 0);
    EXPECT_TRUE(fe->data().source_vn_list == vn_list);
    EXPECT_TRUE(fe->data().dest_vn_list == vn_list);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().dest_vrf ==
                VrfGet("vrf5")->vrf_id());

    DelLink("virtual-network", "default-project:vn4", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    fe = FlowGet(0, vm1_ip, vm5_ip, IPPROTO_ICMP, 0, 0,
            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe == NULL);

    DelLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    client->WaitForIdle();
}

TEST_F(FlowTest, UnderlayInstanceIpToOverlayFip) {
    AddLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();
    AddLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    client->WaitForIdle();

    TxIpPacket(flow0->id(), vm1_ip, vm5_ip, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm1_ip, vm5_ip, IPPROTO_ICMP, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));

    VrfEntry *vrf = VrfGet("default-project:vn4:vn4");

    VnListType vn_list;
    vn_list.insert("default-project:vn4");

    EXPECT_TRUE(fe->data().dest_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().source_vn_list == vn_list);
    EXPECT_TRUE(fe->data().dest_vn_list == vn_list);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().dest_vrf == 0);

    DelLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    DelLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();
}


TEST_F(FlowTest, UnderlayFipToFip) {
    FlowSetup();

    AddLink("virtual-network", "default-project:vn4", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    AddLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    AddLink("floating-ip", "fip2", "virtual-machine-interface", "flow5");
    client->WaitForIdle();

    TxIpPacket(flow0->id(), vm1_ip, vm1_fip2, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm1_ip, vm1_fip2, IPPROTO_ICMP, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));

    VrfEntry *vrf = VrfGet("default-project:vn4:vn4");
    VrfEntry *vrf6 = VrfGet("vrf6");
   
    VnListType vn_list;
    vn_list.insert("default-project:vn4");

    EXPECT_TRUE(fe->data().src_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().dst_policy_vrf == vrf->vrf_id());
    EXPECT_TRUE(fe->data().dest_vrf == vrf6->vrf_id());
    EXPECT_TRUE(fe->data().source_vn_list == vn_list);
    EXPECT_TRUE(fe->data().dest_vn_list == vn_list);
    EXPECT_TRUE(fe->reverse_flow_entry()->data().dest_vrf == 0);

    DelLink("virtual-network", "default-project:vn4", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));

    AddLink("floating-ip", "fip1", "virtual-machine-interface", "flow0");
    AddLink("floating-ip", "fip2", "virtual-machine-interface", "flow5");
    FlowTeardown();
}

TEST_F(FlowTest, OverlayToUnderlayTransition) {
    TxL2Packet(flow0->id(), "00:00:00:01:01:01",
               "00:00:00:01:01:02", vm1_ip, vm2_ip, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm1_ip, vm2_ip, IPPROTO_ICMP, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());

    AddLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());

    DelLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
}

TEST_F(FlowTest, OverlayIpToUnderlayIp) {
    AddLink("virtual-network", "vn5", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();

    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);
    client->WaitForIdle();

    TxIpPacket(flow3->id(), vm4_ip, vm1_ip, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, vm4_ip, vm1_ip, IPPROTO_ICMP, 0, 0,
                            flow3->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_TRUE(fe->data().dest_vrf == flow3->vrf()->vrf_id());
    EXPECT_TRUE(fe->reverse_flow_entry()->data().dest_vrf ==
                flow3->vrf()->vrf_id());

    DelLink("virtual-network", "default-project:vn4", "virtual-network",
            client->agent()->fabric_vn_name().c_str());
    client->WaitForIdle();
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();

}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client =
        TestInit(init_file, ksync_init, true, true, true,
                 (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                eth_itf,
                Agent::GetInstance()->fabric_vrf_name(),
                PhysicalInterface::FABRIC,
                PhysicalInterface::ETHERNET, false, nil_uuid(),
                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}

