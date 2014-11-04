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
#include <uve/test/agent_stats_collector_test.h>
#include <uve/test/flow_stats_collector_test.h>

AgentUveBaseTest::AgentUveBaseTest(Agent *agent, uint64_t intvl)
    : AgentUve(agent, intvl) {
    vn_uve_table_.reset(new VnUveTableTest(agent)); 
    vm_uve_table_.reset(new VmUveTableTest(agent));
    vrouter_uve_entry_.reset(new VrouterUveEntryTest(agent));
    agent_stats_collector_.reset(new AgentStatsCollectorTest(
                                 *(agent->event_manager()->io_service()),
                                 agent));
    flow_stats_collector_.reset(new FlowStatsCollectorTest(
                                *(agent->event_manager()->io_service()),
                                agent->params()->flow_stats_interval(),
                                agent->params()->flow_cache_timeout(),
                                this));
}

AgentUveBaseTest::~AgentUveBaseTest() {
}

