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
#include <uve/agent_uve.h>
#include <uve/vm_uve_table.h>
#include <uve/vn_uve_table.h>
#include <uve/vrouter_uve_entry.h>

AgentUve::AgentUve(Agent *agent, uint64_t intvl)
    : AgentUveBase(agent, intvl),
      agent_stats_collector_(new AgentStatsCollector(
                                 *(agent->event_manager()->io_service()),
                                 agent)),
      flow_stats_collector_(new FlowStatsCollector(
                                 *(agent->event_manager()->io_service()),
                                 agent->params()->flow_stats_interval(),
                                 agent->params()->flow_cache_timeout(),
                                 this)) {
      //Override vm_uve_table_ to point to derived class object
      vn_uve_table_.reset(new VnUveTable(agent));
      vm_uve_table_.reset(new VmUveTable(agent));
      vrouter_uve_entry_.reset(new VrouterUveEntry(agent));
}

AgentUve::~AgentUve() {
}

void AgentUve::Shutdown() {
    AgentUveBase::Shutdown();
    agent_stats_collector_->Shutdown();
    flow_stats_collector_->Shutdown();
    vrouter_stats_collector_->Shutdown();
}

void AgentUve::RegisterDBClients() {
    AgentUveBase::RegisterDBClients();
    agent_stats_collector_->RegisterDBClients();
}

void AgentUve::NewFlow(const FlowEntry *flow) {
    uint8_t proto = flow->key().protocol;
    uint16_t sport = flow->key().src_port;
    uint16_t dport = flow->key().dst_port;

    // Update vrouter port bitmap
    VrouterUveEntry *vre = static_cast<VrouterUveEntry *>(
        vrouter_uve_entry_.get());
    vre->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    VnUveTable *vnte = static_cast<VnUveTable *>(vn_uve_table_.get());
    vnte->UpdateBitmap(flow->data().source_vn, proto, sport, dport);

    // Update dest-vn port bitmap
    vnte->UpdateBitmap(flow->data().dest_vn, proto, sport, dport);

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
    VmUveTable *vmt = static_cast<VmUveTable *>(vm_uve_table_.get());
    vmt->UpdateBitmap(vm, proto, sport, dport);
}

void AgentUve::DeleteFlow(const FlowEntry *flow) {
    /* We need not reset bitmaps on flow deletion. We will have to
     * provide introspect to reset this */
}

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        AgentUveBase *uve = Agent::GetInstance()->uve();
        AgentUve *f_uve = static_cast<AgentUve *>(uve);
        FlowStatsCollector *fec = f_uve->flow_stats_collector();
        fec->set_expiry_time(get_interval() * 1000);
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
        AgentUve *uve = static_cast<AgentUve *>
            (AgentUveBase::GetInstance());
        uve->agent_stats_collector()->set_expiry_time(get_interval() * 1000);
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
    AgentUve *uve = static_cast<AgentUve *>
            (AgentUveBase::GetInstance());
    resp->set_agent_stats_interval((uve->agent_stats_collector()->
                                    expiry_time())/1000);
    resp->set_flow_stats_interval((uve->flow_stats_collector()->
                                   expiry_time())/1000);
    resp->set_context(context());
    resp->Response();
    return;
}

