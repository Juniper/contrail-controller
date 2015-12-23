/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <init/agent_param.h>
#include <cmn/agent_stats.h>
#include <oper/agent_profile.h>
#include "flow_proto.h"
#include "flow_mgmt_dbclient.h"
#include "flow_mgmt.h"
#include "flow_event.h"

static void UpdateStats(FlowEvent::Event event, FlowStats *stats);

FlowProto::FlowProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, kTaskFlowEvent, PktHandler::FLOW, io),
    flow_update_queue_(agent->task_scheduler()->GetTaskId(kTaskFlowUpdate), 0,
                       boost::bind(&FlowProto::FlowEventHandler, this, _1)),
    stats_() {
    flow_update_queue_.set_name("Flow update queue");
    agent->SetFlowProto(this);
    set_trace(false);
    uint16_t table_count = agent->flow_thread_count();
    assert(table_count >= kMinTableCount && table_count <= kMaxTableCount);
    for (uint8_t i = 0; i < table_count; i++) {
        flow_table_list_.push_back(new FlowTable(agent_, i));
    }

    TaskScheduler *scheduler = agent_->task_scheduler();
    uint32_t task_id = scheduler->GetTaskId(kTaskFlowEvent);
    for (uint32_t i = 0; i < table_count; i++) {
        flow_event_queue_.push_back
            (new FlowEventQueue(task_id, i,
                                boost::bind(&FlowProto::FlowEventHandler, this,
                                            _1)));
    }
}

FlowProto::~FlowProto() {
    STLDeleteValues(&flow_event_queue_);
    STLDeleteValues(&flow_table_list_);
}

void FlowProto::Init() {
    agent_->stats()->RegisterFlowCountFn(boost::bind(&FlowProto::FlowCount,
                                                     this));
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->Init();
    }

    AgentProfile *profile = agent_->oper_db()->agent_profile();
    profile->RegisterPktFlowStatsCb(boost::bind(&FlowProto::SetProfileData,
                                                this, _1));
 
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
    for (uint32_t i = 0; i < flow_event_queue_.size(); i++) {
        flow_event_queue_[i]->Shutdown();
    }
    flow_update_queue_.Shutdown();
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
    return (sport ^ dport) % (flow_event_queue_.size());
}

FlowTable *FlowProto::GetFlowTable(const FlowKey &key) const {
    uint16_t index = FlowTableIndex(key.src_port, key.dst_port);
    return flow_table_list_[index];
}

bool FlowProto::Enqueue(boost::shared_ptr<PktInfo> msg) {
    if (Validate(msg.get()) == false) {
        return true;
    }
    FreeBuffer(msg.get());
    FlowEvent event(FlowEvent::VROUTER_FLOW_MSG, msg);
    EnqueueFlowEvent(event);
    return true;
}

void FlowProto::DisableFlowEventQueue(uint32_t index, bool disabled) {
    flow_event_queue_[index]->set_disable(disabled);
}

void FlowProto::DisableFlowMgmtQueue(bool disabled) {
    flow_update_queue_.set_disable(disabled);
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

bool FlowProto::AddFlow(FlowEntry *flow) {
    FlowTable *table = flow->flow_table();
    table->Add(flow, flow->reverse_flow_entry());
    return true;
}

bool FlowProto::UpdateFlow(FlowEntry *flow) {
    FlowTable *table = flow->flow_table();
    table->Update(flow, flow->reverse_flow_entry());
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Flow Control Event routines
/////////////////////////////////////////////////////////////////////////////
void FlowProto::EnqueueFlowEvent(const FlowEvent &event) {
    // Keep UpdateStats in-sync on add of new events
    UpdateStats(event.event(), &stats_);
    switch (event.event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        PktInfo *info = event.pkt_info().get();
        uint32_t index = FlowTableIndex(info->sport, info->dport);
        flow_event_queue_[index]->Enqueue(event);
        break;
    }

    case FlowEvent::DELETE_FLOW: {
        FlowTable *table = GetFlowTable(event.get_flow_key());
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::AUDIT_FLOW:
    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::EVICT_FLOW:
    case FlowEvent::RETRY_INDEX_ACQUIRE:
    case FlowEvent::REVALUATE_FLOW:
    case FlowEvent::FREE_FLOW_REF: {
        FlowEntry *flow = event.flow();
        FlowTable *table = flow->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::FREE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY: {
        flow_update_queue_.Enqueue(event);
        break;
    }

    case FlowEvent::GROW_FREE_LIST: {
        FlowTable *table = GetFlowTable(event.get_flow_key());
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    default:
        assert(0);
        break;
    }

    return;
}

bool FlowProto::FlowEventHandler(const FlowEvent &req) {
    switch (req.event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        ProcessProto(req.pkt_info());
        break;
    }

    case FlowEvent::DELETE_FLOW: {
        FlowTable *table = GetFlowTable(req.get_flow_key());
        table->Delete(req.get_flow_key(), req.get_del_rev_flow());
        break;
    }

    case FlowEvent::AUDIT_FLOW: {
        FlowEntry *flow = req.flow();
        flow->flow_table()->Add(flow, NULL);
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::EVICT_FLOW: {
        FlowEntry *flow = req.flow();
        if (flow->flow_handle() != req.flow_handle())
            break;
        flow->flow_table()->DeleteMessage(flow);
        break;
    }

    // Flow was waiting for an index. Index is available now. Retry acquiring
    // the index
    case FlowEvent::RETRY_INDEX_ACQUIRE: {
        FlowEntry *flow = req.flow();
        if (flow->flow_handle() != req.flow_handle())
            break;
        flow->flow_table()->UpdateKSync(flow, false);
        break;
    }

    case FlowEvent::FREE_FLOW_REF:
        break;

    case FlowEvent::FREE_DBENTRY: {
        FlowMgmtManager *mgr = agent()->pkt()->flow_mgmt_manager();
        mgr->flow_mgmt_dbclient()->FreeDBState(req.db_entry(), req.gen_id());
        break;
    }

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY:
    case FlowEvent::REVALUATE_FLOW: {
        FlowEntry *flow = req.flow();
        flow->flow_table()->FlowResponseHandler(&req);
        break;
    }

    case FlowEvent::GROW_FREE_LIST: {
        FlowTable *table = GetFlowTable(req.get_flow_key());
        table->GrowFreeList();
        break;
    }

    default: {
        assert(0);
        break;
    }
    }
    return true;
}

void FlowProto::DeleteFlowRequest(const FlowKey &flow_key, bool del_rev_flow) {
    EnqueueFlowEvent(FlowEvent(FlowEvent::DELETE_FLOW, flow_key, del_rev_flow));
    return;
}

void FlowProto::EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle) {
    EnqueueFlowEvent(FlowEvent(FlowEvent::EVICT_FLOW, flow, flow_handle));
    return;
}

void FlowProto::RetryIndexAcquireRequest(FlowEntry *flow, uint32_t flow_handle){
    EnqueueFlowEvent(FlowEvent(FlowEvent::RETRY_INDEX_ACQUIRE, flow,
                               flow_handle));
    return;
}

void FlowProto::CreateAuditEntry(FlowEntry *flow) {
    EnqueueFlowEvent(FlowEvent(FlowEvent::AUDIT_FLOW, flow));
    return;
}


void FlowProto::GrowFreeListRequest(const FlowKey &key) {
    EnqueueFlowEvent(FlowEvent(FlowEvent::GROW_FREE_LIST, key, false));
    return;
}

//////////////////////////////////////////////////////////////////////////////
// Set profile information
//////////////////////////////////////////////////////////////////////////////
void UpdateStats(FlowEvent::Event event, FlowStats *stats) {
    switch (event) {
    case FlowEvent::VROUTER_FLOW_MSG:
        stats->add_count_++;
        break;
    case FlowEvent::DELETE_FLOW:
        stats->delete_count_++;
        break;
    case FlowEvent::AUDIT_FLOW:
        stats->audit_count_++;
        break;
    case FlowEvent::REVALUATE_FLOW:
        stats->revaluate_count_++;
        break;
    default:
        break;
    }
}

void FlowProto::SetProfileData(ProfileData *data) {
    data->flow_.add_count_ = stats_.add_count_;
    data->flow_.del_count_ = stats_.delete_count_;
    data->flow_.audit_count_ = stats_.audit_count_;
    data->flow_.reval_count_ = stats_.revaluate_count_;
}
