/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "pkt/pkt_init.h"
#include "pkt/flowtable.h"
#include "test_cmn_util.h"
#include <uve/uve_client.h>
#include <uve/inter_vn_stats.h>
#include "ksync/ksync_sock_user.h"

 
#define MAX_VNET 2

using namespace std;

void RouterIdDepInit() {
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
VmPortInterface *flow0, *flow1;
VmPortInterface *test0, *test1;

class StatsTestMock : public ::testing::Test {
public:
    bool InterVnStatsMatch(string svn, string dvn, uint32_t pkts, 
                                  uint32_t bytes, bool out) {
        InterVnStatsCollector::VnStatsSet *stats_set = 
                AgentUve::GetInstance()->GetInterVnStatsCollector()->Find(svn);

        if (!stats_set) {
            return false;
        }
        VnStats key(dvn, 0, 0, false);
        InterVnStatsCollector::VnStatsSet::iterator it = stats_set->find(&key);
        if (it == stats_set->end()) {
            return false; 
        }
        VnStats *stats = *it;
        if (out && stats->out_bytes == bytes && stats->out_pkts == pkts) {
            return true;
        }
        if (!out && stats->in_bytes == bytes && stats->in_pkts == pkts) {
            return true;
        }
        return false;
    }

    static void TestSetup() {
        unsigned int vn_count = 0;
        hash_id = 1;
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(2);
        vn_count++;

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_EQ(5U, Agent::GetInstance()->GetInterfaceTable()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());

        flow0 = VmPortInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmPortInterfaceGet(input[1].intf_id);
        assert(flow1);

        /* verify that there are no existing Flows */
        EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());

        //Prerequisites for interface stats test
        client->Reset();
        CreateVmportEnv(stats_if, 2);
        client->WaitForIdle(2);
        vn_count++;

        EXPECT_TRUE(VmPortActive(stats_if, 0));
        EXPECT_TRUE(VmPortActive(stats_if, 1));
        EXPECT_EQ(4U, Agent::GetInstance()->GetVmTable()->Size());
        EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_EQ(4U, Agent::GetInstance()->GetIntfCfgTable()->Size());

        test0 = VmPortInterfaceGet(stats_if[0].intf_id);
        assert(test0);
        test1 = VmPortInterfaceGet(stats_if[1].intf_id);
        assert(test1);

        //To disable flow aging set the flow age time to high value
        AgentUve::GetInstance()->GetFlowStatsCollector()->SetFlowAgeTime(1000000 * 60 * 10);

        client->SetFlowFlushExclusionPolicy();
    }
};

TEST_F(StatsTestMock, FlowStatsTest) {
    hash_id = 1;
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->GetInterfaceId(), "1.1.1.1", "1.1.1.2", 0, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, false, 
                        "vn5", "vn5", hash_id++));

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxIpPacketUtil(flow1->GetInterfaceId(), "1.1.1.2", "1.1.1.1", 0, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, true, 
                          "vn5", "vn5", hash_id++));

    //Flow creation using TCP packet
    TxTcpPacketUtil(flow0->GetInterfaceId(), "1.1.1.1", "1.1.1.2",
                    1000, 200, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, false,
                        "vn5", "vn5", hash_id++));

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->GetInterfaceId(), "1.1.1.2", "1.1.1.1",
                200, 1000, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, true, 
                        "vn5", "vn5", hash_id++));

    //Verify flow count
    EXPECT_EQ(4U, FlowTable::GetFlowTableObject()->Size());

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(2);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 1, 30));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 1, 30));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 1, 30));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 1, 30));

    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(3, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(4, 1, 30);

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(2);

    //Verify the updated flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 0, 0, 0, 2, 60));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 0, 0, 0, 2, 60));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 1000, 200, 2, 60));
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 200, 1000, 2, 60));

    client->EnqueueFlowFlush();
    client->WaitForIdle(2);
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
}

TEST_F(StatsTestMock, IntfStatsTest) {
    AgentUve::GetInstance()->GetStatsCollector()->run_counter_ = 0;
    client->IfStatsTimerWait(2);

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0)); 

    //Change the stats
    KSyncSockTypeMap::IfStatsUpdate(test0->GetInterfaceId(), 1, 50, 0, 1, 20, 0);
    KSyncSockTypeMap::IfStatsUpdate(test1->GetInterfaceId(), 1, 50, 0, 1, 20, 0);

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetStatsCollector()->run_counter_ = 0;
    client->IfStatsTimerWait(2);

    //Verify the updated flow stats
    EXPECT_TRUE(VmPortStatsMatch(test0, 1, 50, 1, 20)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 1, 50, 1, 20)); 

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->GetInterfaceId(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->GetInterfaceId(), 0, 0, 0, 0, 0, 0);
}

TEST_F(StatsTestMock, InterVnStatsTest) {
    hash_id = 1;
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
    //(1) Inter-VN stats between for traffic within same VN
    //Flow creation using IP packet
    TxTcpPacketUtil(flow0->GetInterfaceId(), "1.1.1.1", "1.1.1.2",
                    30, 40, hash_id);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, false, 
                        "vn5", "vn5", hash_id++));

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(2);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.1", "1.1.1.2", 6, 30, 40, 1, 30));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 1, 30, false); //Incoming stats

    //Create flow in reverse direction and make sure it is linked to previous flow
    TxTcpPacketUtil(flow1->GetInterfaceId(), "1.1.1.2", "1.1.1.1",
                    40, 30, hash_id);
    client->WaitForIdle(2);
    client->WaitForIdle(2);
    EXPECT_TRUE(FlowGet("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, true, 
                        "vn5", "vn5", hash_id++));

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(2);

    //Verify flow stats
    EXPECT_TRUE(FlowStatsMatch("vrf5", "1.1.1.2", "1.1.1.1", 6, 40, 30, 1, 30));

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 2, 60, false); //Incoming stats

    //(2) Inter-VN stats updation when flow stats are updated
    //Change the stats
    KSyncSockTypeMap::IncrFlowStats(1, 1, 30);
    KSyncSockTypeMap::IncrFlowStats(2, 1, 30);
    client->WaitForIdle(2);

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(3);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 4, 120, false); //Incoming stats

    //(3) Inter-VN stats between known and unknown VNs
    //Flow creation using IP packet
    TxIpPacketUtil(flow0->GetInterfaceId(), "1.1.1.1", "1.1.1.3", 1, hash_id);
    client->WaitForIdle(2);

    // A short flow would be created with Source VN as "vn5" and dest-vn as "Unknown".
    // Not checking for creation of short flow because it will get removed during 
    // the next flow-age evaluation cycle

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetFlowStatsCollector()->run_counter_ = 0;
    client->FlowTimerWait(2);

    /* Make sure that the short flow is removed */
    EXPECT_EQ(2U, FlowTable::GetFlowTableObject()->Size());

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", (*FlowHandler::UnknownVn()).c_str(), 1, 30, true); //outgoing stats
    InterVnStatsMatch((*FlowHandler::UnknownVn()).c_str(), "vn5", 1, 30, false); //Incoming stats

    //clean-up. Flush flow table
    client->EnqueueFlowFlush();
    client->WaitForIdle(2);
    EXPECT_EQ(0U, FlowTable::GetFlowTableObject()->Size());
}

TEST_F(StatsTestMock, VrfStatsTest) {
    //Create 2 vrfs in agent
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);
    int vrf41_id = vrf->GetVrfId();

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);
    int vrf42_id = vrf->GetVrfId();

    //Create 2 vrfs in mock Kernel and update its stats
    KSyncSockTypeMap::VrfStatsAdd(vrf41_id);
    KSyncSockTypeMap::VrfStatsAdd(vrf42_id);
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 10, 11, 12, 13, 14, 15);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 16, 17, 18, 19, 20, 21);

    //Wait for stats to be updated in agent
    AgentUve::GetInstance()->GetStatsCollector()->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verfify the stats read from kernel
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, 10, 11, 12, 13, 14, 15));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, 16, 17, 18, 19, 20, 21));

    //Verfify the prev_* fields of vrf_stats are 0
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 0, 0, 0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 0, 0, 0, 0, 0, 0));

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
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), false, 0, 0, 0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), false, 0, 0, 0, 0, 0, 0));

    //Verify that prev_* fields of vrf_stats are updated
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 10, 11, 12, 13, 14, 15));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 16, 17, 18, 19, 20, 21));

    //Update stats in mock kernel when vrfs are absent in agent
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 20, 21, 22, 23, 24, 25);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 26, 27, 28, 29, 30, 31);

    //Wait for stats to be updated in agent
    AgentUve::GetInstance()->GetStatsCollector()->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that prev_* fields of vrf_stats are updated when vrf is absent from agent
    EXPECT_TRUE(VrfStatsMatchPrev(vrf41_id, 20, 21, 22, 23, 24, 25));
    EXPECT_TRUE(VrfStatsMatchPrev(vrf42_id, 26, 27, 28, 29, 30, 31));

    //Add 2 VRFs in agent. They will re-use the id's allocated earlier
    VrfAddReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== true));
    EXPECT_TRUE(DBTableFind("vrf41.uc.route.0"));

    vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf41");
    EXPECT_TRUE(vrf != NULL);
    int new_vrf41_id = vrf->GetVrfId();

    VrfAddReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== true));
    EXPECT_TRUE(DBTableFind("vrf42.uc.route.0"));

    vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf42");
    EXPECT_TRUE(vrf != NULL);
    int new_vrf42_id = vrf->GetVrfId();

    //Wait for stats to be updated in agent
    AgentUve::GetInstance()->GetStatsCollector()->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that vrf_stats's entry stats are set to 0
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, 0, 0, 0, 0, 0, 0));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, 0, 0, 0, 0, 0, 0));

    //Update stats in mock kernel when vrfs are absent in agent
    KSyncSockTypeMap::VrfStatsUpdate(vrf41_id, 40, 41, 42, 43, 44, 45);
    KSyncSockTypeMap::VrfStatsUpdate(vrf42_id, 46, 47, 48, 49, 50, 51);

    //Wait for stats to be updated in agent
    AgentUve::GetInstance()->GetStatsCollector()->vrf_stats_responses_ = 0;
    client->VrfStatsTimerWait(1);

    //Verify that vrf_stats_entry's stats are set to values after the vrf was added
    EXPECT_TRUE(VrfStatsMatch(vrf41_id, string("vrf41"), true, 20, 20, 20, 20, 20, 20));
    EXPECT_TRUE(VrfStatsMatch(vrf42_id, string("vrf42"), true, 20, 20, 20, 20, 20, 20));

    //Cleanup-Remove the VRFs added 
    VrfDelReq("vrf41");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf41")== false));
    EXPECT_FALSE(DBTableFind("vrf41.uc.route.0"));

    VrfDelReq("vrf42");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf42")== false));
    EXPECT_FALSE(DBTableFind("vrf42.uc.route.0"));
}

TEST_F(StatsTestMock, VnStatsTest) {
    AgentUve::GetInstance()->GetStatsCollector()->run_counter_ = 0;
    client->IfStatsTimerWait(2);

    //Verify vn stats at the start of test case
    char vn_name[20];
    sprintf(vn_name, "vn%d", stats_if[0].vn_id);

    EXPECT_TRUE(VnStatsMatch(vn_name, 0, 0, 0, 0)); 

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0)); 
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0)); 

    //Change the stats on one interface of vn
    KSyncSockTypeMap::IfStatsUpdate(test0->GetInterfaceId(), 50, 1, 0, 20, 1, 0);

    //Wait for stats to be updated
    AgentUve::GetInstance()->GetStatsCollector()->run_counter_ = 0;
    client->IfStatsTimerWait(2);

    //Verify the updated vn stats
    EXPECT_TRUE(VnStatsMatch(vn_name, 50, 1, 20, 1)); 

    if (stats_if[0].vn_id == stats_if[1].vn_id) {
        //Change the stats on the other interface of same vn
        KSyncSockTypeMap::IfStatsUpdate(test1->GetInterfaceId(), 50, 1, 0, 20, 1, 0);

        //Wait for stats to be updated
        AgentUve::GetInstance()->GetStatsCollector()->run_counter_ = 0;
        client->IfStatsTimerWait(2);

        //Verify the updated vn stats
        EXPECT_TRUE(VnStatsMatch(vn_name, 100, 2, 40, 2)); 
    }

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->GetInterfaceId(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->GetInterfaceId(), 0, 0, 0, 0, 0, 0);
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true, 2, 2);
    StatsTestMock::TestSetup();

    ret = RUN_ALL_TESTS();

    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    cout << "Return test result:" << ret << endl;
    return ret;
}

