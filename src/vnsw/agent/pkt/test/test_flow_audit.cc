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

#define vm1_ip "11.1.1.1"
struct PortInfo input[] = {
        {"vmi0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
};
VmInterface *vmi0;

static bool FlowStatsTimerStartStopTrigger (bool stop) {
    Agent::GetInstance()->flow_stats_manager()->\
        default_flow_stats_collector()->TestStartStopTimer(stop);
    return true;
}

static void FlowStatsTimerStartStop (bool stop) {
    int task_id = TaskScheduler::GetInstance()->GetTaskId(kTaskFlowEvent);
    std::auto_ptr<TaskTrigger> trigger_
        (new TaskTrigger(boost::bind(FlowStatsTimerStartStopTrigger, stop),
                         task_id, 0));
    trigger_->Set();
    client->WaitForIdle();
}

class FlowAuditTest : public ::testing::Test {
public:
    FlowAuditTest() : agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        flow_stats_collector_ = agent_->flow_stats_manager()->
            default_flow_stats_collector();
    }

    virtual void SetUp() {
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        client->Reset();

        CreateVmportEnv(input, 1, 1);
        client->WaitForIdle();

        vmi0 = VmInterfaceGet(input[0].intf_id);
        assert(vmi0);
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 1, true, 1);
        client->WaitForIdle();
    }


    bool FlowTableWait(size_t count) {
        int i = 1000;
        while (i > 0) {
            i--;
            if (get_flow_proto()->FlowCount() == count) {
                break;
            }
            client->WaitForIdle();
            usleep(1);
        }
        return (get_flow_proto()->FlowCount() == count);
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
    }

    void RunFlowAudit() {
        KSyncFlowMemory *flow_memory = agent_->ksync()->ksync_flow_memory();
        flow_memory->AuditProcess();
        // audit timeout set to 10 in case of test code.
        // Sleep for audit duration
        usleep(flow_memory->audit_timeout() * 2);
        flow_memory->AuditProcess();
    }

    bool KFlowHoldAdd(uint32_t hash_id, int vrf, const char *sip,
                      const char *dip, int proto, int sport, int dport,
                      int nh_id) {
        KSyncFlowMemory *flow_memory = agent_->ksync()->ksync_flow_memory();
        if (hash_id >= flow_memory->flow_table_entries_count()) {
            return false;
        }

        vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(hash_id);

        vr_flow_req req;
        req.set_fr_index(hash_id);
        IpAddress saddr = IpAddress::from_string(sip);
        IpAddress daddr = IpAddress::from_string(dip);
        req.set_fr_flow_ip(IpToVector(saddr, daddr, Address::INET));
        req.set_fr_flow_proto(proto);
        req.set_fr_family(AF_INET);
        req.set_fr_flow_sport(htons(sport));
        req.set_fr_flow_dport(htons(dport));
        req.set_fr_flow_vrf(vrf);
        req.set_fr_flow_nh_id(nh_id);

        vr_flow->fe_action = VR_FLOW_ACTION_HOLD;
        KSyncSockTypeMap::SetFlowEntry(&req, true);

        return true;
    }

    void KFlowPurgeHold() {
        KSyncFlowMemory *flow_memory = agent_->ksync()->ksync_flow_memory();
        for (size_t count = 0;
             count < flow_memory->flow_table_entries_count();
             count++) {
            vr_flow_entry *vr_flow = KSyncSockTypeMap::GetFlowEntry(count);
            vr_flow->fe_action = VR_FLOW_ACTION_DROP;
            vr_flow_req req;
            req.set_fr_index(0);
            KSyncSockTypeMap::SetFlowEntry(&req, false);
        }

        return;
    }

    FlowProto *get_flow_proto() const { return flow_proto_; }
    Agent *agent() {return agent_;}

public:
    Agent *agent_;
    FlowProto *flow_proto_;
    FlowStatsCollector* flow_stats_collector_;
};

TEST_F(FlowAuditTest, FlowAudit) {
    KFlowPurgeHold();
    FlowStatsTimerStartStop(true);
    EXPECT_TRUE(KFlowHoldAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    EXPECT_TRUE(KFlowHoldAdd(2, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, 0));
    RunFlowAudit();
    EXPECT_TRUE(FlowTableWait(2));
    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_AUDIT_ENTRY);
    //FlowStatsTimerStartStop(false);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
    KFlowPurgeHold();

    string vrf_name =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(1)->GetName();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "2.2.2.2", 1, 0, 0, vrf_name,
                        vmi0->id(), 1),
            {
            }
        }
    };

    CreateFlow(flow, 1);

    EXPECT_TRUE(FlowTableWait(2));
    EXPECT_TRUE(KFlowHoldAdd(10, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    RunFlowAudit();
    client->EnqueueFlowAge();
    client->WaitForIdle();
    usleep(500);
    int tmp_age_time = 10 * 1000;
    int bkp_age_time = flow_stats_collector_->flow_age_time_intvl();
    //Set the flow age time to 10 microsecond
    flow_stats_collector_->UpdateFlowAgeTime(tmp_age_time);
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
    flow_stats_collector_->UpdateFlowAgeTime(bkp_age_time);
    KFlowPurgeHold();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
