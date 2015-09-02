/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/cpuinfo.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>

#include <uve/stats_collector.h>
#include <uve/agent_uve.h>
#include <uve/stats_interval_types.h>
#include <init/agent_param.h>
#include <oper/mirror_table.h>
#include <uve/vrouter_stats_collector.h>
#include <uve/vm_uve_table.h>
#include <uve/vn_uve_table.h>
#include <uve/vrouter_uve_entry.h>
#include <uve/interface_uve_stats_table.h>

AgentUve::AgentUve(Agent *agent, uint64_t intvl, uint32_t default_intvl,
                   uint32_t incremental_intvl)
    : AgentUveBase(agent, intvl, default_intvl, incremental_intvl) {

    vn_uve_table_.reset(new VnUveTableBase(agent, default_intvl));
    vm_uve_table_.reset(new VmUveTableBase(agent, default_intvl));
    vrouter_uve_entry_.reset(new VrouterUveEntryBase(agent));
    interface_uve_table_.reset(new InterfaceUveTable(agent, default_intvl));
}

AgentUve::~AgentUve() {
}
