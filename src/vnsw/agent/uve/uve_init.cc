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
#include <uve/vrouter_stats.h>
#include <uve/uve_client.h>
#include <uve/flow_stats.h>
#include <uve/inter_vn_stats.h>
#include <uve/agent_uve_types.h>
#include <init/agent_param.h>
#include <oper/mirror_table.h>

AgentUve *AgentUve::singleton_;

AgentUve::AgentUve(Agent *agent) {
    singleton_ = this;
    EventManager *evm = agent->GetEventManager();
    agent_stats_collector_ = new AgentStatsCollector
        (*evm->io_service(), agent->params()->agent_stats_interval()),
    vrouter_stats_collector_ = new VrouterStatsCollector(*evm->io_service());
    flow_stats_collector_ = new FlowStatsCollector
        (*evm->io_service(), agent->params()->flow_stats_interval()),
    inter_vn_stats_collector_ = new InterVnStatsCollector();
    intf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    vrf_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
    drop_stats_sandesh_ctx_ = new AgentStatsSandeshContext();
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

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUve::GetInstance()->GetFlowStatsCollector()->
            SetExpiryTime(get_interval() * 1000);
        resp = new StatsCfgResp();
    } else {
        resp = new StatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void SetAgentStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUve::GetInstance()->GetStatsCollector()->
            SetExpiryTime(get_interval() * 1000);
        resp = new StatsCfgResp();
    } else {
        resp = new StatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void GetStatsInterval::HandleRequest() const {
    StatsIntervalResp_InSeconds *resp = new StatsIntervalResp_InSeconds();
    resp->set_agent_stats_interval((AgentUve::GetInstance()->
                                   GetStatsCollector()->
                                   GetExpiryTime())/1000);
    resp->set_flow_stats_interval((AgentUve::GetInstance()->
                                   GetFlowStatsCollector()->
                                   GetExpiryTime())/1000);
    resp->set_context(context());
    resp->Response();
    return;
}

void AgentUve::Init() {
    UveClient::Init();
}
