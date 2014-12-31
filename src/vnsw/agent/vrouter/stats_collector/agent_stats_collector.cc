/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <db/db.h>
#include <cmn/agent_cmn.h>

#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>

#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"

#include <uve/stats_collector.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <init/agent_param.h>
#include <vrouter/stats_collector/interface_stats_io_context.h>
#include <vrouter/stats_collector/vrf_stats_io_context.h>
#include <vrouter/stats_collector/drop_stats_io_context.h>
#include <uve/agent_uve.h>
#include <vrouter/stats_collector/agent_stats_interval_types.h>

AgentStatsCollector::AgentStatsCollector
    (boost::asio::io_service &io, Agent* agent)
    : StatsCollector(TaskScheduler::GetInstance()->
                     GetTaskId("Agent::StatsCollector"),
                     StatsCollector::AgentStatsCollector,
                     io, agent->params()->agent_stats_interval(),
                     "Agent Stats collector"),
    agent_(agent) {
    intf_stats_sandesh_ctx_.reset(new AgentStatsSandeshContext(agent));
    vrf_stats_sandesh_ctx_.reset( new AgentStatsSandeshContext(agent));
    drop_stats_sandesh_ctx_.reset(new AgentStatsSandeshContext(agent));
}

AgentStatsCollector::~AgentStatsCollector() {
}

void AgentStatsCollector::SendInterfaceBulkGet() {
    vr_interface_req encoder;

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vifr_context(0);
    encoder.set_vifr_marker(intf_stats_sandesh_ctx_.get()->marker_id());
    SendRequest(encoder, InterfaceStatsType);
}

void AgentStatsCollector::SendVrfStatsBulkGet() {
    vr_vrf_stats_req encoder;

    encoder.set_h_op(sandesh_op::DUMP);
    encoder.set_vsr_rid(0);
    encoder.set_vsr_family(AF_INET);
    encoder.set_vsr_marker(vrf_stats_sandesh_ctx_.get()->marker_id());
    SendRequest(encoder, VrfStatsType);
}

void AgentStatsCollector::SendDropStatsBulkGet() {
    vr_drop_stats_req encoder;

    encoder.set_h_op(sandesh_op::GET);
    encoder.set_vds_rid(0);
    SendRequest(encoder, DropStatsType);
}

bool AgentStatsCollector::SendRequest(Sandesh &encoder, StatsType type) {
    int encode_len;
    int error;
    uint8_t *buf = (uint8_t *)malloc(KSYNC_DEFAULT_MSG_SIZE);

    encode_len = encoder.WriteBinary(buf, KSYNC_DEFAULT_MSG_SIZE, &error);
    SendAsync((char*)buf, encode_len, type);

    return true;
}

IoContext *AgentStatsCollector::AllocateIoContext(char* buf, uint32_t buf_len,
                                                  StatsType type, uint32_t seq) {
    switch (type) {
        case InterfaceStatsType:
            return (new InterfaceStatsIoContext(buf_len, buf, seq,
                                         intf_stats_sandesh_ctx_.get(),
                                         IoContext::UVE_Q_ID));
            break;
       case VrfStatsType:
            return (new VrfStatsIoContext(buf_len, buf, seq,
                                        vrf_stats_sandesh_ctx_.get(),
                                        IoContext::UVE_Q_ID));
            break;
       case DropStatsType:
            return (new DropStatsIoContext(buf_len, buf, seq,
                                         drop_stats_sandesh_ctx_.get(),
                                         IoContext::UVE_Q_ID));
            break;
       default:
            return NULL;
    }
}

void AgentStatsCollector::SendAsync(char* buf, uint32_t buf_len,
                                    StatsType type) {
    KSyncSock   *sock = KSyncSock::Get(0);
    uint32_t seq = sock->AllocSeqNo(true);

    IoContext *ioc = AllocateIoContext(buf, buf_len, type, seq);
    if (ioc) {
        sock->GenericSend(ioc);
    }
}

bool AgentStatsCollector::Run() {
    SendInterfaceBulkGet();
    SendVrfStatsBulkGet();
    SendDropStatsBulkGet();
    return true;
}

void AgentStatsCollector::SendStats() {
    VnUveTable *vnt = static_cast<VnUveTable *>
        (agent_->uve()->vn_uve_table());
    vnt->SendVnStats(false);

    VmUveTable *vmt = static_cast<VmUveTable *>
        (agent_->uve()->vm_uve_table());
    vmt->SendVmStats();
}

void AgentStatsCollector::Shutdown(void) {
    StatsCollector::Shutdown();
}

void SetAgentStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        Agent::GetInstance()->stats_collector()->set_expiry_time
            (get_interval() * 1000);
        resp = new AgentStatsCfgResp();
    } else {
        resp = new AgentStatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void GetAgentStatsInterval::HandleRequest() const {
    AgentStatsIntervalResp_InSeconds *resp =
        new AgentStatsIntervalResp_InSeconds();
    resp->set_agent_stats_interval((Agent::GetInstance()->stats_collector()->
        expiry_time())/1000);
    resp->set_context(context());
    resp->Response();
    return;
}
