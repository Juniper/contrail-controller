/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <uve/test/agent_uve_test.h>
#include <uve/test/vn_uve_table_test.h>
#include <uve/test/vm_uve_table_test.h>
#include <uve/test/vrouter_uve_entry_test.h>
#include <uve/test/prouter_uve_table_test.h>

AgentUveBaseTest::AgentUveBaseTest(Agent *agent, uint64_t intvl,
                                   uint32_t default_intvl,
                                   uint32_t incremental_intvl)
    : AgentUve(agent, intvl, default_intvl, incremental_intvl) {
    if (vn_uve_table_) {
        vn_uve_table_->Shutdown();
    }
    if (vm_uve_table_) {
        vm_uve_table_->Shutdown();
    }
    if (vrouter_uve_entry_) {
        vrouter_uve_entry_->Shutdown();
    }
    if (prouter_uve_table_) {
        prouter_uve_table_->Shutdown();
    }
    vn_uve_table_.reset(new VnUveTableTest(agent, default_intvl));
    vm_uve_table_.reset(new VmUveTableTest(agent, default_intvl));
    vrouter_uve_entry_.reset(new VrouterUveEntryTest(agent));
    prouter_uve_table_.reset(new ProuterUveTableTest(agent, default_intvl));
}

AgentUveBaseTest::~AgentUveBaseTest() {
}

