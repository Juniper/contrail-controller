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

//Test flow deletion on ACL deletion
TEST_F(FlowTest, AclDelete) {
    AddAcl("acl1", 1, "vn5" , "vn5", "pass");
    client->WaitForIdle();
    uint32_t sport = 30;
    for (uint32_t i = 0; i < 1; i++) {
        sport++;
        TestFlow flow[] = {
            {
                TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, sport, 40, "vrf5",
                            flow1->id(), 1),
                {
                    new VerifyVn("vn5", "vn5")
                }
            }
        };
        CreateFlow(flow, 1);
    }

    //Delete the acl
    DelOperDBAcl(1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

TEST_F(FlowTest, Flow_with_encap_change) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        },
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, flow0->label()),
            {}
        }   
    };

    CreateFlow(flow, 1);
    // Add reverse flow
    CreateFlow(flow + 1, 1);

    FlowEntry *fe = 
        FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));
    const NextHop *nh = fe->data().nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh.get() != NULL);
    nh = fe->data().nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh.get() != NULL);
    nh = fe->data().nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh.get() != NULL);
    nh = fe->data().nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    DelEncapList();
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().nh.get() != NULL);
    nh = fe->data().nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowFail(1, "1.1.1.1", "2.2.2.2", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowFail(1, "2.2.2.2", "1.1.1.1", 1, 0, 0,
                         GetFlowKeyNH(input[0].intf_id)));
    EXPECT_TRUE(FlowTableWait(0));
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

//Create a l2 flow and verify l3 route
//priority gets increased
TEST_F(FlowTest, TrafficPriority) {
    TxL2Packet(flow0->id(),input[0].mac, input[1].mac,
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string(vm1_ip);
    InetUnicastRouteEntry *rt = RouteGet("vrf5", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->path_preference().wait_for_traffic()
            == false);
    FlushFlowTable();
    client->WaitForIdle();
}

//Create a layer2 flow and verify that we dont add layer2 route
//in prefix length manipulation
TEST_F(FlowTest, Layer2PrefixManipulation) {
    DelLink("virtual-network", "vn5", "access-control-list", "acl1");
    //Add a vrf translate acl, so that packet is forced to go thru l3 processing
    AddVrfAssignNetworkAcl("Acl", 10, "vn5", "vn5", "pass", "vrf5");
    AddLink("virtual-network", "vn5", "access-control-list", "Acl");
    client->WaitForIdle();

    TxL2Packet(flow0->id(),input[0].mac, input[1].mac,
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();

    int nh_id = flow0->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, vm2_ip, 1, 0, 0, nh_id);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->data().flow_source_plen_map.size() == 0);

    FlushFlowTable();
    client->WaitForIdle();
    DelLink("virtual-network", "vn5", "access-control-list", "Acl");
    DelAcl("Acl");
    DelLink("virtual-network", "vn5", "access-control-list", "acl1");
    client->WaitForIdle();
}

TEST_F(FlowTest, AclRuleUpdate) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;

    FlowSetup();
    EXPECT_EQ(flow5->sg_list().list_.size(), 0U);
    AddSgEntry("sg_e", "sg_acl_e", 10, 1, "pass", EGRESS,
               "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2", "0");
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    client->WaitForIdle();

    AclDBEntry *acl = AclGet(10);
    EXPECT_TRUE(acl != NULL);
    EXPECT_TRUE(acl->IsRulePresent("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2"));

    strncpy(acl_name, "sg_acl_e", max_len);
    strncat(acl_name, "egress-access-control-list", max_len);

    //Update the ACL with new rule
    AddAclEntry(acl_name, 10, 1, "pass", "ge6a4dcb-dde4-48e6-8957-856a7aacb2d3");
    client->WaitForIdle();
    EXPECT_FALSE(acl->IsRulePresent("fe6a4dcb-dde4-48e6-8957-856a7aacb2d2"));
    EXPECT_TRUE(acl->IsRulePresent("ge6a4dcb-dde4-48e6-8957-856a7aacb2d3"));

    DelLink("virtual-machine-interface", "flow5", "security-group", "sg_e");
    DelLink("security-group", "sg_e", "access-control-list", acl_name);
    DelNode("access-control-list", acl_name);

    DelNode("security-group", "sg_e");
    FlowTeardown();
    client->WaitForIdle(5);
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
