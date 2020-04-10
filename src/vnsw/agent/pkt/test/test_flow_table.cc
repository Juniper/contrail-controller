/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <base/task.h>
#include <base/test/task_test_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "uve/test/test_uve_util.h"

#define vm1_ip "1.1.1.1"
#define vm2_ip "1.1.1.2"
#define remote_vm1_ip "1.1.1.3"
#define remote_compute "100.100.100.100"

struct PortInfo input[] = {
    {"vif0", 1, vm1_ip, "00:00:00:01:01:01", 1, 1},
    {"vif1", 2, vm2_ip, "00:00:00:01:01:02", 1, 2},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class TestFlowTable : public ::testing::Test {
public:
    TestFlowTable() : agent_(Agent::GetInstance()), peer_(NULL), util_() {
        eth = EthInterfaceGet("vnet0");
        EXPECT_TRUE(eth != NULL);
    }
    virtual void SetUp() {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        client->WaitForIdle();
        for (int i = 0; i < 4; i++) {
            flow_count_[i] = 0;
        }

        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
        client->Reset();

        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
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
                            SecurityGroupList(), TagList(), PathPreference());
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        strcpy(router_id_, agent_->router_id().to_string().c_str());
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteRoute(vrf_name_.c_str(), remote_vm1_ip, 32, peer_);
        client->WaitForIdle(3);

        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);
        DelIPAM("vn1");
        client->WaitForIdle();

        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        EXPECT_EQ(0U, agent_->vm_table()->Size());
        EXPECT_EQ(0U, agent_->vn_table()->Size());
        EXPECT_EQ(0U, agent_->acl_table()->Size());
        DeleteBgpPeer(peer_);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));
    }

    void FlowHash(const char *sip, const char *dip, uint8_t proto,
                  uint16_t sport, uint16_t dport, uint32_t flow_handle) {
        uint32_t idx = flow_proto_->FlowTableIndex
            (Ip4Address::from_string(sip), Ip4Address::from_string(dip), proto,
             sport, dport, flow_handle);
        flow_count_[idx]++;
    }

    void FlowHash(uint32_t sip, uint32_t dip, uint8_t proto, uint16_t sport,
                  uint16_t dport, uint32_t flow_handle) {
        uint32_t idx = flow_proto_->FlowTableIndex
            (Ip4Address(sip), Ip4Address(dip), proto, sport, dport,
             flow_handle);
        flow_count_[idx]++;
    }

protected:
    Agent *agent_;
    FlowProto *flow_proto_;
    uint32_t flow_count_[4];
    BgpPeer *peer_;
    VmInterface *vif0;
    VmInterface *vif1;
    PhysicalInterface *eth;
    string vrf_name_;
    string vn_name_;
    char router_id_[80];
    TestUveUtil util_;
};

TEST_F(TestFlowTable, TestParam_1) {
    EXPECT_EQ(flow_proto_->flow_table_count(), 4U);
}

TEST_F(TestFlowTable, flow_hash_sport_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash("1.1.1.1", "1.1.1.2", 6, i, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_dport_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash("1.1.1.1", "1.1.1.2", 6, 1, i, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_sip_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101+i, 0x1010102, 6, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_dip_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101, 0x1010101+i,  6, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_proto_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101, 0x1010101,  i, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, AgeOutVrouterEvictedFlow) {
    TxTcpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                    remote_vm1_ip, vm1_ip, 1000, 200, 1, 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                              200, vif0->flow_key_nh()->id(), 1);
    EXPECT_TRUE(flow != NULL);
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow != NULL);

    uint32_t flow_handle = flow->flow_handle();
    EXPECT_TRUE(flow_handle != FlowEntry::kInvalidFlowHandle);
    uint32_t rflow_handle = rflow->flow_handle();
    EXPECT_TRUE(rflow_handle != FlowEntry::kInvalidFlowHandle);
    KSyncSock *sock = KSyncSock::Get(0);

    //Fetch the delete_count_ for delete enqueues
    uint32_t evict_count = agent_->GetFlowProto()->flow_stats()->evict_count_;
    auto tx_count = sock->tx_count();

    //Invoke FlowStatsCollector to check whether flow gets evicted
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify that evict count has not been modified
    EXPECT_EQ(evict_count, agent_->GetFlowProto()->flow_stats()->
              evict_count_);

    //Set Evicted flag for the flow
    KSyncSockTypeMap::SetEvictedFlag(flow_handle);
    KSyncSockTypeMap::SetEvictedFlag(rflow_handle);

    //Invoke FlowStatsCollector to enqueue delete for evicted flow.
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify that evict count has been increased by 1
    EXPECT_EQ((evict_count + 2), agent_->GetFlowProto()->flow_stats()->
              evict_count_);

    //Invoke FlowStatsCollector to enqueue delete for reverse flow which is
    //marked as short flow
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify that evict count is still same
    EXPECT_EQ((evict_count + 2), agent_->GetFlowProto()->flow_stats()->
              evict_count_);

    //Verify that flows have been removed
    WAIT_FOR(100, 100, (flow_proto_->FlowCount() == 0));

    //Verify that tx_count is unchanged after deletion of evicted flows
    //because agent does not sent any message to vrouter for evicted flows
    EXPECT_EQ(tx_count, sock->tx_count());

    //Reset the Evicted flag set by this test-case
    KSyncSockTypeMap::ResetEvictedFlag(flow_handle);
    KSyncSockTypeMap::ResetEvictedFlag(rflow_handle);
}

TEST_F(TestFlowTable, EvictPktTrapBeforeReverseFlowResp) {
    KSyncSockTypeMap *sock = static_cast<KSyncSockTypeMap *>(KSyncSock::Get(0));
    sock->set_is_incremental_index(true);
    for (uint32_t i = 0; i < flow_proto_->flow_table_count(); i++) {
        flow_proto_->DisableFlowKSyncQueue(i, true);
    }
    TxTcpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                    remote_vm1_ip, vm1_ip, 1000, 200, 1, 1);

    FlowEntry *flow = NULL;

    WAIT_FOR(1000, 1000, ((flow = FlowGet(remote_vm1_ip, vm1_ip, IPPROTO_TCP,
                          1000, 200, vif0->flow_key_nh()->id(), 1)) != NULL));
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow != NULL);

    uint32_t flow_handle = flow->flow_handle();
    EXPECT_TRUE(flow_handle != FlowEntry::kInvalidFlowHandle);
    uint32_t rflow_handle = rflow->flow_handle();
    EXPECT_TRUE(rflow_handle == FlowEntry::kInvalidFlowHandle);

    //client->EnqueueFlowFlush();
    //WAIT_FOR(1000, 1000, (flow->deleted()));
    //WAIT_FOR(1000, 1000, (rflow->deleted()));

    TxTcpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                    vm1_ip, remote_vm1_ip, 200, 1000, 1, 2, 3);
    WAIT_FOR(1000, 1000, ((rflow_handle = rflow->flow_handle())
                != FlowEntry::kInvalidFlowHandle));
    //WAIT_FOR(1000, 1000, (!flow->deleted()));
    //WAIT_FOR(1000, 1000, (!rflow->deleted()));
    for (uint32_t i = 0; i < flow_proto_->flow_table_count(); i++) {
        flow_proto_->DisableFlowKSyncQueue(i, false);
    }
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (flow->flow_handle() != FlowEntry::kInvalidFlowHandle));
    sock->set_is_incremental_index(false);
    client->WaitForIdle();
}

TEST_F(TestFlowTable, ResetFlowStats) {
    TxTcpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                    remote_vm1_ip, vm1_ip, 1000, 200, 1, 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                              200, vif0->flow_key_nh()->id(), 1);
    EXPECT_TRUE(flow != NULL);
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(rflow != NULL);

    //Invoke FlowStatsCollector to check whether flow gets evicted
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify the stats of flow
    EXPECT_TRUE(FlowStatsMatch("vrf1", remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                               200, 1, 30, vif0->flow_key_nh()->id(), 1));

    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);

    //Invoke FlowStatsCollector to enqueue delete for evicted flow.
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify the stats of flow
    EXPECT_TRUE(FlowStatsMatch("vrf1", remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                               200, 2, 60, vif0->flow_key_nh()->id(), 1));

    TxTcpMplsPacket(eth->id(), remote_compute, router_id_, vif0->label(),
                    remote_vm1_ip, vm1_ip, 1000, 200, 1, 10);
    client->WaitForIdle();

    flow = FlowGet(remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                   200, vif0->flow_key_nh()->id(), 10);
    EXPECT_TRUE(flow != NULL);

    //Verify that flow-handle is updated in flow-stats module
    FlowStatsCollector *fsc = flow->fsc();
    EXPECT_TRUE(fsc != NULL);
    FlowExportInfo *info = fsc->FindFlowExportInfo(flow);
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(info->flow_handle() == 10);

    //Verify the stats of flow are reset after change of flow-handle
    EXPECT_TRUE(FlowStatsMatch("vrf1", remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                               200, 0, 0, vif0->flow_key_nh()->id(), 10));

    //Invoke FlowStatsCollector to enqueue delete for reverse flow which is
    //marked as short flow
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle();

    //Verify the stats of flow
    EXPECT_TRUE(FlowStatsMatch("vrf1", remote_vm1_ip, vm1_ip, IPPROTO_TCP, 1000,
                               200, 1, 30, vif0->flow_key_nh()->id(), 10));
}

int main(int argc, char *argv[]) {
    int ret = 0;
    GETUSERARGS();
    strcpy(init_file, "controller/src/vnsw/agent/pkt/test/flow-table.ini");
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
