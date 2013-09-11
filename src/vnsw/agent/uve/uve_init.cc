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

AgentUve *AgentUve::singleton_;

AgentUve::AgentUve(int time_interval) {
    EventManager *evm = Agent::GetInstance()->GetEventManager();
    agent_stats_collector_ = new AgentStatsCollector(*evm->io_service(), time_interval);
    vrouter_stats_collector_ = new VrouterStatsCollector(*evm->io_service());
    flow_stats_collector_ = new FlowStatsCollector(*evm->io_service(), time_interval);
    inter_vn_stats_collector_ = new InterVnStatsCollector();
    intf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    vrf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    drop_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
}

void AgentUve::Init(int time_interval, uint64_t band_intvl) {
    singleton_ = new AgentUve(time_interval);
    UveClient::Init(band_intvl);
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
