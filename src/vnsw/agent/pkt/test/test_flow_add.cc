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
//Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_1) {
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5"),
            new VerifyDestVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5"),
            new VerifyDestVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5"),
            new VerifyDestVrf("vrf5", "vrf5")
        }
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000,
                       "vrf5", flow1->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5"),
            new VerifyDestVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());

    FetchAllFlowRecords *all_flow_records_sandesh = new FetchAllFlowRecords();
    Sandesh::set_response_callback(boost::bind(&FlowTest::CheckSandeshResponse,
                                               this, _1, 4));
    all_flow_records_sandesh->HandleRequest();
    client->WaitForIdle();
    all_flow_records_sandesh->Release();

    FetchFlowRecord *flow_record_sandesh = new FetchFlowRecord();
    flow_record_sandesh->set_nh(GetFlowKeyNH(input[0].intf_id));
    flow_record_sandesh->set_sip(vm1_ip);
    flow_record_sandesh->set_dip(vm2_ip);
    flow_record_sandesh->set_src_port(1000);
    flow_record_sandesh->set_dst_port(200);
    flow_record_sandesh->set_protocol(IPPROTO_TCP);
    flow_record_sandesh->HandleRequest();
    client->WaitForIdle();
    flow_record_sandesh->Release();

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);
}

//Egress flow test (IP fabric to VMPort - Same VN)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_2) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);

    //Create remote VM route. This will be used to figure out destination VN for
    //flow
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();

    CreateRemoteRoute("vrf5", remote_vm3_ip, remote_router_ip, 32, "vn5");
    client->WaitForIdle();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, flow0->label()),
            { 
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a ICMP reply from local to remote VM
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP flow from remote VM to local VM
        {
            TestFlowPkt(Address::INET, remote_vm3_ip, vm3_ip, IPPROTO_TCP, 1001, 1002,
                    "vrf5", remote_router_ip, flow2->label()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        },
        //Send a TCP reply from local VM to remote VM
        {
            TestFlowPkt(Address::INET, vm3_ip, remote_vm3_ip, IPPROTO_TCP, 1002, 1001,
                    "vrf5", flow2->id()),
            {
                new VerifyVn("vn5", "vn5"),
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 4);
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    DeleteRemoteRoute("vrf5", remote_vm3_ip);
    client->WaitForIdle();
}

//Ingress flow test (VMport to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(FlowTest, FlowAdd_3) {

    /* Add Local VM route of vrf3 to vrf5 */
    CreateLocalRoute("vrf5", vm4_ip, flow3, 19);
    /* Add Local VM route of vrf5 to vrf3 */
    CreateLocalRoute("vrf3", vm1_ip, flow0, 16);

    TestFlow flow[] = {
        //Send a ICMP request from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an ICMP reply from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, 1, 0, 0, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP packet from local VM in vn5 to local VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 200, 300, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send an TCP packet from local VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm4_ip, vm1_ip, IPPROTO_TCP, 300, 200, "vrf3",
                    flow3->id()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 4);
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRoute("vrf5", vm4_ip);
    DeleteRoute("vrf3", vm1_ip);
    client->WaitForIdle();
}

//Egress flow test (IP fabric to VMport - Different VNs)
//Flow creation using GRE packets
TEST_F(FlowTest, FlowAdd_4) {
    /* Add remote VN route to vrf5 */
    CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3");
    Ip4Address rid1 = agent()->router_id();
    std::string router_ip_str = rid1.to_string();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, flow0->label()),
            { 
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a ICMP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, IPPROTO_TCP, 1006, 1007,
                    "vrf5", remote_router_ip, flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP reply from local VM in vn5 to remote VM in vn3
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm4_ip, IPPROTO_TCP, 1007, 1006,
                    "vrf5", flow0->id()),
            {
                new VerifyVn("vn5", "vn3"),
            }
        }
    };

    CreateFlow(flow, 4); 
    client->WaitForIdle();
    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Verify ingress and egress flow count of VN "vn3"
    fe = flow[1].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //1. Remove remote VM routes
    DeleteRemoteRoute("vrf5", remote_vm4_ip);
    client->WaitForIdle();
}

//Duplicate Ingress flow test (VMport to VMport - Same VN)
//Flow creation using IP
TEST_F(FlowTest, FlowAdd_5) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Send duplicate flow creation request
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count for VN "vn5" does not change
    fe = flow[0].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

}

//Duplicate Ingress flow test for flow having reverse flow (VMport to VMport - Same VN)
//Flow creation using TCP packets
TEST_F(FlowTest, FlowAdd_6) {
    TestFlow fwd_flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200, "vrf5",
                    flow0->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    TestFlow rev_flow[] = {
        {
            TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, 200, 1000, "vrf5",
                    flow1->id()),
            { 
                new VerifyVn("vn5", "vn5"),
            }
        }
    };


    CreateFlow(fwd_flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count of VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = fwd_flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Send request for reverse flow
    CreateFlow(rev_flow, 1);
    //Send request for reverse flow again
    CreateFlow(rev_flow, 1);
    //Send request for forward flow again 
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    //Verify ingress and egress flow count for VN "vn5" does not change
    fe = fwd_flow[0].pkt_.FlowFetch();
    vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);
}

TEST_F(FlowTest, FlowIndexChange) {
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            { }
        }
    };
    TestFlow flow_new[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 3),
            { }
        }
    };
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(1);
    EXPECT_TRUE(((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) == VR_FLOW_FLAG_ACTIVE));

    /* Change Index for flow. */
    CreateFlow(flow_new, 1);
    EXPECT_EQ(2U, get_flow_proto()->FlowCount());

    /* verify the entry on hash id 1 being deleted */
    EXPECT_TRUE(((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) != VR_FLOW_FLAG_ACTIVE));

    DeleteFlow(flow_new, 1);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
}

// Validate flows to pkt 0 interface
TEST_F(FlowTest, Flow_On_PktIntf) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "11.1.1.254", 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new VerifyVrf("vrf5", "vrf5")
            }
        }
    };

    CreateFlow(flow, 1);
}


// Validate short flows
TEST_F(FlowTest, DISABLED_ShortFlow_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, vm1_ip, "115.115.115.115", 1, 0, 0, "vrf5",
                    flow0->id()),
            {
                new ShortFlow()
            }
        }
    };

    CreateFlow(flow, 1);
    int nh_id = InterfaceTable::GetInstance()->FindInterface(flow0->id())->flow_key_nh()->id();
    FlowEntry *fe = FlowGet(1, vm1_ip, "115.115.115.115", 1,
                            0, 0, nh_id);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_NO_DST_ROUTE);
}

//Src port and dest port should be ignored for non TCP and UDP flows
TEST_F(FlowTest, ICMPPortIgnoreTest) {
    AddAcl("acl1", 1, "vn5" , "vn5", "pass");
    client->WaitForIdle();
    for (uint32_t i = 0; i < 1; i++) {
        TestFlow flow[] = {
            {
                TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_ICMP, 0, 0, "vrf5",
                            flow1->id(), 1),
                {
                    new VerifyVn("vn5", "vn5"),
                    new VerifyFlowAction(TrafficAction::PASS)
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

//Test to make sure egress VN ACL doesnt apply VRF translate rule
TEST_F(FlowTest, EgressVNVrfTranslate) {
    //Leak a route for vm4 ip in vrf5
    VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(9));

    CreateLocalRoute("vrf5", vm4_ip, vm_intf, vm_intf->label());
    client->WaitForIdle();

    AddVrfAssignNetworkAcl("Acl", 10, "any", "any", "pass", "vrf9");
    AddLink("virtual-network", "vn3", "access-control-list", "Acl");
    DelLink("virtual-network", "vn5", "access-control-list", "acl1");
    client->WaitForIdle();

    TestFlow flow[] = {
        {
        TestFlowPkt(Address::INET, vm1_ip, vm4_ip, IPPROTO_TCP, 10, 10, "vrf5",
                flow0->id(), 1),
        {
            new VerifyVn("vn5", "vn3"),
        }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip,
                            IPPROTO_TCP, 10, 10, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    EXPECT_FALSE(fe->match_p().action_info.action &
                (1 << TrafficAction::VRF_TRANSLATE));
    DeleteRoute("vrf5", vm4_ip);
    client->WaitForIdle();
    //Restore ACL
    DelLink("virtual-network", "vn3", "access-control-list", "Acl");
    AddLink("virtual-network", "vn5", "access-control-list", "acl1");
    client->WaitForIdle();

    //Delete the acl
    DelOperDBAcl(10);
    client->WaitForIdle();
}

TEST_F(FlowTest, DISABLED_Flow_entry_reuse) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());

    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1001),
            {}
        }
    };
    TestFlow flow1[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1002),
            {}
        }
    };  
    CreateFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(2));
    FlowEntry *fe = 
        FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, remote_vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->flow_handle() == 1001);

    flow[0].pkt_.set_allow_wait_for_idle(false);
    flow1[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(2));
    CreateFlow(flow1, 1);
    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    flow1[0].pkt_.set_allow_wait_for_idle(true);
    WAIT_FOR(1000, 1000, (fe->deleted() == false));
    client->WaitForIdle();
    FlowTableKSyncEntry *fe_ksync = fe->flow_table()->ksync_object()->Find(fe);
    WAIT_FOR(1000, 1000, (fe_ksync->GetState() == KSyncEntry::IN_SYNC));

    EXPECT_TRUE(fe->flow_handle() == 1002);
    DeleteFlow(flow1, 1);
    EXPECT_TRUE(FlowTableWait(0));
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}
// If route changes, flows referring it must be revaluated
TEST_F(FlowTest, FlowReval_1) {

    //Create PHYSICAL interface to receive GRE packets on it.
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE(intf != NULL);
    CreateRemoteRoute("vrf5", remote_vm1_ip_subnet, remote_vm1_ip_plen,
                      remote_router_ip, 30, "vn5");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {}
        },
        {
            TestFlowPkt(Address::INET, remote_vm1_ip, vm1_ip, 1, 0, 0, "vrf5",
                    remote_router_ip, 16),
            {}
        }
    };

    CreateFlow(flow, 1);
    // Add reverse flow
    CreateFlow(flow + 1, 1);

    FlowEntry *fe =
        FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, remote_vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input[0].intf_id));
    client->WaitForIdle();
    EXPECT_EQ(fe->data().dest_vn_match, "vn5");
    // Add a non-matching /32 route and verify that flow is not modified
    client->WaitForIdle();
    CreateRemoteRoute("vrf5", remote_vm1_ip_5, remote_router_ip, 30, "vn5_1");
    EXPECT_EQ(fe->data().dest_vn_match, "vn5");
    client->WaitForIdle();
    // Add more specific route and verify that flow is updated
    CreateRemoteRoute("vrf5", remote_vm1_ip, remote_router_ip, 30, "vn5_3");
    EXPECT_EQ(fe->data().dest_vn_match, "vn5_3");
    client->WaitForIdle();
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    DeleteRemoteRoute("vrf5", remote_vm1_ip_subnet, remote_vm1_ip_plen);
    DeleteRemoteRoute("vrf5", remote_vm1_ip_5);
    client->WaitForIdle();
    DeleteRemoteRoute("vrf5", remote_vm1_ip, 32);
    client->WaitForIdle();
    client->WaitForIdle();
}

//Create a layer2 flow and verify that we dont add layer2 route
//in prefix length manipulation
TEST_F(FlowTest, WaitForTraffic) {
    Ip4Address ip = Ip4Address::from_string(vm1_ip);
    MacAddress mac(0, 0, 0, 1, 1, 1);

    AgentRoute *ip_rt = RouteGet("vrf5", Ip4Address::from_string(vm1_ip), 32);
    AgentRoute *evpn_Rt = EvpnRouteGet("vrf5", mac, ip, 0);

    EXPECT_TRUE(ip_rt->WaitForTraffic() == true);
    EXPECT_TRUE(evpn_Rt->WaitForTraffic() == true);

    //Enqueue a flow with wrong mac address
    TxL2Packet(flow0->id(),input[2].mac, input[1].mac,
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();
    //Only IP route should goto wait for traffic
    EXPECT_TRUE(ip_rt->WaitForTraffic() == false);
    EXPECT_TRUE(evpn_Rt->WaitForTraffic() == true);

    //Enqueue flow with right mac address
    //EVPN route should also goto traffic seen state
    TxL2Packet(flow0->id(),input[0].mac, input[1].mac,
            input[0].addr, input[1].addr, 1);
    client->WaitForIdle();
    EXPECT_TRUE(ip_rt->WaitForTraffic() == false);
    EXPECT_TRUE(evpn_Rt->WaitForTraffic() == false);
}

//Send traffic to DMAC pointing to receive route
//DMAC should not match VRRP mac and physical interface mac
//Make the VN as l3 only VN
//In pkt parsing this packet would be treated as l2 flow
//since DMAC neither matches VRRP mac nor physical interface mac
//Verify such packet results in flow setup
TEST_F(FlowTest, VCenterL3DisabledVn) {
    AddL3Vn("vn5", 1);
    client->WaitForIdle();

    MacAddress mac(0, 0xb, 0xc, 0xa, 0xf, 0xe);

    PathPreference p;
    agent_->fabric_evpn_table()->
        AddReceiveRouteReq(agent_->local_peer(), "vrf5", 0, mac,
                           Ip4Address(0), 0, "vn5", p);
    client->WaitForIdle();

    //Enqueue a flow with wrong mac address
    TxL2Packet(flow0->id(), input[0].mac, mac.ToString().c_str(),
               input[0].addr, input[1].addr, 1);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm2_ip,
                            IPPROTO_ICMP, 0, 0, flow0->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->l3_flow() == true);
    EXPECT_TRUE(fe->IsShortFlow() == false);

    agent_->fabric_evpn_table()->DeleteReq(agent_->local_peer(), "vrf5",
                                           mac, Ip4Address(0), 0, NULL);
    client->WaitForIdle();
    //Restore
    AddVn("vn5", 1);
    client->WaitForIdle();
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
