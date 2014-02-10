/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vrouter_uve_entry_test.h>

VrouterUveEntryTest::VrouterUveEntryTest(Agent *agent)
        : VrouterUveEntry(agent), vrouter_msg_count_(0), 
        vrouter_stats_msg_count_(0), compute_state_send_count_(0),
        last_sent_vrouter_stats_(), last_sent_vrouter_() {
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
}

void VrouterUveEntryTest::DispatchVrouterStatsMsg(const VrouterStatsAgent &uve)
    {
    vrouter_stats_msg_count_++;
    last_sent_vrouter_stats_ = uve;
}
