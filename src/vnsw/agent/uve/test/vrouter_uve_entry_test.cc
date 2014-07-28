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
#include <uve/test/vrouter_uve_entry_test.h>
#include <uve/vrouter_stats_collector.h>

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

void VrouterUveEntryTest::DispatchComputeCpuStateMsg
                             (const ComputeCpuState &ccs) {
    compute_state_send_count_++;
}

void VrouterUveEntryTest::DispatchVrouterMsg(const VrouterAgent &uve) {
    vrouter_msg_count_++;
    last_sent_vrouter_ = uve;
    if (!first_uve_dispatched_ &&
        (Agent::GetInstance()->uve()->vrouter_stats_collector()->run_counter_ == 1)) {
        first_uve_dispatched_ = true;
        first_vrouter_uve_ = uve;
    }
}

void VrouterUveEntryTest::DispatchVrouterStatsMsg(const VrouterStatsAgent &uve)
    {
    vrouter_stats_msg_count_++;
    last_sent_vrouter_stats_ = uve;
}
