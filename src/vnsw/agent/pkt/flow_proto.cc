/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <net/address_util.h>
#include <init/agent_param.h>
#include <cmn/agent_stats.h>
#include <oper/agent_profile.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>
#include "vrouter/flow_stats/flow_stats_collector.h"
#include "flow_proto.h"
#include "flow_mgmt_dbclient.h"
#include "flow_mgmt.h"
#include "flow_event.h"

static void UpdateStats(FlowEvent::Event event, FlowStats *stats);

FlowProto::FlowProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, kTaskFlowEvent, PktHandler::FLOW, io),
    flow_update_queue_(agent->task_scheduler()->GetTaskId(kTaskFlowUpdate), 0,
                       boost::bind(&FlowProto::FlowEventHandler, this, _1,
                                   static_cast<FlowTable *>(NULL))),
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
                                            _1, flow_table_list_[i])));
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
        if (msg->family == Address::INET) {
            FLOW_TRACE(DetailErr, msg->agent_hdr.cmd_param,
                       msg->agent_hdr.ifindex,
                       msg->agent_hdr.vrf,
                       msg->ip_saddr.to_v4().to_ulong(),
                       msg->ip_daddr.to_v4().to_ulong(),
                       "Flow : Non-IP packet. Dropping",
                       msg->l3_forwarding, 0, 0, 0, 0);
        } else if (msg->family == Address::INET6) {
            uint64_t sip[2], dip[2];
            Ip6AddressToU64Array(msg->ip_saddr.to_v6(), sip, 2);
            Ip6AddressToU64Array(msg->ip_daddr.to_v6(), dip, 2);
            FLOW_TRACE(DetailErr, msg->agent_hdr.cmd_param,
                       msg->agent_hdr.ifindex,
                       msg->agent_hdr.vrf, -1, -1,
                       "Flow : Non-IP packet. Dropping",
                       msg->l3_forwarding,
                       sip[0], sip[1], dip[0], dip[1]);
        } else {
            assert(0);
        }
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
    EnqueueFlowEvent(new FlowEvent(FlowEvent::VROUTER_FLOW_MSG, msg));
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
void FlowProto::EnqueueEvent(FlowEvent *event, FlowTable *table) {
    flow_event_queue_[table->table_index()]->Enqueue(event);
}

void FlowProto::EnqueueFlowEvent(FlowEvent *event) {
    // Keep UpdateStats in-sync on add of new events
    UpdateStats(event->event(), &stats_);
    switch (event->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        PktInfo *info = event->pkt_info().get();
        uint32_t index = FlowTableIndex(info->sport, info->dport);
        flow_event_queue_[index]->Enqueue(event);
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        FlowTaskMsg *ipc = static_cast<FlowTaskMsg *>(event->pkt_info()->ipc);
        FlowTable *table = ipc->fe_ptr.get()->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::DELETE_FLOW: {
        FlowTable *table = GetFlowTable(event->get_flow_key());
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::EVICT_FLOW:
    case FlowEvent::RETRY_INDEX_ACQUIRE:
    case FlowEvent::REVALUATE_FLOW:
    case FlowEvent::FREE_FLOW_REF: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::FREE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY: {
        flow_update_queue_.Enqueue(event);
        break;
    }

    case FlowEvent::AUDIT_FLOW:
    case FlowEvent::GROW_FREE_LIST: {
        FlowTable *table = GetFlowTable(event->get_flow_key());
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::FLOW_HANDLE_UPDATE:
    case FlowEvent::KSYNC_VROUTER_ERROR:
    case FlowEvent::KSYNC_EVENT: {
        FlowTableKSyncObject *ksync_obj = static_cast<FlowTableKSyncObject *>
            (event->ksync_entry()->GetObject());
        FlowTable *table = ksync_obj->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    default:
        assert(0);
        break;
    }

    return;
}

bool FlowProto::FlowEventHandler(FlowEvent *req, FlowTable *table) {
    std::auto_ptr<FlowEvent> req_ptr(req);
    if (table) {
        table->ConcurrencyCheck();
    }
    switch (req->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        ProcessProto(req->pkt_info());
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        FlowHandler *handler = new FlowHandler(agent(), req->pkt_info(), io_,
                                               this, table->table_index());
        RunProtoHandler(handler);
        break;
    }

    case FlowEvent::DELETE_FLOW: {
        FlowTable *table = GetFlowTable(req->get_flow_key());
        table->Delete(req->get_flow_key(), req->get_del_rev_flow());
        break;
    }

    case FlowEvent::AUDIT_FLOW: {
        FlowEntryPtr flow = FlowEntry::Allocate(req->get_flow_key(), table);
        flow->InitAuditFlow(req->flow_handle());
        flow->flow_table()->Add(flow.get(), NULL);
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::EVICT_FLOW: {
        FlowEntry *flow = req->flow();
        if (flow->flow_handle() != req->flow_handle())
            break;
        flow->flow_table()->EvictFlow(flow);
        break;
    }

    // Flow was waiting for an index. Index is available now. Retry acquiring
    // the index
    case FlowEvent::RETRY_INDEX_ACQUIRE: {
        FlowEntry *flow = req->flow();
        if (flow->flow_handle() != req->flow_handle())
            break;
        flow->flow_table()->UpdateKSync(flow, false);
        break;
    }

    case FlowEvent::FREE_FLOW_REF:
        break;

    case FlowEvent::FREE_DBENTRY: {
        FlowMgmtManager *mgr = agent()->pkt()->flow_mgmt_manager();
        mgr->flow_mgmt_dbclient()->FreeDBState(req->db_entry(), req->gen_id());
        break;
    }

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY:
    case FlowEvent::REVALUATE_FLOW: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->FlowResponseHandler(req);
        break;
    }

    case FlowEvent::GROW_FREE_LIST: {
        FlowTable *table = GetFlowTable(req->get_flow_key());
        table->GrowFreeList();
        break;
    }

    case FlowEvent::FLOW_HANDLE_UPDATE: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
        FlowEntry *flow = ksync_entry->flow_entry().get();
        table->KSyncSetFlowHandle(flow, req->flow_handle());
        break;
    }

    case FlowEvent::KSYNC_EVENT: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
        FlowTableKSyncObject *ksync_object = static_cast<FlowTableKSyncObject *>
            (ksync_entry->GetObject());
        ksync_object->GenerateKSyncEvent(ksync_entry, req->ksync_event());
        break;
    }

    case FlowEvent::KSYNC_VROUTER_ERROR: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (req->ksync_entry()));
        // Mark the flow entry as short flow and update ksync error event
        // to ksync index manager
        FlowEntry *flow = ksync_entry->flow_entry().get();
        flow->MakeShortFlow(FlowEntry::SHORT_FAILED_VROUTER_INSTALL);
        KSyncFlowIndexManager *mgr =
            agent()->ksync()->ksync_flow_index_manager();
        mgr->UpdateKSyncError(flow);
        // Enqueue Add request to flow-stats-collector
        // to update flow flags in stats collector
        FlowEntryPtr flow_ptr(flow);
        agent()->flow_stats_manager()->AddEvent(flow_ptr);
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
    EnqueueFlowEvent(new FlowEvent(FlowEvent::DELETE_FLOW, flow_key,
                                   del_rev_flow));
    return;
}

void FlowProto::EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::EVICT_FLOW, flow, flow_handle));
    return;
}

void FlowProto::RetryIndexAcquireRequest(FlowEntry *flow, uint32_t flow_handle){
    EnqueueFlowEvent(new FlowEvent(FlowEvent::RETRY_INDEX_ACQUIRE, flow,
                                   flow_handle));
    return;
}

void FlowProto::CreateAuditEntry(const FlowKey &key, uint32_t flow_handle) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::AUDIT_FLOW, key, flow_handle));
    return;
}


void FlowProto::GrowFreeListRequest(const FlowKey &key) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::GROW_FREE_LIST, key, false));
    return;
}

void FlowProto::KSyncEventRequest(KSyncEntry *ksync_entry,
                                  KSyncEntry::KSyncEvent event) {
    EnqueueFlowEvent(new FlowEvent(ksync_entry, event));
    return;
}

void FlowProto::KSyncFlowHandleRequest(KSyncEntry *ksync_entry,
                                       uint32_t flow_handle) {
    EnqueueFlowEvent(new FlowEvent(ksync_entry, flow_handle));
    return;
}

void FlowProto::KSyncFlowErrorRequest(KSyncEntry *ksync_entry) {
    EnqueueFlowEvent(new FlowEvent(ksync_entry));
    return;
}

void FlowProto::MessageRequest(InterTaskMsg *msg) {
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(PktHandler::FLOW, msg));
    FreeBuffer(pkt_info.get());
    EnqueueFlowEvent(new FlowEvent(FlowEvent::FLOW_MESSAGE, pkt_info));
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
    case FlowEvent::FLOW_MESSAGE:
        stats->flow_messages_++;
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
    case FlowEvent::FLOW_HANDLE_UPDATE:
        stats->handle_update_++;
        break;
    case FlowEvent::KSYNC_VROUTER_ERROR:
        stats->vrouter_error_++;
        break;
    default:
        break;
    }
}

void FlowProto::SetProfileData(ProfileData *data) {
    data->flow_.flow_count_ = FlowCount();
    data->flow_.add_count_ = stats_.add_count_;
    data->flow_.del_count_ = stats_.delete_count_;
    data->flow_.audit_count_ = stats_.audit_count_;
    data->flow_.reval_count_ = stats_.revaluate_count_;
    data->flow_.handle_update_ = stats_.handle_update_;
    data->flow_.vrouter_error_ = stats_.vrouter_error_;
}
