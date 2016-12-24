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

class FlowAuditTest : public ::testing::Test {
public:
    FlowAuditTest() : agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        flow_stats_collector_ = agent_->flow_stats_manager()->
            default_flow_stats_collector_obj();
    }

    virtual void SetUp() {
        EXPECT_EQ(0U, get_flow_proto()->FlowCount());
        client->Reset();

        CreateVmportEnv(input, 1, 1);
        client->WaitForIdle();

        vmi0 = VmInterfaceGet(input[0].intf_id);
        assert(vmi0);
        FlowStatsTimerStartStop(agent_, true);
        KFlowPurgeHold();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 1, true, 1);
        client->WaitForIdle();
        FlowStatsTimerStartStop(agent_, false);
        KFlowPurgeHold();
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

        uint64_t supper;
        uint64_t slower;
        uint64_t dupper;
        uint64_t dlower;

        IpToU64(saddr, daddr, &supper, &slower, &dupper, &dlower);
        req.set_fr_flow_sip_l(slower);
        req.set_fr_flow_sip_u(supper);
        req.set_fr_flow_dip_l(dlower);
        req.set_fr_flow_dip_u(dupper);

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
    FlowStatsCollectorObject* flow_stats_collector_;
};

// Validate flows audit
TEST_F(FlowAuditTest, FlowAudit_1) {
    // Create two hold-flows
    EXPECT_TRUE(KFlowHoldAdd(1, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    EXPECT_TRUE(KFlowHoldAdd(2, 1, "2.2.2.2", "3.3.3.3", 1, 0, 0, 0));
    RunFlowAudit();
    EXPECT_TRUE(FlowTableWait(2));

    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0);
    EXPECT_TRUE(fe != NULL && fe->is_flags_set(FlowEntry::ShortFlow) == true &&
                fe->short_flow_reason() == FlowEntry::SHORT_AUDIT_ENTRY);

    // Wait till flow-stats-collector sees the flows
    WAIT_FOR(1000, 1000, (flow_stats_collector_->Size() == 2));

    // Enqueue aging and validate flows are deleted
    client->EnqueueFlowAge();
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (get_flow_proto()->FlowCount() == 0U));
}

// Validate flow do not get deleted in following case,
// - Flow-audit runs and enqueues request to delete
// - Add flow before audit message is run
// - Flow-audit message should be ignored
TEST_F(FlowAuditTest, FlowAudit_2) {

    // Create the flow first
    string vrf_name = agent_->vrf_table()->FindVrfFromId(1)->GetName();
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

    uint32_t nh_id = vmi0->flow_key_nh()->id();
    // Validate that flow-drop-reason is not AUDIT
    FlowEntry *fe = FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, nh_id);
    EXPECT_TRUE(fe != NULL &&
                fe->short_flow_reason() != FlowEntry::SHORT_AUDIT_ENTRY);

    // Wait till flow-stats-collector sees the flows
    WAIT_FOR(1000, 1000, (flow_stats_collector_->Size() == 2));

    // Enqueue Audit message
    EXPECT_TRUE(KFlowHoldAdd(nh_id, 1, "1.1.1.1", "2.2.2.2", 1, 0, 0, 0));
    RunFlowAudit();
    client->WaitForIdle();

    // Validate that flow-drop-reason is not AUDIT
    fe = FlowGet(1, "1.1.1.1", "2.2.2.2", 1, 0, 0, nh_id);
    EXPECT_TRUE(fe != NULL &&
                fe->short_flow_reason() != FlowEntry::SHORT_AUDIT_ENTRY);
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
