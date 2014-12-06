/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "cmn/agent_cmn.h"
#include "pkt/pkt_init.h"
#include "pkt/flow_table.h"
#include "test_cmn_util.h"
#include <uve/agent_uve.h>
#include <uve/test/vn_uve_table_test.h>
#include "ksync/ksync_sock_user.h"
#include <uve/test/agent_stats_collector_test.h>
#include <uve/test/flow_stats_collector_test.h>
#include "uve/test/test_uve_util.h"
#include "pkt/test/test_flow_util.h"

using namespace std;
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
        {"flow0", 6, "1.1.1.1", "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, "1.1.1.2", "00:00:00:01:01:02", 5, 2},
};

struct PortInfo stats_if[] = {
        {"test0", 8, "3.1.1.1", "00:00:00:01:01:01", 6, 3},
        {"test1", 9, "4.1.1.2", "00:00:00:01:01:02", 6, 4},
};

int hash_id;
VmInterface *flow0, *flow1;
VmInterface *test0, *test1;

class StatsTestMock : public ::testing::Test {
public:
    StatsTestMock() : util_(), peer_(NULL) {}
    bool InterVnStatsMatch(const string &svn, const string &dvn, uint32_t pkts,
                           uint32_t bytes, bool out) {
        VnUveTableTest *vut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
        const VnUveEntry::VnStatsSet *stats_set = vut->FindInterVnStats(svn);

        if (!stats_set) {
            return false;
        }
        VnUveEntry::VnStatsPtr key(new VnUveEntry::VnStats(dvn, 0, 0, false));
        VnUveEntry::VnStatsSet::iterator it = stats_set->find(key);
        if (it == stats_set->end()) {
            return false;
        }
        VnUveEntry::VnStatsPtr stats_ptr(*it);
        const VnUveEntry::VnStats *stats = stats_ptr.get();
        if (out && stats->out_bytes_ == bytes && stats->out_pkts_ == pkts) {
            return true;
        }
        if (!out && stats->in_bytes_ == bytes && stats->in_pkts_ == pkts) {
            return true;
        }
        return false;
    }

    static void TestSetup() {
        unsigned int vn_count = 0;
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(10);
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_EQ(5U, Agent::GetInstance()->interface_table()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->vm_table()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->vn_table()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->interface_config_table()->Size());

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);

        /* verify that there are no existing Flows */
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

        //Prerequisites for interface stats test
        client->Reset();
        CreateVmportEnv(stats_if, 2);
        client->WaitForIdle(10);
        vn_count++;

        EXPECT_TRUE(VmPortActive(stats_if, 0));
        EXPECT_TRUE(VmPortActive(stats_if, 1));
        EXPECT_EQ(4U, Agent::GetInstance()->vm_table()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->vn_table()->Size());
        EXPECT_EQ(4U, Agent::GetInstance()->interface_config_table()->Size());

        test0 = VmInterfaceGet(stats_if[0].intf_id);
        assert(test0);
        test1 = VmInterfaceGet(stats_if[1].intf_id);
        assert(test1);

        //To disable flow aging set the flow age time to high value
        AgentUveBase *uve = Agent::GetInstance()->uve();
        AgentUve *f_uve = static_cast<AgentUve *>(uve);
        f_uve->flow_stats_collector()->UpdateFlowAgeTime(1000000 * 60 * 10);

    }
    static void TestTeardown() {
        client->Reset();
        DeleteVmportEnv(input, 2, 1, 1);
        client->WaitForIdle(10);
        client->VnDelNotifyWait(1);
        client->PortDelNotifyWait(2);
        EXPECT_TRUE(client->AclNotifyWait(1));

        client->Reset();
        DeleteVmportEnv(stats_if, 2, 1);
        client->WaitForIdle(10);
        client->VnDelNotifyWait(1);
        client->PortDelNotifyWait(2);
    }
    TestUveUtil util_;
    BgpPeer *peer_;
};

TEST_F(StatsTestMock, FlowStatsTest) {
    hash_id = 1;
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2", 0, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 0, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxIpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 0, f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, true,
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f2_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", f2_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 1, 30,
                               flow1->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 30);
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f2_rev->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 2, 60,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 2, 60,
                               flow1->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 2, 60,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 2, 60,
                               flow1->flow_key_nh()->id()));

    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(StatsTestMock, FlowStatsOverflowTest) {
    hash_id = 1;
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);


    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test1 */
    //Decrement the stats so that they become 0
    KSyncSockTypeMap::IncrFlowStats(1, -1, -30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -30);

    KSyncSockTypeMap::SetOFlowStats(1, 1, 1);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 1, 1);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x100000000ULL, 0x100000000ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x100000000ULL, 0x100000000ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test2 */
    KSyncSockTypeMap::IncrFlowStats(1, 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x100000001ULL, 0x100000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x100000001ULL, 0x100000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test3 */
    KSyncSockTypeMap::SetOFlowStats(1, 2, 3);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 4, 5);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x200000001ULL, 0x300000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x400000001ULL, 0x500000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of vrouter-Test4 */
    KSyncSockTypeMap::IncrFlowStats(1, 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    KSyncSockTypeMap::SetOFlowStats(1, 0xA, 0xB);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0xC, 0xD);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0xA00000002ULL, 0xB00000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0xC00000002ULL, 0xD00000020ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test1 */
    //Decrement flow stats
    KSyncSockTypeMap::IncrFlowStats(1, -1, -0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10A00000001ULL, 0x1000B00000010ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10C00000001ULL, 0x1000D00000010ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test2 */
    KSyncSockTypeMap::IncrFlowStats(1, 1, 0x10);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 0x10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10A00000002ULL, 0x1000B00000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10C00000002ULL, 0x1000D00000020ULL,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent-Test3 */
    KSyncSockTypeMap::SetOFlowStats(1, 0xA1, 0xB1);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0xC1, 0xD1);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x1A100000002ULL, 0x100B100000020ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x1C100000002ULL, 0x100D100000020ULL,
                               flow1->flow_key_nh()->id()));

    //cleanup
    KSyncSockTypeMap::SetOFlowStats(1, 0, 0);
    KSyncSockTypeMap::SetOFlowStats(f1_rev->flow_handle(), 0, 0);
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(StatsTestMock, FlowStatsOverflow_AgeTest) {
    hash_id = 1;
    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true,
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    /* Verify overflow counter of agent */
    //Decrement the stats so that they become 0
    KSyncSockTypeMap::IncrFlowStats(1, -1, -30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), -1, -30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                               0x10000000000ULL, 0x1000000000000ULL,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000,
                               0x10000000000ULL, 0x1000000000000ULL,
                               flow1->flow_key_nh()->id()));

    int tmp_age_time = 1000 * 1000;
    AgentUveBase *uve = Agent::GetInstance()->uve();
    AgentUve *f_uve = static_cast<AgentUve *>(uve);
    int bkp_age_time = f_uve->flow_stats_collector()->flow_age_time_intvl();

    //Set the flow age time to 1000 microsecond
    f_uve->flow_stats_collector()->UpdateFlowAgeTime(tmp_age_time);

    usleep(tmp_age_time + 10);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 0U));

    //Restore flow aging time
    f_uve->flow_stats_collector()->UpdateFlowAgeTime(bkp_age_time);
}

TEST_F(StatsTestMock, DeletedFlowStatsTest) {
    hash_id = 1;
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2", 0, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, false, 
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 0, 0, 0,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxIpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 0, f1_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, true, 
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));
    FlowEntry *f2 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 1000, 200,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f2 != NULL);
    FlowEntry *f2_rev = f2->reverse_flow_entry();
    EXPECT_TRUE(f2_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 200, 1000,
                    f2_rev->flow_handle());
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", f2_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Verify flow count
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 1, 30,
                               flow1->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Mark the flow entries as deleted
    f1->set_deleted(true);
    f1_rev->set_deleted(true);

    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 30);
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f2_rev->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats are unchanged for deleted flows
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Verfiy flow stats are updated for flows which are not marked for delete
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 2, 60,
                               flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 2, 60,
                               flow1->flow_key_nh()->id()));

    //Remove the deleted flag, so that the cleanup can happen
    f1->set_deleted(false);
    f1_rev->set_deleted(false);

    //cleanup
    client->EnqueueFlowFlush();
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(StatsTestMock, IntfStatsTest) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (u->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    client->IfStatsTimerWait(1);

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0)); 

    //Change the stats
    KSyncSockTypeMap::IfStatsUpdate(test0->id(), 1, 50, 0, 1, 20, 0);
    KSyncSockTypeMap::IfStatsUpdate(test1->id(), 1, 50, 0, 1, 20, 0);

    //Wait for stats to be updated
    collector->interface_stats_responses_ = 0;
    client->IfStatsTimerWait(1);

    //Verify the updated flow stats
    EXPECT_TRUE(VmPortStatsMatch(test0, 1, 50, 1, 20)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 1, 50, 1, 20)); 

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->id(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->id(), 0, 0, 0, 0, 0, 0);
}

TEST_F(StatsTestMock, InterVnStatsTest) {
    hash_id = 1;
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    //(1) Inter-VN stats between for traffic within same VN
    //Flow creation using IP packet
    TxTcpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.2",
                    30, 40, hash_id);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, false, 
                        "vn5", "vn5", hash_id++, flow0->flow_key_nh()->id()));

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, 1, 30,
                               flow0->flow_key_nh()->id()));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, false); //Incoming stats

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf5");
    EXPECT_TRUE(vrf != NULL);
    FlowEntry *f1 = FlowGet(vrf->vrf_id(), "1.1.1.1", "1.1.1.2", 6, 30, 40,
                            flow0->flow_key_nh()->id());
    EXPECT_TRUE(f1 != NULL);
    FlowEntry *f1_rev = f1->reverse_flow_entry();
    EXPECT_TRUE(f1_rev != NULL);

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->id(), "1.1.1.2", "1.1.1.1", 40, 30,
                    f1_rev->flow_handle());
    client->WaitForIdle(10);
    client->WaitForIdle(10);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, true, 
                        "vn5", "vn5", f1_rev->flow_handle(),
                        flow1->flow_key_nh()->id(),
                        flow0->flow_key_nh()->id()));

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, 1, 30,
                               flow1->flow_key_nh()->id()));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, false); //Incoming stats

    //(2) Inter-VN stats updation when flow stats are updated
    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(f1_rev->flow_handle(), 1, 30);
    client->WaitForIdle(10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, false); //Incoming stats

    //(3) Inter-VN stats between known and unknown VNs
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->id(), "1.1.1.1", "1.1.1.3", 1, hash_id);
    client->WaitForIdle(10);

    // A short flow would be created with Source VN as "vn5" and dest-vn as "Unknown".
    // Not checking for creation of short flow because it will get removed during 
    // the next flow-age evaluation cycle

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    /* Make sure that the short flow is removed */
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 2U));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", (FlowHandler::UnknownVn()).c_str(), 1, 30, true); //outgoing stats
    InterVnStatsMatch((FlowHandler::UnknownVn()).c_str(), "vn5", 1, 30, false); //Incoming stats

    //clean-up. Flush flow table
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    WAIT_FOR(100, 10000, (Agent::GetInstance()->pkt()->flow_table()->Size() == 0U));
}

TEST_F(StatsTestMock, VrfStatsTest) {
    //Create 2 vrfs in agent
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);
    int vrf41_id = vrf->vrf_id();

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);
    int vrf42_id = vrf->vrf_id();

    //Create 2 vrfs in mock Kernel and update its stats
    KSyncSockTypeMap::VrfStatsAdd(vrf41_id);
    KSyncSockTypeMap::VrfStatsAdd(vrf42_id);
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 10, 11, 12, 13, 14, 15, 16, 17,
                                     18, 19, 20, 21, 22);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 23, 24, 25, 26, 27, 28, 29, 30,
                                     31, 32, 33, 34, 35);

    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (u->agent_stats_collector());
    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verfify the stats read from kernel
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, 10, 11, 12, 13, 
                              14, 15, 16, 17, 18, 19, 20, 21, 22));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, 23, 24, 25, 26,
                              27, 28, 29, 30, 31, 32, 33, 34, 35));

    //Verfify the prev_* fields of vrf_stats are 0
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
                                  0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
                                  0, 0, 0, 0));

    //Delete both the VRFs from agent
    VrfDelReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== false));
    EXPECT_FALSE(DBTableFind("vrf41.uc.route.0"));

    VrfDelReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== false));
    EXPECT_FALSE(DBTableFind("vrf42.uc.route.0"));

    //Verify that vrf_stats's entry is still present in agent_stats_collector.
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), false,
                              0, 0, 0, 0, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), false,
                              0, 0, 0, 0, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0));

    //Verify that prev_* fields of vrf_stats are updated
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 10, 11, 12, 13, 
                              14, 15, 16, 17, 18, 19, 20, 21, 22));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 23, 24, 25, 26,
                              27, 28, 29, 30, 31, 32, 33, 34, 35));

    //Update stats in mock kernel when vrfs are absent in agent
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 36, 37, 38, 39, 40, 41, 42, 43, 
                                     44, 45, 46, 47, 48);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 49, 50, 51, 52, 53, 54, 55, 56,
                                     57, 58, 59, 60, 61);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that prev_* fields of vrf_stats are updated when vrf is absent from agent
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 36, 37, 38, 39, 40, 41, 42, 43,
                                  44, 45, 46, 47, 48));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 49, 50, 51, 52, 53, 54, 55, 56,
                                  57, 58, 59, 60, 61));

    //Add 2 VRFs in agent. They will re-use the id's allocated earlier
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that vrf_stats's entry stats are set to 0
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true,
                              0, 0, 0, 0, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true,
                              0, 0, 0, 0, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0));

    //Update stats in mock kernel when vrfs are absent in agent
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 62, 63, 64, 65, 66, 67, 68, 69, 
                                     70, 71, 72, 73, 74);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 75, 76, 77, 78, 79, 80, 81, 82,
                                     83, 84, 85, 86, 87);

    //Wait for stats to be updated in agent
    collector->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that vrf_stats_entry's stats are set to values after the vrf was added
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, 26, 26, 26, 26, 
                              26, 26, 26, 26, 26, 26, 26, 26, 26));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, 26, 26, 26, 26, 
                              26, 26, 26, 26, 26, 26, 26, 26, 26));

    //Cleanup-Remove the VRFs added 
    VrfDelReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== false));
    EXPECT_FALSE(DBTableFind("vrf41.uc.route.0"));

    VrfDelReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== false));
    EXPECT_FALSE(DBTableFind("vrf42.uc.route.0"));

    //Remove vrf stats object from agent
    collector->Test_DeleteVrfStatsEntry(vrf41_id);
    collector->Test_DeleteVrfStatsEntry(vrf42_id);

    //Remove vrf stats object from mock Kernel
    KSyncSockTypeMap::VrfStatsDelete(vrf41_id);
    KSyncSockTypeMap::VrfStatsDelete(vrf42_id);

}

TEST_F(StatsTestMock, VnStatsTest) {
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (u->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    client->IfStatsTimerWait(1);

    //Verify vn stats at the start of test case
    char vn_name[20];
    sprintf(vn_name, "vn%d", stats_if[0].vn_id);

    EXPECT_TRUE(VnStatsMatch(vn_name, 0, 0, 0, 0)); 

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0)); 

    //Change the stats on one interface of vn
    KSyncSockTypeMap::IfStatsUpdate(test0->id(), 50, 1, 0, 20, 1, 0);

    //Wait for stats to be updated
    collector->interface_stats_responses_ = 0;
    client->IfStatsTimerWait(1);

    //Verify the updated vn stats
    EXPECT_TRUE(VnStatsMatch(vn_name, 50, 1, 20, 1)); 

    if (stats_if[0].vn_id == stats_if[1].vn_id) {
        //Change the stats on the other interface of same vn
        KSyncSockTypeMap::IfStatsUpdate(test1->id(), 50, 1, 0, 20, 1, 0);

        //Wait for stats to be updated
        collector->interface_stats_responses_ = 0;
        client->IfStatsTimerWait(1);

        //Verify the updated vn stats
        EXPECT_TRUE(VnStatsMatch(vn_name, 100, 2, 40, 2)); 
    }

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->id(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->id(), 0, 0, 0, 0, 0, 0);
}

//Flow parameters (vrouter, peer_vrouter and tunnel_type) verification for
//local flow
TEST_F(StatsTestMock, Underlay_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf5",
                        flow0->id()),
            {
                new VerifyVn("vn5", "vn5"),
            }
        }
    };

    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_STREQ(fe->peer_vrouter().c_str(),
                 Agent::GetInstance()->router_id().to_string().c_str());
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::INVALID);
    EXPECT_EQ(fe->underlay_source_port(), 0);

    EXPECT_STREQ(rfe->peer_vrouter().c_str(),
                 Agent::GetInstance()->router_id().to_string().c_str());
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::INVALID);
    EXPECT_EQ(rfe->underlay_source_port(), 0);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
}

//Flow parameters (vrouter, peer_vrouter and tunnel_type) verification for
//non-local flow
TEST_F(StatsTestMock, Underlay_2) {
    /* Add remote VN route to vrf5 */
    util_.CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3",
                            peer_);

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, "1.1.1.1", 1, 0, 0, "vrf5",
                        remote_router_ip, flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rfe = fe->reverse_flow_entry();

    EXPECT_STREQ(fe->peer_vrouter().c_str(), remote_router_ip);
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(fe->underlay_source_port(), 0);

    EXPECT_STREQ(rfe->peer_vrouter().c_str(), remote_router_ip);
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(rfe->underlay_source_port(), 0);

    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();
}

//Flow underlay source port verification for non-local flow
TEST_F(StatsTestMock, Underlay_3) {
    /* Add remote VN route to vrf5 */
    util_.CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3",
                            peer_);

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, "1.1.1.1", 1, 0, 0, "vrf5",
                        remote_router_ip, flow0->label()),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };
    CreateFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const FlowEntry *rfe = fe->reverse_flow_entry();

    //Change the underlay source port
    KSyncSockTypeMap::SetUnderlaySourcePort(fe->flow_handle(), 1234);
    KSyncSockTypeMap::SetUnderlaySourcePort(rfe->flow_handle(), 5678);

    //Increment flow stats to ensure that when flow_stats_collector is
    //invoked, the dispatch of flow log message happens
    KSyncSockTypeMap::IncrFlowStats(fe->flow_handle(), 1, 30);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify underlay source port for forward flow
    EXPECT_EQ(fe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(fe->underlay_source_port(), 1234);

    //Verify underlay source port for reverse flow
    EXPECT_EQ(rfe->tunnel_type().GetType(), TunnelType::MPLS_GRE);
    EXPECT_EQ(rfe->underlay_source_port(), 5678);

    //Since encap type is MPLS_GRE verify that exported flow has
    //0 as underlay source port
    AgentUveBase *uve = Agent::GetInstance()->uve();
    AgentUve *f_uve = static_cast<AgentUve *>(uve);
    FlowStatsCollectorTest *f = static_cast<FlowStatsCollectorTest *>
        (f_uve->flow_stats_collector());
    FlowDataIpv4 flow_log = f->last_sent_flow_log();
    EXPECT_EQ(flow_log.get_underlay_source_port(), 0);

    //cleanup
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true, 2, 2);
    StatsTestMock::TestSetup();

    ret = RUN_ALL_TESTS();
    client->WaitForIdle(3);
    StatsTestMock::TestTeardown();
    client->WaitForIdle(3);
    TestShutdown();
    delete client;
    return ret;
}

