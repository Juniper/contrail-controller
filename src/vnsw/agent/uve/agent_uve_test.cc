/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <uve/agent_uve_test.h>
#include <uve/vn_uve_table_test.h>
#include <uve/vm_uve_table_test.h>
#include <uve/vrouter_uve_entry_test.h>

AgentUveTest::AgentUveTest(Agent *agent, uint64_t intvl) 
    : AgentUve(agent, intvl) {
    vn_uve_table_.reset(new VnUveTableTest(agent)); 
    vm_uve_table_.reset(new VmUveTableTest(agent));
    vrouter_uve_entry_.reset(new VrouterUveEntryTest(agent));
}

AgentUveTest::~AgentUveTest() {
}

