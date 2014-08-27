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
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include "vr_types.h"
#include <uve/vrouter_stats_collector.h>
#include <uve/test/vrouter_uve_entry_test.h>
#include "uve/test/test_uve_util.h"
#include "ksync/ksync_sock_user.h"

using namespace std;

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};

void RouterIdDepInit(Agent *agent) {
}

int hash_id;
VmInterface *flow0, *flow1;

class UveVrouterUveTest : public ::testing::Test {
public:
    static void TestSetup() {
    }
    void FlowSetUp() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
    }

    void FlowTearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle(10);
        client->Reset();
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }

    bool ValidateBmap(const std::vector<uint32_t> &smap, const std::vector<uint32_t> &dmap, 
                      uint16_t sport, uint16_t dport) {
        bool ret = true;

        int idx = sport / L4PortBitmap::kBucketCount;
        int sport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int sport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        idx = dport / L4PortBitmap::kBucketCount;
        int dport_idx = idx / L4PortBitmap::kBitsPerEntry;
        int dport_bit = (1 << (idx % L4PortBitmap::kBitsPerEntry));

        uint32_t val = smap[sport_idx];
        if ((val & sport_bit) == 0) {
            EXPECT_STREQ("TCP Sport bit not set", "");
            ret = false;
        }

        val = dmap[dport_idx];
        if ((val & dport_bit) == 0) {
            EXPECT_STREQ("TCP Dport bit not set", "");
            ret = false;
        }
        return ret;
    }
    bool BandwidthMatch(const vector<AgentIfBandwidth> &list, int in, int out) {
        if (0 == list.size()) {
            if ((in == 0) && (out == 0)) {
                return true;
            }
            return false;
        }
        EXPECT_EQ(1U, list.size());
        vector<AgentIfBandwidth>::const_iterator it = list.begin();
        AgentIfBandwidth band = *it;
        EXPECT_EQ(in, band.get_in_bandwidth_usage());
        EXPECT_EQ(out, band.get_out_bandwidth_usage());
        if ((in == band.get_in_bandwidth_usage()) &&
            (out == band.get_out_bandwidth_usage())) {
            return true;
        }
        return false;
    }

    bool DropStatsEqual(const AgentDropStats &d1, const vr_drop_stats_req &d2) const {
        if (d1.get_ds_discard() != (uint64_t)d2.get_vds_discard()) {
            return false;
        }
        if (d1.get_ds_pull() != (uint64_t)d2.get_vds_pull()) {
            return false;
        }
        if (d1.get_ds_invalid_if() != (uint64_t)d2.get_vds_invalid_if()) {
            return false;
        }
        if (d1.get_ds_arp_not_me() != (uint64_t)d2.get_vds_arp_not_me()) {
            return false;
        }
        if (d1.get_ds_garp_from_vm() != (uint64_t)d2.get_vds_garp_from_vm()) {
            return false;
        }
        if (d1.get_ds_invalid_arp() != (uint64_t)d2.get_vds_invalid_arp()) {
            return false;
        }
        if (d1.get_ds_trap_no_if() != (uint64_t)d2.get_vds_trap_no_if()) {
            return false;
        }
        if (d1.get_ds_nowhere_to_go() != (uint64_t)d2.get_vds_nowhere_to_go()) {
            return false;
        }
        if (d1.get_ds_flow_queue_limit_exceeded() != (uint64_t)d2.get_vds_flow_queue_limit_exceeded()) {
            return false;
        }
        if (d1.get_ds_flow_no_memory() != (uint64_t)d2.get_vds_flow_no_memory()) {
            return false;
        }
        if (d1.get_ds_flow_invalid_protocol() != (uint64_t)d2.get_vds_flow_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_flow_nat_no_rflow() != (uint64_t)d2.get_vds_flow_nat_no_rflow()) {
            return false;
        }
        if (d1.get_ds_flow_action_drop() != (uint64_t)d2.get_vds_flow_action_drop()) {
            return false;
        }
        if (d1.get_ds_flow_action_invalid() != (uint64_t)d2.get_vds_flow_action_invalid()) {
            return false;
        }
        if (d1.get_ds_flow_unusable() != (uint64_t)d2.get_vds_flow_unusable()) {
            return false;
        }
        if (d1.get_ds_flow_table_full() != (uint64_t)d2.get_vds_flow_table_full()) {
            return false;
        }
        if (d1.get_ds_interface_tx_discard() != (uint64_t)d2.get_vds_interface_tx_discard()) {
            return false;
        }
        if (d1.get_ds_interface_drop() != (uint64_t)d2.get_vds_interface_drop()) {
            return false;
        }
        if (d1.get_ds_duplicated() != (uint64_t)d2.get_vds_duplicated()) {
            return false;
        }
        if (d1.get_ds_push() != (uint64_t)d2.get_vds_push()) {
            return false;
        }
        if (d1.get_ds_ttl_exceeded() != (uint64_t)d2.get_vds_ttl_exceeded()) {
            return false;
        }
        if (d1.get_ds_invalid_nh() != (uint64_t)d2.get_vds_invalid_nh()) {
            return false;
        }
        if (d1.get_ds_invalid_label() != (uint64_t)d2.get_vds_invalid_label()) {
            return false;
        }
        if (d1.get_ds_invalid_protocol() != (uint64_t)d2.get_vds_invalid_protocol()) {
            return false;
        }
        if (d1.get_ds_interface_rx_discard() != (uint64_t)d2.get_vds_interface_rx_discard()) {
            return false;
        }
        if (d1.get_ds_invalid_mcast_source() != (uint64_t)d2.get_vds_invalid_mcast_source()) {
            return false;
        }
        if (d1.get_ds_head_alloc_fail() != (uint64_t)d2.get_vds_head_alloc_fail()) {
            return false;
        }
        if (d1.get_ds_head_space_reserve_fail() != (uint64_t)d2.get_vds_head_space_reserve_fail()) {
            return false;
        }
        if (d1.get_ds_pcow_fail() != (uint64_t)d2.get_vds_pcow_fail()) {
            return false;
        }
        if (d1.get_ds_flood() != (uint64_t)d2.get_vds_flood()) {
            return false;
        }
        if (d1.get_ds_mcast_clone_fail() != (uint64_t)d2.get_vds_mcast_clone_fail()) {
            return false;
        }
        if (d1.get_ds_composite_invalid_interface() != (uint64_t)d2.get_vds_composite_invalid_interface()) {
            return false;
        }
        if (d1.get_ds_rewrite_fail() != (uint64_t)d2.get_vds_rewrite_fail()) {
            return false;
        }
        if (d1.get_ds_misc() != (uint64_t)d2.get_vds_misc()) {
            return false;
        }
        if (d1.get_ds_invalid_packet() != (uint64_t)d2.get_vds_invalid_packet()) {
            return false;
        }
        if (d1.get_ds_cksum_err() != (uint64_t)d2.get_vds_cksum_err()) {
            return false;
        }
        if (d1.get_ds_clone_fail() != (uint64_t)d2.get_vds_clone_fail()) {
            return false;
        }
        if (d1.get_ds_no_fmd() != (uint64_t)d2.get_vds_no_fmd()) {
            return false;
        }
        if (d1.get_ds_cloned_original() != (uint64_t)d2.get_vds_cloned_original()) {
            return false;
        }
        if (d1.get_ds_invalid_vnid() != (uint64_t)d2.get_vds_invalid_vnid()) {
            return false;
        }
        if (d1.get_ds_frag_err() != (uint64_t)d2.get_vds_frag_err()) {
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
    client->WaitForIdle();

    EXPECT_EQ(1U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_virtual_machine_list().size());

    util_.VmAdd(2);
    client->WaitForIdle();

    EXPECT_EQ(2U, vr->vrouter_msg_count());
    EXPECT_EQ(2U, uve.get_virtual_machine_list().size());

    util_.VmDelete(2);
    client->WaitForIdle();

    EXPECT_EQ(3U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_virtual_machine_list().size());

    util_.VmDelete(1);
    client->WaitForIdle();

    EXPECT_EQ(4U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_virtual_machine_list().size());
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, VnAddDel) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();

    const VrouterAgent &uve = vr->last_sent_vrouter();
    EXPECT_EQ(0U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_connected_networks().size());

    util_.VnAdd(1);
    client->WaitForIdle();

    EXPECT_EQ(1U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_connected_networks().size());

    util_.VnAdd(2);
    client->WaitForIdle();

    EXPECT_EQ(2U, vr->vrouter_msg_count());
    EXPECT_EQ(2U, uve.get_connected_networks().size());

    util_.VnDelete(2);
    client->WaitForIdle();

    EXPECT_EQ(3U, vr->vrouter_msg_count());
    EXPECT_EQ(1U, uve.get_connected_networks().size());

    util_.VnDelete(1);
    client->WaitForIdle();

    EXPECT_EQ(4U, vr->vrouter_msg_count());
    EXPECT_EQ(0U, uve.get_connected_networks().size());
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, ComputeCpuState_1) {

    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    vr->ResetCpuStatsCount();
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(0U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(5);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vr->compute_state_send_count());

    util_.EnqueueVRouterStatsCollectorTask(5);
    client->WaitForIdle();
    EXPECT_EQ(2U, vr->compute_state_send_count());
    vr->clear_count();
}

TEST_F(UveVrouterUveTest, DropStatsAddChange) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent &uve = vr->last_sent_stats();
    const vr_drop_stats_req ds_null;
    const AgentDropStats ds = uve.get_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds, ds_null));
    vr->clear_count();

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
    util_.EnqueueVRouterStatsCollectorTask(1);
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
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify UVE and send count
    EXPECT_EQ(2U, vr->vrouter_stats_msg_count());
    const VrouterStatsAgent &uve3 = vr->last_sent_stats();
    const AgentDropStats ds4 = uve3.get_drop_stats();
    EXPECT_TRUE(DropStatsEqual(ds4, ds2));

    //cleanup
    vr->clear_count();
    //Reset drop stats in mock kernel
    KSyncSockTypeMap::SetDropStats(ds_null);
    //Fetch drop-stats in agent_stats_collector by fetching null stats from
    //mock kernel
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();
}

TEST_F(UveVrouterUveTest, BandwidthTest_1) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    //Reset bandwidth counter which controls when bandwidth is updated
    VrouterStatsAgent &uve = vr->prev_stats();
    vr->set_bandwidth_count(0);
    vector<AgentIfBandwidth> empty_list;
    uve.set_phy_if_1min_usage(empty_list);

    PhysicalInterfaceKey key(Agent::GetInstance()->params()->eth_port());
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE((intf != NULL));

    //Fetch interface stats
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Fetch the stats object from agent_stats_collector
    AgentStatsCollector::InterfaceStats* stats = Agent::GetInstance()->uve()->
        agent_stats_collector()->GetInterfaceStats(intf);
    EXPECT_TRUE((stats != NULL));

    //Run Vrouter stats collector to update bandwidth
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    EXPECT_EQ(0, uve.get_phy_if_1min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_1min_usage(), 0, 0));

    //Update the stats object
    stats->speed = 1;
    stats->in_bytes = 1 * 1024 * 1024;
    stats->out_bytes = (60 * 1024 * 1024)/8; //60 Mbps = 60 MBps/8
    stats->prev_in_bytes = 0;
    stats->prev_out_bytes = 0;

    //Run Vrouter stats collector to update bandwidth
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    if (vr->bandwidth_count() == 1) {
        Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
                    run_counter_ >= 1));
    }

    EXPECT_EQ(2, vr->bandwidth_count());
    EXPECT_EQ(1U, uve.get_phy_if_1min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_1min_usage(), 13, 100));
    vr->clear_count();

    //cleanup
    stats->speed = stats->in_bytes = stats->out_bytes = stats->prev_in_bytes
        = stats->prev_out_bytes = 0;
}

TEST_F(UveVrouterUveTest, BandwidthTest_2) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    VrouterStatsAgent &uve = vr->prev_stats();

    PhysicalInterfaceKey key(Agent::GetInstance()->params()->eth_port());
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE((intf != NULL));

    //Fetch interface stats
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Fetch the stats object from agent_stats_collector
    AgentStatsCollector::InterfaceStats* stats = Agent::GetInstance()->uve()->
        agent_stats_collector()->GetInterfaceStats(intf);
    EXPECT_TRUE((stats != NULL));

    //Reset bandwidth counter which controls when bandwidth is updated
    vr->set_bandwidth_count(0);
    vector<AgentIfBandwidth> empty_list;
    uve.set_phy_if_5min_usage(empty_list);

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 1));

    //Update the stats object
    stats->speed = 1;
    stats->in_bytes = 1 * 1024 * 1024;
    stats->out_bytes = (5 * 60 * 1024 * 1024)/8; //60 Mbps = 60 MBps/8
    stats->prev_in_bytes = 0;
    stats->prev_out_bytes = 0;
    stats->prev_5min_in_bytes = 0;
    stats->prev_5min_out_bytes = 0;

    //Run Vrouter stats collector to update bandwidth
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(8);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 8));

    if (vr->bandwidth_count() == 8) {
        Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
                    run_counter_ >= 1));
    }

    EXPECT_EQ(9, vr->bandwidth_count());
    //Verify the 5-min bandwidth usage
    EXPECT_EQ(0, uve.get_phy_if_5min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 0, 0));

    //Run Vrouter stats collector again
    vr->clear_count();
    EXPECT_TRUE(stats->in_bytes != stats->prev_5min_in_bytes);
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify the 5-min bandwidth usage
    EXPECT_EQ(1U, uve.get_phy_if_5min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 2, 100));

    //Run Vrouter stats collector
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(9);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 9));

    //Verify the 5-min bandwidth usage has not changed
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 2, 100));

    //Run Vrouter stats collector again
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify the 5-min bandwidth usage
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_5min_usage(), 0, 0));

    //cleanup
    vr->clear_count();
    stats->speed = stats->in_bytes = stats->out_bytes = stats->prev_in_bytes
        = stats->prev_out_bytes = stats->prev_5min_in_bytes =
        stats->prev_5min_out_bytes = 0;
}

TEST_F(UveVrouterUveTest, BandwidthTest_3) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    VrouterStatsAgent &uve = vr->prev_stats();

    PhysicalInterfaceKey key(Agent::GetInstance()->params()->eth_port());
    Interface *intf = static_cast<Interface *>
        (Agent::GetInstance()->interface_table()->FindActiveEntry(&key));
    EXPECT_TRUE((intf != NULL));

    //Fetch interface stats
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    Agent::GetInstance()->uve()->agent_stats_collector()->Run();
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Fetch the stats object from agent_stats_collector
    AgentStatsCollector::InterfaceStats* stats = Agent::GetInstance()->uve()->
        agent_stats_collector()->GetInterfaceStats(intf);
    EXPECT_TRUE((stats != NULL));

    //Reset bandwidth counter which controls when bandwidth is updated
    vr->set_bandwidth_count(0);
    vector<AgentIfBandwidth> empty_list;
    uve.set_phy_if_10min_usage(empty_list);

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 1));

    //Update the stats object
    stats->speed = 1;
    stats->in_bytes = 10 * 1024 * 1024;
    stats->out_bytes = (10 * 60 * 1024 * 1024)/8; //60 Mbps = 60 MBps/8
    stats->prev_in_bytes = 0;
    stats->prev_out_bytes = 0;
    stats->prev_10min_in_bytes = 0;
    stats->prev_10min_out_bytes = 0;

    //Run Vrouter stats collector to update bandwidth
    vr->set_bandwidth_count(1);
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(18);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 18));
    if (vr->bandwidth_count() == 18) {
        Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
                    run_counter_ >= 1));
    }

    //Verify the 10-min bandwidth usage
    EXPECT_EQ(19, vr->bandwidth_count());
    EXPECT_EQ(0, uve.get_phy_if_10min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_10min_usage(), 0, 0));

    //Run Vrouter stats collector again
    vr->clear_count();
    EXPECT_TRUE(stats->in_bytes != stats->prev_10min_in_bytes);
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 1));

    //Verify the 10-min bandwidth usage
    EXPECT_EQ(0, vr->bandwidth_count());
    WAIT_FOR(10000, 500, (uve.get_phy_if_10min_usage().size() == 1));
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_10min_usage(), 13, 100));

    //Run Vrouter stats collector
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(19);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 19));

    //Verify the 10-min bandwidth usage
    EXPECT_EQ(1U, uve.get_phy_if_10min_usage().size());
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_10min_usage(), 13, 100));

    //Run Vrouter stats collector again
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->
             run_counter_ >= 1));

    //Verify the 10-min bandwidth usage
    EXPECT_TRUE(BandwidthMatch(uve.get_phy_if_10min_usage(), 0, 0));
    vr->clear_count();
    stats->speed = stats->in_bytes = stats->out_bytes = stats->prev_in_bytes
        = stats->prev_out_bytes = stats->prev_10min_in_bytes =
        stats->prev_10min_out_bytes = 0;
}

TEST_F(UveVrouterUveTest, ExceptionPktsChange) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    Agent::GetInstance()->stats()->Reset();

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent &uve = vr->prev_stats();
    //Verify exception stats in UVE
    EXPECT_EQ(0U, uve.get_exception_packets());
    EXPECT_EQ(0U, uve.get_exception_packets_dropped());
    EXPECT_EQ(0U, uve.get_exception_packets_allowed());

    //Update exception stats 
    Agent::GetInstance()->stats()->incr_pkt_exceptions();
    Agent::GetInstance()->stats()->incr_pkt_exceptions();
    Agent::GetInstance()->stats()->incr_pkt_exceptions();

    Agent::GetInstance()->stats()->incr_pkt_dropped();

    //Run vrouter_stats_collector to update the UVE with updated exception stats
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(10000, 500, (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ >= 1));

    //Verify exception stats in UVE
    EXPECT_EQ(3U, uve.get_exception_packets());
    EXPECT_EQ(1U, uve.get_exception_packets_dropped());
    EXPECT_EQ(2U, uve.get_exception_packets_allowed());
    vr->clear_count();
    Agent::GetInstance()->stats()->Reset();
}

//Flow creation using IP and TCP packets
TEST_F(UveVrouterUveTest, Bitmap_1) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    vr->ResetPortBitmap();

    FlowSetUp();
    TestFlow flow[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_TCP, 100, 200, 
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a UDP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_UDP, 100, 200, 
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    EXPECT_EQ(0, Agent::GetInstance()->pkt()->flow_table()->Size());
    CreateFlow(flow, 2);
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (Agent::GetInstance()->uve()->
                         vrouter_stats_collector()->run_counter_ >= 1));

    const VrouterStatsAgent stats = vr->last_sent_stats();
    const std::vector<uint32_t> &tcp_smap = stats.get_tcp_sport_bitmap();
    const std::vector<uint32_t> &tcp_dmap = stats.get_tcp_dport_bitmap();
    const std::vector<uint32_t> &udp_smap = stats.get_udp_sport_bitmap();
    const std::vector<uint32_t> &udp_dmap = stats.get_udp_dport_bitmap();

    EXPECT_TRUE(ValidateBmap(tcp_smap, tcp_dmap, 100, 200));
    EXPECT_TRUE(ValidateBmap(udp_smap, udp_dmap, 100, 200));

    TestFlow flow2[] = {
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 2000, 
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a UDP forward and reverse flow
        {  TestFlowPkt(vm1_ip, vm2_ip, IPPROTO_UDP, 1000, 2000, 
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    vr->clear_count();
    CreateFlow(flow2, 2);
    EXPECT_EQ(8U, Agent::GetInstance()->pkt()->flow_table()->Size());

    util_.EnqueueVRouterStatsCollectorTask(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (Agent::GetInstance()->uve()->
                         vrouter_stats_collector()->run_counter_ >= 1));
    WAIT_FOR(1000, 500, (vr->vrouter_stats_msg_count() == 1));

    const VrouterStatsAgent stats2 = vr->last_sent_stats();
    const std::vector<uint32_t> &tcp_smap2 = stats2.get_tcp_sport_bitmap();
    const std::vector<uint32_t> &tcp_dmap2 = stats2.get_tcp_dport_bitmap();
    const std::vector<uint32_t> &udp_smap2 = stats2.get_udp_sport_bitmap();
    const std::vector<uint32_t> &udp_dmap2 = stats2.get_udp_dport_bitmap();

    EXPECT_TRUE(ValidateBmap(tcp_smap2, tcp_dmap2, 100, 200));
    EXPECT_TRUE(ValidateBmap(udp_smap2, udp_dmap2, 100, 200));
    FlowTearDown();
    vr->clear_count();
    vr->ResetPortBitmap();
}

/* Verifies agent configuration sent via vrouter UVE. */
TEST_F(UveVrouterUveTest, config_1) {
    VrouterUveEntryTest *vr = static_cast<VrouterUveEntryTest *>
        (Agent::GetInstance()->uve()->vrouter_uve_entry());
    vr->clear_count();
    Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ = 0;
    if (!vr->first_uve_dispatched()) {
        util_.EnqueueVRouterStatsCollectorTask(1);
        client->WaitForIdle();
        WAIT_FOR(1000, 500, (Agent::GetInstance()->uve()->
                 vrouter_stats_collector()->run_counter_ == 1));
        WAIT_FOR(1000, 500, (vr->vrouter_msg_count() == 1));
        EXPECT_TRUE(vr->first_uve_dispatched());
        const VrouterAgent &uve = vr->first_vrouter_uve();

        EXPECT_STREQ(uve.get_eth_name().c_str(), "vnet0");
        EXPECT_STREQ(uve.get_vhost_cfg().get_name().c_str(), "vhost0");
        EXPECT_STREQ(uve.get_vhost_cfg().get_ip().c_str(), "10.1.1.1");
        EXPECT_STREQ(uve.get_vhost_cfg().get_gateway().c_str(), "10.1.1.254");
        EXPECT_EQ(uve.get_vhost_cfg().get_ip_prefix_len(), 24);

        EXPECT_STREQ(uve.get_tunnel_type().c_str(), "MPLSoGRE");
        EXPECT_EQ(uve.get_flow_cache_timeout_cfg(), 0);

        EXPECT_STREQ(uve.get_hypervisor().c_str(), "kvm");
        EXPECT_STREQ(uve.get_ds_addr().c_str(), "0.0.0.0");
        EXPECT_EQ(uve.get_ds_xs_instances(), 0);
        EXPECT_STREQ(uve.get_control_ip().c_str(), "0.0.0.0");
        EXPECT_EQ(uve.get_ll_max_system_flows_cfg(), 3);
        EXPECT_EQ(uve.get_ll_max_vm_flows_cfg(), 2);
        EXPECT_EQ(uve.get_max_vm_flows_cfg(), 100);
        EXPECT_EQ(uve.get_dns_server_list_cfg().size(), 2);
        EXPECT_EQ(uve.get_control_node_list_cfg().size(), 2);
        EXPECT_EQ(uve.get_gateway_cfg_list().size(), 2);
        vr->clear_count();
    }
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit("controller/src/vnsw/agent/uve/test/vnswa_cfg.ini", ksync_init,
                      true, false, true, (10 * 60 * 1000), (10 * 60 * 1000),
                      true, true, (10 * 60 * 1000));

    usleep(10000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
