/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
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
#include <uve/test/vrouter_uve_entry_test.h>
#include <uve/vrouter_stats_collector.h>
#include "testing/gunit.h"
#include "xmpp/test/xmpp_test_util.h"

VrouterUveEntryTest::VrouterUveEntryTest(Agent *agent)
        : VrouterUveEntry(agent), first_uve_dispatched_(false), 
        vrouter_msg_count_(0), vrouter_stats_msg_count_(0), 
        compute_state_send_count_(0), last_sent_vrouter_stats_(), 
        last_sent_vrouter_(), first_vrouter_uve_() {
}

VrouterUveEntryTest::~VrouterUveEntryTest() {
}

void VrouterUveEntryTest::clear_count() {
    compute_state_send_count_ = 0;
    vrouter_msg_count_ = 0;
    vrouter_stats_msg_count_ = 0;
}

void VrouterUveEntryTest::ResetCpuStatsCount() {
    cpu_stats_count_ = 0;
}

void VrouterUveEntryTest::ResetPortBitmap() {
    port_bitmap_.Reset();
}

void VrouterUveEntryTest::DispatchComputeCpuStateMsg
                             (const ComputeCpuState &ccs) {
    compute_state_send_count_++;
}

void VrouterUveEntryTest::DispatchVrouterMsg(const VrouterAgent &uve) {
    vrouter_msg_count_++;
    last_sent_vrouter_ = uve;
    AgentUve *u = static_cast<AgentUve *>(Agent::GetInstance()->uve());
    if (!first_uve_dispatched_ &&
        (u->vrouter_stats_collector()->run_counter_ == 1)) {
        first_uve_dispatched_ = true;
        first_vrouter_uve_ = uve;
    }
}

void VrouterUveEntryTest::DispatchVrouterStatsMsg(const VrouterStatsAgent &uve)
    {
    vrouter_stats_msg_count_++;
    last_sent_vrouter_stats_ = uve;
    if (uve.__isset.phy_band_in_bps) {
        prev_stats_.set_phy_band_in_bps(uve.get_phy_band_in_bps());
    }
    if (uve.__isset.phy_band_out_bps) {
        prev_stats_.set_phy_band_out_bps(uve.get_phy_band_out_bps());
    }
}

void VrouterUveEntryTest::WaitForWalkCompletion() {
    WAIT_FOR(1000, 500, (do_vn_walk_ == false));
    WAIT_FOR(1000, 500, (do_vm_walk_ == false));
    WAIT_FOR(1000, 500, (do_interface_walk_ == false));
    WAIT_FOR(1000, 500, (vn_walk_id_ == DBTableWalker::kInvalidWalkerId));
    WAIT_FOR(1000, 500, (vm_walk_id_ == DBTableWalker::kInvalidWalkerId));
    WAIT_FOR(1000, 500, (interface_walk_id_ == DBTableWalker::kInvalidWalkerId));
}

void VrouterUveEntryTest::set_prev_flow_setup_rate_export_time(uint64_t
                                                               micro_secs) {
    prev_flow_setup_rate_export_time_ = micro_secs;
}
