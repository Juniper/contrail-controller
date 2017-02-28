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
#include "pkt/flow_proto.h"

#include "test_flow_base.cc"

#define uuid_1 "fe6a4dcb-dde4-48e6-8957-856a7aacb2d2"
#define uuid_2 "ge6a4dcb-dde4-48e6-8957-856a7aacb2d3"

class FlowUpdateTest : public FlowTest {
public:
    FlowUpdateTest() : FlowTest() {
    }

    virtual ~FlowUpdateTest() {
    }

    virtual void SetUp() {
        FlowTest::SetUp();
        FlowSetup();
        flow_stats_ = flow_proto_->flow_stats();
        EXPECT_EQ(flow5->sg_list().list_.size(), 0U);

        AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", EGRESS, uuid_1, "0");
        AddSgEntry("sg2", "sg_acl2", 11, 1, "deny", EGRESS, uuid_2, "0");
        AddLink("virtual-machine-interface", "flow5", "security-group", "sg1");
        client->WaitForIdle();
        EXPECT_EQ(flow5->sg_list().list_.size(), 1U);

        strcpy(sg1_acl_name_, "sg_acl1" "egress-access-control-list");
        strcpy(sg2_acl_name_, "sg_acl2" "egress-access-control-list");
        update_queue_ = GetUpdateFlowEventQueue();
        event_queue_ = GetFlowEventQueue(0);
        delete_queue_ = GetDeleteFlowEventQueue(0);
    }

    virtual void TearDown() {
        DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");
        DelLink("virtual-machine-interface", "flow5", "security-group", "sg2");
        DelLink("security-group", "sg1", "access-control-list", sg1_acl_name_);
        DelLink("security-group", "sg2", "access-control-list", sg2_acl_name_);
        DelNode("access-control-list", sg1_acl_name_);
        DelNode("access-control-list", sg2_acl_name_);

        DelNode("security-group", "sg1");
        DelNode("security-group", "sg2");
        client->WaitForIdle();
        FlowTeardown();
        client->WaitForIdle();

        FlowTest::TearDown();
    }

protected:
    char sg1_acl_name_[1024];
    char sg2_acl_name_[1024];
    const struct FlowStats *flow_stats_;
    FlowEventQueue *event_queue_;
    UpdateFlowEventQueue *update_queue_;
    DeleteFlowEventQueue *delete_queue_;
};

// Validate SG rule. This is basic test to ensure SG and and ACL rules 
// Subsequent tests are based on this
TEST_F(FlowUpdateTest, sg_basic_1) {
    AclDBEntry *acl = AclGet(10);
    EXPECT_TRUE(acl->IsRulePresent(uuid_1));

    //Update the ACL with new rule
    AddAclEntry(sg1_acl_name_, 10, 1, "pass", uuid_2);
    client->WaitForIdle();
    EXPECT_FALSE(acl->IsRulePresent(uuid_1));
    EXPECT_TRUE(acl->IsRulePresent(uuid_2));
}

// Test to verify that flow is recomputed when action in SG is changed
TEST_F(FlowUpdateTest, sg_change_1) {
    // Send packet to setup flow
    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Change action for the SG
    AddSgEntry("sg1", "sg_acl1", 10, 1, "deny", EGRESS, uuid_1, uuid_2);
    client->WaitForIdle();
    EXPECT_TRUE(flow->ActionSet(TrafficAction::DENY));
}

// Test to verify that flow is recomputed when SG for interface is changed
TEST_F(FlowUpdateTest, sg_change_2) {
    // Send packet
    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Add sg2 to interface. Traffic is PASS since sg1 is still linked
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg2");
    client->WaitForIdle();
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Del link from sg1 to interface. Action is DENY
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_TRUE(flow->ActionSet(TrafficAction::DENY));
}

// Test to verify that flow is recomputed when ACL rule in as SG is changed
TEST_F(FlowUpdateTest, sg_change_3) {
    // Send packet
    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Change ACL rule inside sg1
    AddAclEntry(sg1_acl_name_, 10, 1, "deny", uuid_1);
    client->WaitForIdle();
    EXPECT_TRUE(flow->ActionSet(TrafficAction::DENY));

    uint64_t update_count = flow_stats_->revaluate_count_;
    DelLink("virtual-machine-interface", "flow5", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_GE(flow_stats_->revaluate_count_, update_count);
}

// Change ACL linked to a VN
TEST_F(FlowUpdateTest, vn_change_1) {

    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Create new ACL and link it with vn
    AddAcl("acl1000", 1000, "vn6" , "vn6", "deny");
    AddLink("virtual-network", "vn6", "access-control-list", "acl1");
    AddLink("virtual-network", "vn6", "access-control-list", "acl1000");
    client->WaitForIdle();

    EXPECT_TRUE(flow->ActionSet(TrafficAction::DENY));

    //Delete the acl
    DelLink("virtual-network", "vn6", "access-control-list", "acl1000");
    DelNode("access-control-list", "acl1000");
    client->WaitForIdle();
}

// Test for multiple changes to be state compressed into single change
// Change SG used for interface and ACL changed for VN
TEST_F(FlowUpdateTest, multiple_change_1) {
    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Enable flow-update queue and validate statistics
    uint64_t update_count = update_queue_->events_processed();
    uint64_t event_count = event_queue_->events_processed();

    // Disable flow-update queue to ensure state compression
    flow_proto_->DisableFlowUpdateQueue(true);
    client->WaitForIdle();

    // Update interface config to use sg2
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg2");
    client->WaitForIdle();

    // Update VN to use new ACL
    AddAcl("acl1000", 1000, "vn6" , "vn6", "deny");
    AddLink("virtual-network", "vn6", "access-control-list", "acl1000");
    client->WaitForIdle();

    // Enable flow-update queue and validate statistics
    flow_proto_->DisableFlowUpdateQueue(false);
    client->WaitForIdle();

    // There can be 2 RECOMPUTE events - one for each flow
    EXPECT_LE((update_count + 2), update_queue_->events_processed());
    // There should be atmost one FLOW_MESSAGE
    EXPECT_LE((event_count + 1), event_queue_->events_processed());

    // Delete the acl
    DelLink("virtual-network", "vn6", "access-control-list", "acl1000");
    DelNode("access-control-list", "acl1000");
    client->WaitForIdle();
}

// Test for delete state-compressing pending changes for a flow-entry
TEST_F(FlowUpdateTest, multiple_change_delete_1) {
    TxIpPacket(flow5->id(), vm_a_ip, vm_b_ip, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    FlowEntry *flow = FlowGet(flow5->vrf()->vrf_id(), vm_a_ip, vm_b_ip, 1, 0,
                              0, flow5->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_FALSE(flow->ActionSet(TrafficAction::DENY));

    // Enable flow-update queue and validate statistics
    uint64_t update_count = update_queue_->events_processed();

    uint64_t delete_count = delete_queue_->events_processed();

    // Disable flow-update queue to ensure state compression
    flow_proto_->DisableFlowUpdateQueue(true);
    client->WaitForIdle();

    // Update interface config to use sg2
    AddLink("virtual-machine-interface", "flow5", "security-group", "sg2");
    client->WaitForIdle();

    // Update VN to use new ACL
    AddAcl("acl1000", 1000, "vn6" , "vn6", "deny");
    AddLink("virtual-network", "vn6", "access-control-list", "acl1000");
    client->WaitForIdle();

    // Enable flow-update queue and validate statistics
    flow_proto_->DisableFlowUpdateQueue(false);
    client->WaitForIdle();

    // Ensure that only one recompute is done
    EXPECT_LE((update_count + 2), update_queue_->events_processed());
    EXPECT_EQ((delete_count), delete_queue_->events_processed());

    update_count = update_queue_->events_processed();
    // Disable flow-update queue before deleting ACL
    flow_proto_->DisableFlowUpdateQueue(true);
    client->WaitForIdle();

    // Delete the acl
    DelLink("virtual-network", "vn6", "access-control-list", "acl1000");
    DelNode("access-control-list", "acl1000");
    client->WaitForIdle();

    // Enable flow-update queue and validate statistics
    flow_proto_->DisableFlowUpdateQueue(false);
    client->WaitForIdle();

    // Update queue has following events,
    // - 2 DELETE_DBENTRY delete for flows
    EXPECT_LE((update_count + 2), update_queue_->events_processed());
    EXPECT_EQ(delete_count, delete_queue_->events_processed());
}

// Test flow deletion on ACL deletion
TEST_F(FlowTest, AclDelete) {
    AddAcl("acl1", 1, "vn5" , "vn5", "pass");
    client->WaitForIdle();

    uint32_t sport = 30;
    for (uint32_t i = 0; i < 1; i++) {
        sport++;
        TestFlow flow[] = {
            {
                TestFlowPkt(Address::INET, vm2_ip, vm1_ip, IPPROTO_TCP, sport,
                            40, "vrf5", flow1->id(), 1),
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
    const NextHop *nh = fe->data().rpf_nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().rpf_nh.get() != NULL);
    nh = fe->data().rpf_nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().rpf_nh.get() != NULL);
    nh = fe->data().rpf_nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().rpf_nh.get() != NULL);
    nh = fe->data().rpf_nh.get();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    tnh = static_cast<const TunnelNH *>(nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_UDP);

    DelEncapList();
    client->WaitForIdle();
    fe = FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm1_ip, vm1_ip, 1, 0, 0,
                 GetFlowKeyNH(input[0].intf_id));
    EXPECT_TRUE(fe->data().rpf_nh.get() != NULL);
    nh = fe->data().rpf_nh.get();
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

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = 
        TestInit(init_file, ksync_init, true, true, true, (1000000 * 60 * 10),
                 (3 * 60 * 1000));
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
