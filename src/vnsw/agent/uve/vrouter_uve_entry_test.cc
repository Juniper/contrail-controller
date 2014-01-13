/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vrouter_uve_entry_test.h>

VrouterUveEntryTest *VrouterUveEntryTest::singleton_;

VrouterUveEntryTest::VrouterUveEntryTest(Agent *agent)
        : VrouterUveEntry(agent) {
    singleton_ = this;
}
