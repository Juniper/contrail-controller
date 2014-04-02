/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/cpuinfo.h>
#include <db/db.h>
#include <cmn/agent_cmn.h>
#include <oper/interface_common.h>
#include <oper/interface.h>

#include "vr_genetlink.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <uve/agent_uve.h>
#include <uve/stats_interval_types.h>
#include <init/agent_param.h>
#include <oper/mirror_table.h>
#include <uve/vrouter_stats_collector.h>

AgentUve *AgentUve::singleton_;

AgentUve::AgentUve(Agent *agent, uint64_t intvl) 
    : vn_uve_table_(new VnUveTable(agent)), 
      vm_uve_table_(new VmUveTable(agent)), 
      vrouter_uve_entry_(new VrouterUveEntry(agent)),
      agent_stats_collector_(new AgentStatsCollector(
                                 *(agent->GetEventManager()->io_service()),
                                 agent)),
      agent_(agent), bandwidth_intvl_(intvl),
      vrouter_stats_collector_(new VrouterStatsCollector(
                                   *(agent->GetEventManager()->io_service()), 
                                   this)),
      flow_stats_collector_(new FlowStatsCollector(
                                 *(agent->GetEventManager()->io_service()),
                                 agent->params()->flow_stats_interval(),
                                 agent->params()->flow_cache_timeout(), 
                                 this)) {
    singleton_ = this;
}

AgentUve::~AgentUve() {
}

void AgentUve::Shutdown() {
    agent_stats_collector_->Shutdown();
    vn_uve_table_.get()->Shutdown();
    vm_uve_table_.get()->Shutdown();
    vrouter_uve_entry_.get()->Shutdown();
}

void AgentUve::Init() {
    CpuLoadData::Init();
}

void AgentUve::RegisterDBClients() {
    agent_stats_collector_->RegisterDBClients();
    vn_uve_table_.get()->RegisterDBClients();
    vm_uve_table_.get()->RegisterDBClients();
    vrouter_uve_entry_.get()->RegisterDBClients();
}

void AgentUve::NewFlow(const FlowEntry *flow) {
    uint8_t proto = flow->key().protocol;
    uint16_t sport = flow->key().src_port;
    uint16_t dport = flow->key().dst_port;

    // Update vrouter port bitmap
    vrouter_uve_entry_.get()->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    vn_uve_table_.get()->UpdateBitmap(flow->data().source_vn, 
                                      proto, sport, dport);

    // Update dest-vn port bitmap
    vn_uve_table_.get()->UpdateBitmap(flow->data().dest_vn, 
                                      proto, sport, dport);

    const Interface *intf = flow->data().intf_entry.get();

    const VmInterface *port = dynamic_cast<const VmInterface *>(intf);
    if (port == NULL) {
        return;
    }
    const VmEntry *vm = port->vm();
    if (vm == NULL) {
        return;
    }

    // update vm and interface (all interfaces of vm) bitmap
    vm_uve_table_.get()->UpdateBitmap(vm, proto, sport, dport);
}

void AgentUve::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to 
     * provide introspect to reset this */
}

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUve::GetInstance()->flow_stats_collector()->
            set_expiry_time(get_interval() * 1000);
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
        AgentUve::GetInstance()->agent_stats_collector()->
            set_expiry_time(get_interval() * 1000);
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
                                    agent_stats_collector()->
                                    expiry_time())/1000);
    resp->set_flow_stats_interval((AgentUve::GetInstance()->
                                   flow_stats_collector()->
                                   expiry_time())/1000);
    resp->set_context(context());
    resp->Response();
    return;
}

