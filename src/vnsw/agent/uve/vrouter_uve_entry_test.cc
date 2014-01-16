/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vrouter_uve_entry_test.h>

VrouterUveEntryTest::VrouterUveEntryTest(Agent *agent)
        : VrouterUveEntry(agent), compute_state_send_count_(0) {
}

VrouterUveEntryTest::~VrouterUveEntryTest() {
}

void VrouterUveEntryTest::clear_count() {
    compute_state_send_count_ = 0;
}

void VrouterUveEntryTest::DispatchComputeCputStateMsg
                             (const ComputeCpuState &ccs) {
    compute_state_send_count_++;
}

