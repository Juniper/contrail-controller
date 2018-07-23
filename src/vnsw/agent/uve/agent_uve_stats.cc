/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/cpuinfo.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>

#include <uve/stats_collector.h>
#include <uve/agent_uve_stats.h>
#include <uve/stats_interval_types.h>
#include <init/agent_param.h>
#include <oper/mirror_table.h>
#include <uve/vrouter_stats_collector.h>
#include <uve/vm_uve_table.h>
#include <uve/vn_uve_table.h>
#include <uve/vrouter_uve_entry.h>
#include <uve/interface_uve_stats_table.h>

AgentUveStats::AgentUveStats(Agent *agent, uint64_t intvl,
                             uint32_t default_intvl, uint32_t incremental_intvl)
    : AgentUveBase(agent, intvl, default_intvl, incremental_intvl),
      stats_manager_(new StatsManager(agent)) {
      vn_uve_table_.reset(new VnUveTable(agent, default_intvl));
      vm_uve_table_.reset(new VmUveTable(agent, default_intvl));
      vrouter_uve_entry_.reset(new VrouterUveEntry(agent));
      interface_uve_table_.reset(new InterfaceUveStatsTable(agent,
                                                            default_intvl));
}

AgentUveStats::~AgentUveStats() {
}

StatsManager *AgentUveStats::stats_manager() const {
    return stats_manager_.get();
}

void AgentUveStats::Shutdown() {
    AgentUveBase::Shutdown();
    stats_manager_->Shutdown();
}

void AgentUveStats::RegisterDBClients() {
    AgentUveBase::RegisterDBClients();
    stats_manager_->RegisterDBClients();
}

void AgentUveStats::InitDone() {
    AgentUveBase::InitDone();
    stats_manager_->InitDone();
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
