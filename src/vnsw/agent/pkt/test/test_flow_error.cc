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
TEST_F(FlowTest, FlowOnDeletedInterface) {
    struct PortInfo input[] = {
        {"flow5", 11, "11.1.1.3", "00:00:00:01:01:01", 5, 6},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    int nh_id = GetFlowKeyNH(input[0].intf_id);

    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));
    //Delete the interface with reference help
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    TxTcpPacket(intf->id(), "11.1.1.3", vm1_ip, 30, 40, false, 1,
               VrfGet("vrf5")->vrf_id());
    client->WaitForIdle();

    //Flow find should fail as interface is delete marked, and packet get dropped
    // in packet parsing
    FlowEntry *fe = FlowGet(VrfGet("vrf5")->vrf_id(), "11.1.1.3", vm1_ip,
                            IPPROTO_TCP, 30, 40, nh_id);
    EXPECT_TRUE(fe == NULL);
}

TEST_F(FlowTest, FlowOnDeletedVrf) {
    struct PortInfo input[] = {
        {"flow5", 11, "11.1.1.3", "00:00:00:01:01:01", 5, 6},
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    InterfaceRef intf(VmInterfaceGet(input[0].intf_id));
    //Delete the VRF
    DelVrf("vrf5");
    client->WaitForIdle();

    TxTcpPacket(intf->id(), "11.1.1.3", vm1_ip, 30, 40, false, 1, vrf_id);
    client->WaitForIdle();

    //Flow find should fail as interface is delete marked
    FlowEntry *fe = FlowGet(vrf_id, "11.1.1.3", vm1_ip,
                            IPPROTO_TCP, 30, 40, 0);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_UNAVIALABLE_INTERFACE);
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_return_error) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
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
        }
    };

    flow[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    /* Failure to allocate reverse flow index, convert to short flow and age */
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, -ENOSPC);
    CreateFlow(flow, 1);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                            GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) != true);

    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    client->WaitForIdle();
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
    }

    client->WaitForIdle();
    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));

    flow[0].pkt_.set_allow_wait_for_idle(false);
    sock->SetBlockMsgProcessing(true);
    /* EBADF failure to write an entry, covert to short flow and age */
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, -EBADF);
    CreateFlow(flow, 1);

    fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) != true);
    sock->SetBlockMsgProcessing(false);
    flow[0].pkt_.set_allow_wait_for_idle(true);
    client->WaitForIdle();
    if (fe != NULL) {
        WAIT_FOR(1000, 500, (fe->is_flags_set(FlowEntry::ShortFlow) == true));
        EXPECT_TRUE(fe->short_flow_reason() == FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
    }

    client->EnqueueFlowAge();
    client->WaitForIdle();

    EXPECT_TRUE(FlowTableWait(0));

    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, 0);
}

//Create short-flow and delete the flow entry
//before vrouter sends a response for flow
//verify flow gets cleaned up
TEST_F(FlowTest, Flow_return_error_1) {
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    client->WaitForIdle();

    //Create short flow
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm1_ip, 1, 0, 0, "vrf5",
                    flow0->id()),
            {new ShortFlow()}
        }
    };

    sock->SetBlockMsgProcessing(true);
    /* Failure to allocate reverse flow index, convert to short flow and age */
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, -ENOSPC);
    flow[0].pkt_.set_allow_wait_for_idle(false);
    CreateFlow(flow, 1);

    uint32_t vrf_id = VrfGet("vrf5")->vrf_id();
    FlowEntry *fe = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 1, 0, 0,
                            GetFlowKeyNH(input[0].intf_id));

    WAIT_FOR(1000, 1000, (flow_stats_collector_->Size() == 2));
    client->EnqueueFlowAge();
    WAIT_FOR(1000, 1000, (fe->deleted() == true));
    assert(fe->deleted());
    sock->SetBlockMsgProcessing(false);
    client->WaitForIdle();
    EXPECT_TRUE(FlowTableWait(0));
    sock->SetKSyncError(KSyncSockTypeMap::KSYNC_FLOW_ENTRY_TYPE, 0);
}

//Test for subnet broadcast flow
TEST_F(FlowTest, Subnet_broadcast_Flow) {
    IpamInfo ipam_info[] = {
        {"11.1.1.0", 24, "11.1.1.200", true},
    };
    AddIPAM("vn5", ipam_info, 1);
    client->WaitForIdle();

    //Get the route
    if (!RouteFind("vrf5", "11.1.1.255", 32)) {
        return;
    }

    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, vm1_ip, "11.1.1.255", 1, 0, 0, "vrf5",
                       flow0->id()),
        {}
        },
        {  TestFlowPkt(Address::INET, vm2_ip, vm1_ip, 1, 0, 0, "vrf5",
                       flow1->id()),
        {}
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(4U, get_flow_proto()->FlowCount());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev_fe = fe->reverse_flow_entry();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(rev_fe->is_flags_set(FlowEntry::Multicast));
    EXPECT_TRUE(fe->data().match_p.action_info.action == 
                (1 << TrafficAction::PASS));
    EXPECT_TRUE(rev_fe->data().match_p.action_info.action == 
                (1 << TrafficAction::PASS));
    //fe->data.match_p.flow_action.action

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const VnEntry *vn = fe->data().vn_entry.get();
    get_flow_proto()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(3U, in_count);
    EXPECT_EQ(3U, out_count);

    DelIPAM("vn5");
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_ksync_nh_state_find_failure) {
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

    DBTableBase *table = Agent::GetInstance()->nexthop_table();
    NHKSyncObject *nh_object = Agent::GetInstance()->ksync()->nh_ksync_obj();
    DBTableBase::ListenerId nh_listener =
        table->Register(boost::bind(&NHNotify, _1, _2));

    vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(1001);
    EXPECT_TRUE((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) == 0);

    nh_object->set_test_id(nh_listener);
    CreateFlow(flow, 1);

    EXPECT_TRUE((vr_flow->fe_flags & VR_FLOW_FLAG_ACTIVE) != 0);
    DeleteFlow(flow, 1);
    EXPECT_TRUE(FlowTableWait(0));
    nh_object->set_test_id(-1);
    table->Unregister(nh_listener);
    DeleteRemoteRoute("vrf5", remote_vm1_ip);
    client->WaitForIdle();
}

TEST_F(FlowTest, Flow_Deleted_Interace_nh) {
    EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    //Create PHYSICAL interface to receive GRE packets on it.
    FlowSetup();
    PhysicalInterfaceKey key(eth_itf);
    Interface *intf = static_cast<Interface *>
        (agent()->interface_table()->FindActiveEntry(&key));

    EXPECT_TRUE(intf != NULL);
    CreateRemoteRoute("vrf6", remote_vm1_ip, remote_router_ip, 30, "vn6");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm_a_ip, remote_vm1_ip, 1, 0, 0, "vrf6",
                    flow5->id()),
            {
            }
        }
    };
   //remove policy enabled Interface
   DBRequest req(DBRequest::DB_ENTRY_DELETE);
   req.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, flow5->GetUuid(), ""),
                   true, InterfaceNHFlags::INET4,
                   flow5->vm_mac()));
   req.data.reset(NULL);
   NextHopTable::GetInstance()->Enqueue(&req);
   //remove policy disabled Interface
   DBRequest req1(DBRequest::DB_ENTRY_DELETE);
   client->WaitForIdle(1);
   req1.key.reset(new InterfaceNHKey
                  (new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, flow5->GetUuid(), ""),
                   false, InterfaceNHFlags::INET4,
                   flow5->vm_mac()));
   req1.data.reset(NULL);
   NextHopTable::GetInstance()->Enqueue(&req1);

   CreateFlow(flow, 1);
   client->WaitForIdle();

   FlowEntry *fe =
        FlowGet(VrfGet("vrf6")->vrf_id(), vm_a_ip, remote_vm1_ip, 1, 0, 0,
                GetFlowKeyNH(input4[0].intf_id));
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true);
    DeleteFlow(flow, 1);
    DeleteRemoteRoute("vrf6", remote_vm1_ip);
    FlowTeardown();
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
