/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <base/os.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#include "test_flow_base.cc"

// FloatingIP test for traffic from VM to local VM with PortMap
TEST_F(FlowTest, PortMap_Fabric_To_Fip_1) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, NULL, true, 80);
    client->WaitForIdle();

    // Send TCP packet from fabric to floating-ip of flow0 interface
    TxTcpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                    remote_vm3_ip, "14.1.1.100", 1000, 80, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_TCP, 1000,
                            80, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_TCP, 1080,
                             1000, flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);

    // Send UDP packet from fabric to floating-ip of flow0 interface
    TxUdpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                    remote_vm3_ip, "14.1.1.100", 1000, 80, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_UDP, 1000, 80,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_UDP, 1080, 1000,
                  flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

// Packet with port not in PortMap
TEST_F(FlowTest, PortMap_Fabric_To_Fip_2) {
    client->agent()->flow_stats_manager()->
        default_flow_stats_collector_obj()->SetExpiryTime(1000*1000);
    client->agent()->flow_stats_manager()->set_delete_short_flow(true);
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, NULL, true, 80);
    client->WaitForIdle();

    // Send TCP packet from fabric to floating-ip of flow0 interface
    TxTcpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                    remote_vm3_ip, "14.1.1.100", 1000, 180, false);
    client->WaitForIdle();

    // PortMap not found. Flow created with NAT-port as 0
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_TCP, 1000,
                            180, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_TCP, 0,
                             1000, flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);

    // Send UDP packet from fabric to floating-ip of flow0 interface
    TxUdpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                    remote_vm3_ip, "14.1.1.100", 1000, 180, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_UDP, 1000, 180,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_UDP, 0, 1000,
                  flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

// FloatingIP test for traffic from VM to local VM with PortMap
TEST_F(FlowTest, PortMap_Fip_To_Fabric_1) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, NULL, true, 80);
    client->WaitForIdle();

    // Send TCP packet from fabric to floating-ip of flow0 interface
    TxTcpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1080, 1000, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    FlowEntry *fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_TCP, 1080,
                            1000, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_TCP, 1000,
                             80, flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);

    // Send UDP packet from fabric to floating-ip of flow0 interface
    TxUdpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1080, 1000, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_UDP, 1080, 1000,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    rfe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_UDP, 1000, 80,
                  flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

// Packet with port not in PortMap
TEST_F(FlowTest, PortMap_Fip_To_Fabric_2) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, NULL, true, 80);
    client->WaitForIdle();

    // Send TCP packet from fabric to floating-ip of flow0 interface
    TxTcpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 80, 1000, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    FlowEntry *fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_TCP, 80,
                            1000, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_TCP, 1000,
                             0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);

    // Send UDP packet from fabric to floating-ip of flow0 interface
    TxUdpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 80, 1000, false);
    client->WaitForIdle();

    // Validate flow created with port-nat
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_UDP, 80, 1000,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);

    rfe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_UDP, 1000, 0,
                  flow0->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

// With direction set "both", allow both "ingress-traffic" and "egress-traffic"
TEST_F(FlowTest, Direction_Both_1) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip);
    client->WaitForIdle();

    // Send packet from fabric to floating-ip of flow0 interface
    TxIpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                   remote_vm3_ip, "14.1.1.100", 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_ICMP, 0,
                            0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    FlushFlowTable();

    // Send packet from VM
    TxIpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_ICMP, 0, 0,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    FlushFlowTable();

    //EXPECT_EQ(fe->short_flow_reason(), FlowEntry::SHORT_NO_DST_ROUTE);
    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

TEST_F(FlowTest, Direction_Ingress_1) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, "ingress");
    client->WaitForIdle();

    // Send packet from fabric to floating-ip of flow0 interface
    TxIpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                   remote_vm3_ip, "14.1.1.100", 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_ICMP, 0,
                            0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    FlushFlowTable();

    // Send packet from VM
    TxIpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_ICMP, 0, 0,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_FALSE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_TRUE(fe->IsShortFlow());
    EXPECT_EQ(fe->short_flow_reason(), FlowEntry::SHORT_NO_DST_ROUTE);
    FlushFlowTable();

    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

TEST_F(FlowTest, Direction_Egress_1) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, "egress");
    client->WaitForIdle();

    // Send packet from fabric to floating-ip of flow0 interface
    TxIpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                   remote_vm3_ip, "14.1.1.100", 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_ICMP, 0,
                            0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->IsShortFlow());
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_EQ(fe->short_flow_reason(), FlowEntry::SHORT_NO_SRC_ROUTE);
    FlushFlowTable();

    // Send packet from VM
    TxIpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_ICMP, 0, 0,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_EQ(fe->short_flow_reason(), 0);
    FlushFlowTable();

    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}

TEST_F(FlowTest, UnderlayVmiWithOverlayFip) {
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm3_ip,
                      remote_router_ip, 30, "default-project:vn4");
    AddFloatingIp("fip1", 1, "14.1.1.100", vm1_ip, "egress");
    client->WaitForIdle();

    // Send packet from fabric to floating-ip of flow0 interface
    TxIpMplsPacket(eth->id(), "10.1.1.2", router_id_, flow0->label(),
                   remote_vm3_ip, "14.1.1.100", 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    FlowEntry *fe = FlowGet(0, remote_vm3_ip, "14.1.1.100", IPPROTO_ICMP, 0,
                            0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->IsShortFlow());
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_EQ(fe->short_flow_reason(), FlowEntry::SHORT_NO_SRC_ROUTE);
    FlushFlowTable();

    // Send packet from VM
    TxIpPacket(flow0->id(), vm1_ip, remote_vm3_ip, 1);
    client->WaitForIdle();

    // Validate flow created with floating-ip translation
    fe = FlowGet(0, vm1_ip, remote_vm3_ip, IPPROTO_ICMP, 0, 0,
                 flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe != NULL);

    EXPECT_TRUE(fe->is_flags_set(FlowEntry::NatFlow));
    EXPECT_FALSE(fe->IsShortFlow());
    EXPECT_EQ(fe->short_flow_reason(), 0);
    FlushFlowTable();

    DeleteRemoteRoute("vrf4", remote_vm3_ip);
}


int main(int argc, char *argv[]) {
    GETUSERARGS();

    client =
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10), (3 * 60 * 1000));
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->fabric_interface_name();
    } else {
        eth_itf = "eth0";
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_itf,
                                Agent::GetInstance()->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false,
                                boost::uuids::nil_uuid(), Ip4Address(0),
                                Interface::TRANSPORT_ETHERNET);
        client->WaitForIdle();
    }

    FlowTest::TestSetup(ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
