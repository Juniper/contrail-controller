/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap_agent_parser.h>
#include <ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <uve/vrouter_stats_collector.h>
#include <uve/vrouter_uve_entry_test.h>
#include "uve/test/test_uve_util.h"
#include "ksync/ksync_sock_user.h"

using namespace std;

void RouterIdDepInit() {
}

class VRouterStatsCollectorTask : public Task {
public:
    VRouterStatsCollectorTask(int count) : 
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::Uve")), 0), 
        count_(count) {
    }
    virtual bool Run() {
        for (int i = 0; i < count_; i++)
            Agent::GetInstance()->uve()->vrouter_stats_collector()->Run();
        return true;
    }
private:
    int count_;
};

class UveVrouterUveTest : public ::testing::Test {
public:
    void EnqueueVRouterStatsCollectorTask(int count) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        VRouterStatsCollectorTask *task = new VRouterStatsCollectorTask(count);
        scheduler->Enqueue(task);
    }
    bool DropStatsEqual(const AgentDropStats &d1, const vr_drop_stats_req &d2) const {
        if (d1.get_ds_discard() != d2.get_vds_discard()) {
            return false;
        }
        if (d1.get_ds_pull() != d2.get_vds_pull()) {
            return false;
        }
        if (d1.get_ds_invalid_if() != d2.get_vds_invalid_if()) {
            return false;
        }
        if (d1.get_ds_arp_not_me() != d2.get_vds_arp_not_me()) {
            return false;
        }
        if (d1.get_ds_garp_from_vm() != d2.get_vds_garp_from_vm()) {
            return false;
        }
        if (d1.get_ds_invalid_arp() != d2.get_vds_invalid_arp()) {
            return false;
        }
        if (d1.get_ds_trap_no_if() != d2.get_vds_trap_no_if()) {
            return false;
        }
        if (d1.get_ds_nowhere_to_go() != d2.get_vds_nowhere_to_go()) {
            return false;
        }
        if (d1.get_ds_flow_queue_limit_exceeded() != d2.get_vds_flow_queue_limit_exceeded()) {
            return false;
        }
        if (d1.get_ds_flow_no_memory() != d2.get_vds_flow_no_memory()) {
            return false;
        }
        if (d1.get_ds_flow_invalid_protocol() != d2.get_vds_flow_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_flow_nat_no_rflow() != d2.get_vds_flow_nat_no_rflow()) {
            return false;
        }
        if (d1.get_ds_flow_action_drop() != d2.get_vds_flow_action_drop()) {
            return false;
        }
        if (d1.get_ds_flow_action_invalid() != d2.get_vds_flow_action_invalid()) {
            return false;
        }
        if (d1.get_ds_flow_unusable() != d2.get_vds_flow_unusable()) {
            return false;
        }
        if (d1.get_ds_flow_table_full() != d2.get_vds_flow_table_full()) {
            return false;
        }
        if (d1.get_ds_interface_tx_discard() != d2.get_vds_interface_tx_discard()) {
            return false;
        }
        if (d1.get_ds_interface_drop() != d2.get_vds_interface_drop()) {
            return false;
        }
        if (d1.get_ds_duplicated() != d2.get_vds_duplicated()) {
            return false;
        }
        if (d1.get_ds_push() != d2.get_vds_push()) {
            return false;
        }
        if (d1.get_ds_ttl_exceeded() != d2.get_vds_ttl_exceeded()) {
            return false;
        }
        if (d1.get_ds_invalid_nh() != d2.get_vds_invalid_nh()) {
            return false;
        }
        if (d1.get_ds_invalid_label() != d2.get_vds_invalid_label()) {
            return false;
        }
        if (d1.get_ds_invalid_protocol() != d2.get_vds_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_interface_rx_discard() != d2.get_vds_interface_rx_discard()) {
            return false;
        }
        if (d1.get_ds_invalid_mcast_source() != d2.get_vds_invalid_mcast_source()) {
            return false;
        }
        if (d1.get_ds_head_alloc_fail() != d2.get_vds_head_alloc_fail()) {
            return false;
        }
        if (d1.get_ds_head_space_reserve_fail() != d2.get_vds_head_space_reserve_fail()) {
            return false;
        }
        if (d1.get_ds_pcow_fail() != d2.get_vds_pcow_fail()) {
            return false;
        }
        if (d1.get_ds_flood() != d2.get_vds_flood()) {
            return false;
        }
        if (d1.get_ds_mcast_clone_fail() != d2.get_vds_mcast_clone_fail()) {
            return false;
        }
        if (d1.get_ds_composite_invalid_interface() != d2.get_vds_composite_invalid_interface()) {
            return false;
        }
        if (d1.get_ds_rewrite_fail() != d2.get_vds_rewrite_fail()) {
            return false;
        }
        if (d1.get_ds_misc() != d2.get_vds_misc()) {
            return false;
        }
        if (d1.get_ds_invalid_packet() != d2.get_vds_invalid_packet()) {
            return false;
        }
        if (d1.get_ds_cksum_err() != d2.get_vds_cksum_err()) {
            return false;
        }
        if (d1.get_ds_clone_fail() != d2.get_vds_clone_fail()) {
            return false;
        }
        if (d1.get_ds_no_fmd() != d2.get_vds_no_fmd()) {
            return false;
        }
        if (d1.get_ds_cloned_original() != d2.get_vds_cloned_original()) {
            return false;
        }
        if (d1.get_ds_invalid_vnid() != d2.get_vds_invalid_vnid()) {
            return false;
        }
        if (d1.get_ds_frag_err() != d2.get_vds_frag_err()) {
            return false;
        }
        return true;
    }
    TestUveUtil util_;
};


TEST_F(UveVrouterUveTest, VmAddDel) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_virtual_machine_list().size());

    util_.VmAdd(1);

    EXPECT_EQ(1U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_virtual_machine_list().size());

    util_.VmAdd(2);

    EXPECT_EQ(2U, vr->vrouter_msg_count());
    EXPECT_EQ(2U, uve.get_virtual_machine_list().size());

    util_.VmDelete(2);

    EXPECT_EQ(3U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_virtual_machine_list().size());

    util_.VmDelete(1);

    EXPECT_EQ(4U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_virtual_machine_list().size());
}

TEST_F(UveVrouterUveTest, VnAddDel) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_connected_networks().size());

    util_.VnAdd(1);

    EXPECT_EQ(1U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_connected_networks().size());

    util_.VnAdd(2);

    EXPECT_EQ(2U, vr->vrouter_msg_count());
    EXPECT_EQ(2U, uve.get_connected_networks().size());

    util_.VnDelete(2);

    EXPECT_EQ(3U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_connected_networks().size());

    util_.VnDelete(1);

    EXPECT_EQ(4U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_connected_networks().size());
}

TEST_F(UveVrouterUveTest, ComputeCpuState_1) {

    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(0U, vr->compute_state_send_count());

    EnqueueVRouterStatsCollectorTask(5);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    EnqueueVRouterStatsCollectorTask(5);
    client->WaitForIdle();
    EXPECT_EQ(2U, vr->compute_state_send_count());
}

TEST_F(UveVrouterUveTest, DropStatsAddChange) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent &uve = vr->last_sent_stats();
    const vr_drop_stats_req ds_null;
    const AgentDropStats ds = uve.get_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds, ds_null));

    vr_drop_stats_req ds2;
    ds2.set_vds_discard(10);
    //Update drop-stats in mock kernel
    KSyncSockTypeMap::SetDropStats(ds2);

    //Fetch drop-stats
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();

    //Verify drop stats in agent_stats_collector
    vr_drop_stats_req fetched_ds = Agent::GetInstance()->uve()->agent_stats_collector()->drop_stats();
    EXPECT_TRUE(fetched_ds == ds2);

    //Trigger vrouter stats collection
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify UVE and send count
    EXPECT_EQ(1U, vr->vrouter_stats_msg_count());
    const VrouterStatsAgent &uve2 = vr->last_sent_stats();
    const AgentDropStats ds3 = uve2.get_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds3, ds2));

    //Update drop-stats in mock kernel
    ds2.set_vds_pull(10);
    KSyncSockTypeMap::SetDropStats(ds2);

    //Fetch drop-stats
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();

    //Trigger vrouter stats collection
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify UVE and send count
    EXPECT_EQ(2U, vr->vrouter_stats_msg_count());
    const VrouterStatsAgent &uve3 = vr->last_sent_stats();
    const AgentDropStats ds4 = uve3.get_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds4, ds2));
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    usleep(10000);
    int ret = RUN_ALL_TESTS();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();
    return ret;
}
