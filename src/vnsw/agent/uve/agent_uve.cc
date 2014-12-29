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

AgentUve::AgentUve(Agent *agent, uint64_t intvl)
    : AgentUveBase(agent, intvl),
      stats_manager_(new StatsManager(agent)) {
      //Override vm_uve_table_ to point to derived class object
      vn_uve_table_.reset(new VnUveTable(agent));
      vm_uve_table_.reset(new VmUveTable(agent));
      vrouter_uve_entry_.reset(new VrouterUveEntry(agent));
}

AgentUve::~AgentUve() {
}

StatsManager *AgentUve::stats_manager() const {
    return stats_manager_.get();
}

void AgentUve::Shutdown() {
    AgentUveBase::Shutdown();
    stats_manager_->Shutdown();
}

void AgentUve::RegisterDBClients() {
    AgentUveBase::RegisterDBClients();
    stats_manager_->RegisterDBClients();
}

// The following is deprecated and is present only for backward compatibility
void GetStatsInterval::HandleRequest() const {
    StatsIntervalResp_InSeconds *resp = new StatsIntervalResp_InSeconds();
    resp->set_agent_stats_interval(0);
    resp->set_flow_stats_interval(0);
    resp->set_context(context());
    resp->Response();
    return;
}
