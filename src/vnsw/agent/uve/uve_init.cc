/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface.h>
#include <asm/types.h>

#include "vr_genetlink.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>
#include <uve/vrouter_stats.h>
#include <uve/flow_stats.h>
#include <uve/inter_vn_stats.h>
#include <oper/mirror_table.h>

AgentStatsCollector *AgentUve::agent_stats_collector_;
VrouterStatsCollector *AgentUve::vrouter_stats_collector_;
FlowStatsCollector *AgentUve::flow_stats_collector_;
InterVnStatsCollector *AgentUve::inter_vn_stats_collector_;
AgentStatsSandeshContext *AgentUve::intf_stats_sandesh_ctx_;
AgentStatsSandeshContext *AgentUve::vrf_stats_sandesh_ctx_;
AgentStatsSandeshContext *AgentUve::drop_stats_sandesh_ctx_;

void AgentUve::Init(int time_interval) {
    EventManager *evm = Agent::GetEventManager();
    agent_stats_collector_ = new AgentStatsCollector(*evm->io_service(), time_interval);
    vrouter_stats_collector_ = new VrouterStatsCollector(*evm->io_service());
    flow_stats_collector_ = new FlowStatsCollector(*evm->io_service(), time_interval);
    inter_vn_stats_collector_ = new InterVnStatsCollector();
    intf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    vrf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    drop_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    UveClient::Init();
}

void AgentUve::Shutdown() {
    delete agent_stats_collector_;
    agent_stats_collector_ = NULL;

    delete vrouter_stats_collector_;
    vrouter_stats_collector_ = NULL;

    delete flow_stats_collector_;
    flow_stats_collector_ = NULL;

    delete inter_vn_stats_collector_;
    inter_vn_stats_collector_ = NULL;

    delete intf_stats_sandesh_ctx_;
    intf_stats_sandesh_ctx_ = NULL;

    delete vrf_stats_sandesh_ctx_;
    vrf_stats_sandesh_ctx_ = NULL;

    delete drop_stats_sandesh_ctx_;
    drop_stats_sandesh_ctx_ = NULL;
}
