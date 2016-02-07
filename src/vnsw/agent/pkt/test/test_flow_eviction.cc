/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <netinet/in.h>
#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_mgmt.h"
#include <algorithm>

#define vm1_ip "1.1.1.1"
#define vm2_ip "1.1.1.2"
#define remote_vm1_ip "1.1.1.3"
#define remote_compute "100.100.100.100"

struct PortInfo input[] = {
        {"vif0", 1, vm1_ip, "00:00:00:01:01:01", 1, 1},
        {"vif1", 2, vm2_ip, "00:00:00:01:01:02", 1, 2},
};

class FlowEvictionTest : public ::testing::Test {
public:
    FlowEvictionTest() : peer_(NULL), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        flow_mgmt_ = agent_->pkt()->flow_mgmt_manager();
        eth = EthInterfaceGet("vnet0");
        EXPECT_TRUE(eth != NULL);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
    }

protected:
    virtual void SetUp() {
        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
        client->Reset();

        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));

        vif0 = VmInterfaceGet(input[0].intf_id);
        assert(vif0);
        vif1 = VmInterfaceGet(input[1].intf_id);
        assert(vif1);
        vrf_name_ = vif0->vrf()->GetName();
        vn_name_ = vif0->vn()->GetName();

        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        client->WaitForIdle();

        boost::system::error_code ec;
        Ip4Address remote_ip = Ip4Address::from_string(remote_vm1_ip, ec);
        Ip4Address remote_compute_ip = Ip4Address::from_string(remote_compute,
                                                               ec);

        Inet4TunnelRouteAdd(peer_, vrf_name_, remote_ip, 32,
                            remote_compute_ip,
                            TunnelType::AllType(), 10, vn_name_,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        strcpy(router_id_, agent_->router_id().to_string().c_str());
        inet4_table_ = vif0->vrf()->GetInet4UnicastRouteTable();
        ksync_sock_ = dynamic_cast<KSyncSockTypeMap *>(KSyncSock::Get(0));
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteRoute(vrf_name_.c_str(), remote_vm1_ip, 32, peer_);
        client->WaitForIdle(3);

        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);

        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        EXPECT_EQ(0U, agent_->vm_table()->Size());
        EXPECT_EQ(0U, agent_->vn_table()->Size());
        EXPECT_EQ(0U, agent_->acl_table()->Size());
        DeleteBgpPeer(peer_);
    }

protected:
    BgpPeer *peer_;
    Agent *agent_;
    FlowProto *flow_proto_;
    FlowMgmtManager *flow_mgmt_;
    VmInterface *vif0;
    VmInterface *vif1;
    PhysicalInterface *eth;
    string vrf_name_;
    string vn_name_;
    char router_id_[80];
    InetUnicastAgentRouteTable *inet4_table_;
    KSyncSockTypeMap *ksync_sock_;
};

// New flow no-eviction
TEST_F(FlowEvictionTest, FlowNoEviction) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(0, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// New flow, evicts a flow created earlier
TEST_F(FlowEvictionTest, NewFlow_Evicted_Index_1) {
    uint32_t vrf_id = vif0->vrf_id();
    // Create a flow
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    // Generate a flow that evicts flow created above
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    // Original flow should be deleted
    EXPECT_TRUE(FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                        vif0->flow_key_nh()->id()) == false);

    // The reverse flow should not be deleted. Should be marked short flow
    FlowEntry *rflow = FlowGet(vrf_id, vm1_ip, remote_vm1_ip, 2, 0, 0,
                               vif0->flow_key_nh()->id());
    EXPECT_TRUE(rflow != NULL);
    if (rflow) {
        EXPECT_TRUE(rflow->IsShortFlow());
        EXPECT_TRUE(rflow->reverse_flow_entry() == NULL);
    }

    // New flow should be present
    flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                   vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(0, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. Flow created again with same index
TEST_F(FlowEvictionTest, Evict_RecreateFlow_1) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. Flow created again with new index
TEST_F(FlowEvictionTest, Evict_RecreateFlow_2) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. Flow created again with index of for another evicted flow
TEST_F(FlowEvictionTest, Evict_RecreateFlow_3) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 2);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow1 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow1 != NULL);
    EXPECT_EQ(1, flow1->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow1->ksync_index_entry()->index());
    EXPECT_EQ(2, flow1->flow_handle());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow1->ksync_index_entry()->state());

    FlowEntry *flow2 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow2 == NULL);
}

// Flow evicted. Flow created again with same index before first add is
// completed
TEST_F(FlowEvictionTest, Evict_Recreate_Before_Write_1) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. Flow created again with new index before first add is
// completed
TEST_F(FlowEvictionTest, Evict_Recreate_Before_Write_2) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. Flow created again with index of another evicted flow (flow2)
// Write of flow2 is not yet complete
TEST_F(FlowEvictionTest, Evict_Recreate_Before_Write_3) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 2);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow1 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow1 != NULL);

    FlowEntry *flow2 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow2 == NULL);
    EXPECT_EQ(1, flow1->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow1->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow1->ksync_index_entry()->state());
}

// Flow evicted. New flow added with same index before DEL_ACK for original
// flow is got
TEST_F(FlowEvictionTest, Evict_Add_Before_DelAck_1) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    ksync_sock_->DisableReceiveQueue(true);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    ksync_sock_->DisableReceiveQueue(false);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. New flow added with new index before DEL_ACK for original
// flow is got
TEST_F(FlowEvictionTest, Evict_Add_Before_DelAck_2) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    ksync_sock_->DisableReceiveQueue(true);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    client->WaitForIdle();

    ksync_sock_->DisableReceiveQueue(false);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(1, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Flow evicted. New flow added with new index of another evicted flow
// before DEL_ACK for second flow is got
TEST_F(FlowEvictionTest, Evict_Add_Before_DelAck_3) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 2);
    ksync_sock_->DisableReceiveQueue(true);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    ksync_sock_->DisableReceiveQueue(false);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow1 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow1 != NULL);

    FlowEntry *flow2 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow2 == NULL);
    EXPECT_EQ(1, flow1->ksync_index_entry()->evict_count());
    EXPECT_EQ(2, flow1->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow1->ksync_index_entry()->state());
}

// 2 flows evicted and index for them exchanged
TEST_F(FlowEvictionTest, Evict_Cyclic_Reuse_1) {
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 2);
    client->WaitForIdle();

    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 2);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow1 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow1 != NULL);
    EXPECT_EQ(2, flow1->flow_handle());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow1->ksync_index_entry()->state());

    FlowEntry *flow2 = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow2 != NULL);
    EXPECT_EQ(1, flow2->flow_handle());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow2->ksync_index_entry()->state());
}

// Flow evict on reverse-flow
TEST_F(FlowEvictionTest, Delete_Evicted_Flow_1) {
    uint32_t vrf_id = vif0->vrf_id();
    // Create a flow
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    FlowKey key = flow->key();

    // Generate a flow that evicts flow created above
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    // Original flow should be deleted
    EXPECT_TRUE(FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                        vif0->flow_key_nh()->id()) == false);

    flow_proto_->DeleteFlowRequest(key, true);
    client->WaitForIdle();

    // New flow should be present
    flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                   vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(0, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

TEST_F(FlowEvictionTest, Delete_Evicted_Flow_2) {
    uint32_t vrf_id = vif0->vrf_id();
    // Create a flow
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 2, 1);
    client->WaitForIdle();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    // Generate delete request followed by flow-evict
    flow_proto_->DeleteFlowRequest(flow->key(), true);
    // Generate a flow that evicts flow created above
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    // Original flow should be deleted
    EXPECT_TRUE(FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 2, 0, 0,
                        vif0->flow_key_nh()->id()) == false);

    // New flow should be present
    flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                   vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ(0, flow->ksync_index_entry()->evict_count());
    EXPECT_EQ(1, flow->ksync_index_entry()->index());
    EXPECT_EQ(KSyncFlowIndexEntry::INDEX_SET,
              flow->ksync_index_entry()->state());
}

// Delete a flow that is not yet assigned index
TEST_F(FlowEvictionTest, Delete_Index_Unassigned_Flow_1) {
    ksync_sock_->DisableReceiveQueue(true);
    TxIpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                   remote_vm1_ip, vm1_ip, 1, 1);
    client->WaitForIdle();

    uint32_t vrf_id = vif0->vrf_id();
    FlowEntry *flow = FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                              vif0->flow_key_nh()->id());
    EXPECT_TRUE(flow != NULL);

    FlowKey key = flow->key();
    flow_proto_->DeleteFlowRequest(key, true);
    client->WaitForIdle();

    ksync_sock_->DisableReceiveQueue(false);
    client->WaitForIdle();

    EXPECT_TRUE(FlowGet(vrf_id, remote_vm1_ip, vm1_ip, 1, 0, 0,
                        vif0->flow_key_nh()->id()) == NULL);
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
