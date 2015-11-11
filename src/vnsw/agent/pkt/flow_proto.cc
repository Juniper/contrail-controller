/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "pkt/flow_proto.h"
#include <init/agent_param.h>
#include <pkt/flow_mgmt.h>
#include <cmn/agent_stats.h>

FlowProto::FlowProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, kFlowHandlerTask.c_str(), PktHandler::FLOW, io) {
    agent->SetFlowProto(this);
    set_trace(false);
    uint16_t table_count = agent->flow_thread_count();
    assert(table_count >= kMinTableCount && table_count <= kMaxTableCount);
    for (uint8_t i = 0; i < table_count; i++) {
        flow_table_list_.push_back(new FlowTable(agent_, i));
    }

    TaskScheduler *scheduler = agent_->task_scheduler();
    uint32_t task_id = scheduler->GetTaskId(kFlowHandlerTask.c_str());
    for (uint32_t i = 0; i < table_count; i++) {
        flow_work_queue_list_.push_back
            (new FlowWorkQueue(task_id, -1, boost::bind(&Proto::ProcessProto,
                                                        this, _1)));
    }
}

FlowProto::~FlowProto() {
    STLDeleteValues(&flow_work_queue_list_);
    STLDeleteValues(&flow_table_list_);
}

void FlowProto::Init() {
    agent_->stats()->RegisterFlowCountFn(boost::bind(&FlowProto::FlowCount,
                                                     this));
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->Init();
    }
}

void FlowProto::InitDone() {
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->InitDone();
    }
}

void FlowProto::Shutdown() {
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->Shutdown();
    }
    for (uint32_t i = 0; i < flow_work_queue_list_.size(); i++) {
        flow_work_queue_list_[i]->Shutdown();
    }
}

FlowHandler *FlowProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    uint32_t index = FlowTableIndex(info->sport, info->dport);
    return new FlowHandler(agent(), info, io, this, index);
}

bool FlowProto::Validate(PktInfo *msg) {
    if (msg->l3_forwarding && msg->ip == NULL && msg->ip6 == NULL &&
        msg->type != PktType::MESSAGE) {
        FLOW_TRACE(DetailErr, msg->agent_hdr.cmd_param,
                   msg->agent_hdr.ifindex, msg->agent_hdr.vrf,
                   msg->ether_type, 0, "Flow : Non-IP packet. Dropping",
                   msg->l3_forwarding, 0, 0, 0, 0);
        return false;
    }
    return true;
}

uint16_t FlowProto::FlowTableIndex(uint16_t sport, uint16_t dport) const {
    return (sport ^ dport) % (flow_work_queue_list_.size());
}

FlowTable *FlowProto::GetFlowTable(const FlowKey &key) const {
    uint16_t index = FlowTableIndex(key.src_port, key.dst_port);
    return flow_table_list_[index];
}

bool FlowProto::Enqueue(boost::shared_ptr<PktInfo> msg) {
    uint32_t index = FlowTableIndex(msg->sport, msg->dport);
    if (Validate(msg.get()) == false) {
        return true;
    }
    FreeBuffer(msg.get());
    bool ret = flow_work_queue_list_[index]->Enqueue(msg);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// FlowTable related routines
/////////////////////////////////////////////////////////////////////////////
void FlowProto::FlushFlows() {
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->DeleteAll();
    }
}

FlowTable *FlowProto::GetTable(uint16_t index) const {
    return flow_table_list_[index];
}

uint32_t FlowProto::FlowCount() const {
    uint32_t count = 0;
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        count += flow_table_list_[i]->Size();
    }
    return count;
}

void FlowProto::VnFlowCounters(const VnEntry *vn, uint32_t *in_count,
                               uint32_t *out_count) {
    *in_count = 0;
    *out_count = 0;
    if (vn == NULL)
        return;

    agent_->pkt()->flow_mgmt_manager()->VnFlowCounters(vn, in_count, out_count);
}

FlowEntry *FlowProto::Find(const FlowKey &key) const {
    return GetFlowTable(key)->Find(key);
}
