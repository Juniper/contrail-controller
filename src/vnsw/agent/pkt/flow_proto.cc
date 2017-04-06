/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <net/address_util.h>
#include <boost/functional/hash.hpp>
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

static void UpdateStats(FlowEvent *event, FlowStats *stats);

FlowProto::FlowProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, kTaskFlowEvent, PktHandler::FLOW, io),
    add_tokens_("Add Tokens", this, agent->flow_add_tokens()),
    ksync_tokens_("KSync` Tokens", this, agent->flow_ksync_tokens()),
    del_tokens_("Delete Tokens", this, agent->flow_del_tokens()),
    update_tokens_("Update Tokens", this, agent->flow_update_tokens()),
    flow_update_queue_(agent, this, &update_tokens_,
                       agent->params()->flow_task_latency_limit(), 16),
    use_vrouter_hash_(false), ipv4_trace_filter_(), ipv6_trace_filter_(),
    stats_(),
    stats_update_timer_(TimerManager::CreateTimer
        (*(agent->event_manager())->io_service(), "FlowStatsUpdateTimer",
         TaskScheduler::GetInstance()->GetTaskId(kTaskFlowStatsUpdate), 0)) {
    linklocal_flow_count_ = 0;
    agent->SetFlowProto(this);
    set_trace(false);
    uint16_t table_count = agent->flow_thread_count();
    assert(table_count >= kMinTableCount && table_count <= kMaxTableCount);
    for (uint8_t i = 0; i < table_count; i++) {
        flow_table_list_.push_back(new FlowTable(agent_, i));
    }

    for (uint32_t i = 0; i < table_count; i++) {
        uint16_t latency = agent->params()->flow_task_latency_limit();
        flow_event_queue_.push_back
            (new FlowEventQueue(agent, this, flow_table_list_[i],
                                &add_tokens_, latency, 16));

        flow_tokenless_queue_.push_back
            (new FlowEventQueue(agent, this, flow_table_list_[i],
                                NULL, latency, 16));

        flow_delete_queue_.push_back
            (new DeleteFlowEventQueue(agent, this, flow_table_list_[i],
                                      &del_tokens_, latency, 16));

        flow_ksync_queue_.push_back
            (new KSyncFlowEventQueue(agent, this, flow_table_list_[i],
                                     &ksync_tokens_, latency, 32));
    }
    if (::getenv("USE_VROUTER_HASH") != NULL) {
        string opt = ::getenv("USE_VROUTER_HASH");
        if (opt == "" || strcasecmp(opt.c_str(), "false"))
            use_vrouter_hash_ = false;
        else
            use_vrouter_hash_ = true;
    }
}

FlowProto::~FlowProto() {
    STLDeleteValues(&flow_event_queue_);
    STLDeleteValues(&flow_tokenless_queue_);
    STLDeleteValues(&flow_delete_queue_);
    STLDeleteValues(&flow_ksync_queue_);
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

    ipv4_trace_filter_.Init(agent_->flow_trace_enable(), Address::INET);
    ipv6_trace_filter_.Init(agent_->flow_trace_enable(), Address::INET6);
}

void FlowProto::InitDone() {
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->InitDone();
    }
    stats_update_timer_->Start(agent_->stats()->flow_stats_update_timeout(),
        boost::bind(&FlowProto::FlowStatsUpdate, this));
}

void FlowProto::Shutdown() {
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        flow_table_list_[i]->Shutdown();
    }
    for (uint32_t i = 0; i < flow_event_queue_.size(); i++) {
        flow_event_queue_[i]->Shutdown();
        flow_tokenless_queue_[i]->Shutdown();
        flow_delete_queue_[i]->Shutdown();
        flow_ksync_queue_[i]->Shutdown();
    }
    flow_update_queue_.Shutdown();
    if (stats_update_timer_) {
        stats_update_timer_->Cancel();
        TimerManager::DeleteTimer(stats_update_timer_);
    }
}

static std::size_t HashCombine(std::size_t hash, uint64_t val) {
    boost::hash_combine(hash, val);
    return hash;
}

static std::size_t HashIp(std::size_t hash, const IpAddress &ip) {
    if (ip.is_v6()) {
        uint64_t val[2];
        Ip6AddressToU64Array(ip.to_v6(), val, 2);
        hash = HashCombine(hash, val[0]);
        hash = HashCombine(hash, val[1]);
    } else if (ip.is_v4()) {
        hash = HashCombine(hash, ip.to_v4().to_ulong());
    } else {
        assert(0);
    }
    return hash;
}

// Get the thread to be used for the flow. We *try* to map forward and reverse
// flow to same thread with following,
//  if (sip < dip)
//      ip1 = sip
//      ip2 = dip
//  else
//      ip1 = dip
//      ip2 = sip
//  if (sport < dport)
//      port1 = sport
//      port2 = dport
//  else
//      port1 = dport
//      port2 = sport
//  field5 = proto
//  hash = HASH(ip1, ip2, port1, port2, proto)
//
// The algorithm above cannot ensure NAT flows belong to same thread.
uint16_t FlowProto::FlowTableIndex(const IpAddress &sip, const IpAddress &dip,
                                   uint8_t proto, uint16_t sport,
                                   uint16_t dport, uint32_t flow_handle) const {
    if (use_vrouter_hash_) {
        return (flow_handle/flow_table_list_.size()) % flow_table_list_.size();
    }

    std::size_t hash = 0;
    if (sip < dip) {
        hash = HashIp(hash, sip);
        hash = HashIp(hash, dip);
    } else {
        hash = HashIp(hash, dip);
        hash = HashIp(hash, sip);
    }

    if (sport < dport) {
        hash = HashCombine(hash, sport);
        hash = HashCombine(hash, dport);
    } else {
        hash = HashCombine(hash, dport);
        hash = HashCombine(hash, sport);
    }
    hash = HashCombine(hash, proto);
    return (hash % (flow_event_queue_.size()));
}

FlowHandler *FlowProto::AllocProtoHandler(PktInfoPtr info,
                                          boost::asio::io_service &io) {
    uint32_t index = FlowTableIndex(info->ip_saddr, info->ip_daddr,
                                    info->ip_proto, info->sport, info->dport,
                                    info->agent_hdr.cmd_param);
    return new FlowHandler(agent(), info, io, this, index);
}

bool FlowProto::Validate(PktInfo *msg) {
    if (msg->ip == NULL && msg->ip6 == NULL && msg->type != PktType::MESSAGE) {
        if (msg->family == Address::INET || msg->family == Address::INET6) {
            FLOW_TRACE(DetailErr, msg->agent_hdr.cmd_param,
                       msg->agent_hdr.ifindex,
                       msg->agent_hdr.vrf,
                       msg->ip_saddr.to_string(),
                       msg->ip_daddr.to_string(),
                       "Flow : Non-IP packet. Dropping", false);
        } else {
            assert(0);
        }
        return false;
    }
    return true;
}

FlowTable *FlowProto::GetFlowTable(const FlowKey &key,
                                   uint32_t flow_handle) const {
    uint32_t index = FlowTableIndex(key.src_addr, key.dst_addr, key.protocol,
                                    key.src_port, key.dst_port, flow_handle);
    return flow_table_list_[index];
}

bool FlowProto::Enqueue(PktInfoPtr msg) {
    if (Validate(msg.get()) == false) {
        return true;
    }

    FreeBuffer(msg.get());
    EnqueueFlowEvent(new FlowEvent(FlowEvent::VROUTER_FLOW_MSG, msg, NULL, 0));
    return true;
}

void FlowProto::DisableFlowEventQueue(uint32_t index, bool disabled) {
    flow_event_queue_[index]->set_disable(disabled);
    flow_tokenless_queue_[index]->set_disable(disabled);
    flow_delete_queue_[index]->set_disable(disabled);
}

void FlowProto::DisableFlowUpdateQueue(bool disabled) {
    flow_update_queue_.set_disable(disabled);
}

void FlowProto::DisableFlowKSyncQueue(uint32_t index, bool disabled) {
    flow_ksync_queue_[index]->set_disable(disabled);
}

size_t FlowProto::FlowUpdateQueueLength() {
    return flow_update_queue_.Length();
}

void FlowProto::DisableFlowDeleteQueue(uint32_t index, bool disabled) {
    flow_delete_queue_[index]->set_disable(disabled);
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

    std::vector<FlowMgmtManager *>::const_iterator it =
        agent_->pkt()->flow_mgmt_manager_iterator_begin();
    while (it != agent_->pkt()->flow_mgmt_manager_iterator_end()) {
        (*it)->VnFlowCounters(vn, in_count, out_count);
        it++;
    }
}

FlowEntry *FlowProto::Find(const FlowKey &key, uint32_t table_index) const {
    return GetTable(table_index)->Find(key);
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
void FlowProto::EnqueueFlowEvent(FlowEvent *event) {
    FlowEventQueueBase *queue = NULL;
    switch (event->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        PktInfo *info = event->pkt_info().get();
        uint32_t index = FlowTableIndex(info->ip_saddr, info->ip_daddr,
                                        info->ip_proto, info->sport,
                                        info->dport,
                                        info->agent_hdr.cmd_param);
        queue = flow_event_queue_[index];
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        queue = flow_event_queue_[table->table_index()];
        break;
    }

    case FlowEvent::EVICT_FLOW: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        queue = flow_ksync_queue_[table->table_index()];
        break;
    }

    case FlowEvent::FREE_FLOW_REF: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        queue = flow_tokenless_queue_[table->table_index()];
        break;
    }

    case FlowEvent::AUDIT_FLOW: {
        FlowTable *table = GetFlowTable(event->get_flow_key(),
                                        event->flow_handle());
        queue = flow_event_queue_[table->table_index()];
        break;
    }

    case FlowEvent::GROW_FREE_LIST: {
        queue = flow_tokenless_queue_[event->table_index()];
        break;
    }

    case FlowEvent::KSYNC_EVENT: {
        FlowEventKSync *ksync_event = static_cast<FlowEventKSync *>(event);
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (ksync_event->ksync_entry()));
        FlowEntry *flow = ksync_entry->flow_entry().get();
        FlowTable *table = flow->flow_table();
        queue = flow_ksync_queue_[table->table_index()];
        break;
    }

    case FlowEvent::REENTRANT: {
        queue = flow_event_queue_[event->table_index()];
        break;
    }

    case FlowEvent::DELETE_FLOW: {
        FlowEntry *flow = event->flow();
        queue = flow_delete_queue_[flow->flow_table()->table_index()];
        break;
    }

    case FlowEvent::FREE_DBENTRY: {
        queue = flow_tokenless_queue_[0];
        break;
    }

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::RECOMPUTE_FLOW:
    case FlowEvent::REVALUATE_DBENTRY: {
        queue = &flow_update_queue_;
        break;
    }

    case FlowEvent::UNRESOLVED_FLOW_ENTRY: {
        FlowTable *table = event->flow()->flow_table();
        queue = flow_event_queue_[table->table_index()];
        break;
    }
    default:
        assert(0);
        break;
    }

    UpdateStats(event, &stats_);
    queue->Enqueue(event);
    return;
}

bool FlowProto::FlowEventHandler(FlowEvent *req, FlowTable *table) {
    // concurrency check to ensure all request are in right partitions
    assert(table->ConcurrencyCheck(table->flow_task_id()) == true);

    switch (req->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        ProcessProto(req->pkt_info());
        break;
    }

    case FlowEvent::REENTRANT: {
        FlowHandler *handler = new FlowHandler(agent(), req->pkt_info(), io_,
                                               this, table->table_index());
        RunProtoHandler(handler);
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        FlowEntry *flow = req->flow();
        FlowTaskMsg *flow_msg = new FlowTaskMsg(flow);
        PktInfoPtr pkt_info(new PktInfo(PktHandler::FLOW, flow_msg));
        FlowHandler *handler = new FlowHandler(agent(), pkt_info, io_,
                                               this, table->table_index());
        RunProtoHandler(handler);
        break;
    }

    case FlowEvent::FREE_FLOW_REF:
        break;

    case FlowEvent::GROW_FREE_LIST: {
        table->GrowFreeList();
        break;
    }

    case FlowEvent::AUDIT_FLOW: {
        if (table->Find(req->get_flow_key()) == NULL) {
            FlowEntryPtr flow = FlowEntry::Allocate(req->get_flow_key(), table);
            flow->InitAuditFlow(req->flow_handle(), req->gen_id());
            flow->flow_table()->Add(flow.get(), NULL);
        }
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::UNRESOLVED_FLOW_ENTRY: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->ProcessFlowEvent(req, flow,
                                             flow->reverse_flow_entry());
        break;
    }

    case FlowEvent::KSYNC_EVENT: {
        return FlowKSyncMsgHandler(req, table);
    }

    case FlowEvent::FREE_DBENTRY: {
        FlowMgmtManager *mgr = agent()->pkt()->flow_mgmt_manager(
                                   req->table_index());
        mgr->flow_mgmt_dbclient()->FreeDBState(req->db_entry(), req->gen_id());
        break;
    }

    default: {
        assert(0);
        break;
    }
    }

    return true;
}

bool FlowProto::FlowKSyncMsgHandler(FlowEvent *req, FlowTable *table) {
    FlowEventKSync *ksync_event = static_cast<FlowEventKSync *>(req);

    // concurrency check to ensure all request are in right partitions
    assert((table->ConcurrencyCheck(table->flow_ksync_task_id()) == true) ||
           (table->ConcurrencyCheck(table->flow_task_id()) == true));

    switch (req->event()) {
    // Flow was waiting for an index. Index is available now. Retry acquiring
    // the index
    case FlowEvent::KSYNC_EVENT: {
        FlowTableKSyncEntry *ksync_entry =
            (static_cast<FlowTableKSyncEntry *> (ksync_event->ksync_entry()));
        FlowEntry *flow = ksync_entry->flow_entry().get();
        flow->flow_table()->ProcessFlowEvent(req, flow,
                                             flow->reverse_flow_entry());
        break;
    }

    case FlowEvent::EVICT_FLOW: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->ProcessFlowEvent(req, flow,
                                             flow->reverse_flow_entry());
        break;
    }

    default: {
        assert(0);
        break;
    }
    }

    return true;
}

bool FlowProto::FlowUpdateHandler(FlowEvent *req) {
    switch (req->event()) {
    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->ProcessFlowEvent(req, flow,
                                             flow->reverse_flow_entry());
        break;
    }

    case FlowEvent::RECOMPUTE_FLOW: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->ProcessFlowEvent(req, flow,
                                             flow->reverse_flow_entry());
        break;
    }

    default: {
        assert(0);
        break;
    }
    }

    return true;
}

bool FlowProto::FlowDeleteHandler(FlowEvent *req, FlowTable *table) {
    // concurrency check to ensure all request are in right partitions
    // flow-update-queue doenst happen table pointer. Skip concurrency check
    // for flow-update-queue
    if (table) {
        assert(table->ConcurrencyCheck(table->flow_delete_task_id()) == true);
    }

    switch (req->event()) {
    case FlowEvent::DELETE_FLOW: {
        FlowEntry *flow = req->flow();
        table->ProcessFlowEvent(req, flow, flow->reverse_flow_entry());
        break;
    }

    default: {
        assert(0);
        break;
    }
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////////
// Utility methods to generate events
//////////////////////////////////////////////////////////////////////////////
void FlowProto::DeleteFlowRequest(FlowEntry *flow) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::DELETE_FLOW, flow));
    return;
}

void FlowProto::EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle,
                                 uint8_t gen_id, uint8_t evict_gen_id) {
    FlowEvent *event = new FlowEvent(FlowEvent::EVICT_FLOW, flow,
                                     flow_handle, gen_id, evict_gen_id);
    EnqueueFlowEvent(event);
   return;
}

void FlowProto::CreateAuditEntry(const FlowKey &key, uint32_t flow_handle,
                                 uint8_t gen_id) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::AUDIT_FLOW, key, flow_handle,
                                   gen_id));
    return;
}


void FlowProto::GrowFreeListRequest(FlowTable *table) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::GROW_FREE_LIST,
                                   table->table_index()));
    return;
}

void FlowProto::KSyncEventRequest(KSyncEntry *ksync_entry,
                                  KSyncEntry::KSyncEvent event,
                                  uint32_t flow_handle, uint8_t gen_id,
                                  int ksync_error, uint64_t evict_flow_bytes,
                                  uint64_t evict_flow_packets,
                                  int32_t evict_flow_oflow) {
    EnqueueFlowEvent(new FlowEventKSync(ksync_entry, event, flow_handle,
                                        gen_id, ksync_error, evict_flow_bytes,
                                        evict_flow_packets, evict_flow_oflow));
}

void FlowProto::MessageRequest(FlowEntry *flow) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::FLOW_MESSAGE, flow));
    return;
}

// Flow management runs in parallel to flow processing. As a result,
// we need to ensure that last reference for flow will go away from
// kTaskFlowEvent context only. This is ensured by following 2 actions
//
// 1. On return from here reference to the flow is removed which can
//    potentially be last reference. So, enqueue a dummy request to
//    flow-table queue.
// 2. Due to OS scheduling, its possible that the request we are
//    enqueuing completes even before this function is returned. So,
//    drop the reference immediately after allocating the event
void FlowProto::ForceEnqueueFreeFlowReference(FlowEntryPtr &flow) {
    FlowEvent *event = new FlowEvent(FlowEvent::FREE_FLOW_REF,
                                     flow.get());
    flow.reset();
    EnqueueFlowEvent(event);
}

bool FlowProto::EnqueueReentrant(PktInfoPtr msg, uint8_t table_index) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::REENTRANT,
                                   msg, NULL, table_index));
    return true;
}

// Enqueue event to force revaluation of KSync entry
void FlowProto::EnqueueUnResolvedFlowEntry(FlowEntry *flow) {
    FlowEvent *event = new FlowEvent(FlowEvent::UNRESOLVED_FLOW_ENTRY, flow);
    EnqueueFlowEvent(event);
}

// Apply trace-filter for flow. Will not allow true-false transistions.
// That is, if flows are already marked for tracing, action is retained
bool FlowProto::ShouldTrace(const FlowEntry *flow, const FlowEntry *rflow) {
    // Handle case where flow is NULL. It can happen if Update is called
    // and flow is deleted between event-processing and calling
    // FlowTable::Update
    if (flow == NULL)
        return false;

    bool trace = flow->trace();
    if (rflow)
        trace |= rflow->trace();

    if (trace == false) {
        FlowTraceFilter *filter;
        if (flow->key().family == Address::INET) {
            filter = &ipv4_trace_filter_;
        } else {
            filter = &ipv6_trace_filter_;
        }

        trace = filter->Match(&flow->key());
        if (rflow && trace == false) {
            trace = filter->Match(&rflow->key());
        }
    }

    return trace;
}

//////////////////////////////////////////////////////////////////////////////
// Token Management routines
//////////////////////////////////////////////////////////////////////////////
TokenPtr FlowProto::GetToken(FlowEvent::Event event) {
    switch (event) {
    case FlowEvent::VROUTER_FLOW_MSG:
    case FlowEvent::AUDIT_FLOW:
    case FlowEvent::REENTRANT:
        return add_tokens_.GetToken(NULL);
        break;

    case FlowEvent::KSYNC_EVENT:
        return ksync_tokens_.GetToken(NULL);
        break;

    case FlowEvent::FLOW_MESSAGE:
    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::REVALUATE_DBENTRY:
    case FlowEvent::RECOMPUTE_FLOW:
        return update_tokens_.GetToken(NULL);
        break;

    case FlowEvent::DELETE_FLOW:
        return del_tokens_.GetToken(NULL);
        break;

    case FlowEvent::EVICT_FLOW:
    case FlowEvent::INVALID:
        break;

    default:
        assert(0);
        break;
    }

    return add_tokens_.GetToken(NULL);
}

bool FlowProto::TokenCheck(const FlowTokenPool *pool) const {
    return pool->TokenCheck();
}

void FlowProto::TokenAvailable(TokenPool *pool_base) {
    FlowTokenPool *pool = dynamic_cast<FlowTokenPool *>(pool_base);
    if (pool_base == NULL)
        return;

    pool->IncrementRestarts();
    if (pool == &add_tokens_) {
        for (uint32_t i = 0; i < flow_event_queue_.size(); i++) {
            flow_event_queue_[i]->MayBeStartRunner();
        }
    }

    if (pool == &ksync_tokens_) {
        for (uint32_t i = 0; i < flow_event_queue_.size(); i++) {
            flow_ksync_queue_[i]->MayBeStartRunner();
        }
    }

    if (pool == &del_tokens_) {
        for (uint32_t i = 0; i < flow_event_queue_.size(); i++) {
            flow_delete_queue_[i]->MayBeStartRunner();
        }
    }

    if (pool == &update_tokens_) {
        flow_update_queue_.MayBeStartRunner();
    }
}

//////////////////////////////////////////////////////////////////////////////
// Set profile information
//////////////////////////////////////////////////////////////////////////////
void UpdateStats(FlowEvent *req, FlowStats *stats) {
    switch (req->event()) {
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
    case FlowEvent::RECOMPUTE_FLOW:
        stats->recompute_count_++;
        break;
    case FlowEvent::REVALUATE_DBENTRY:
        stats->revaluate_count_++;
        break;
    case FlowEvent::EVICT_FLOW:
        stats->evict_count_++;
        break;
    case FlowEvent::KSYNC_EVENT: {
        stats->vrouter_responses_++;
        FlowEventKSync *ksync_event = static_cast<FlowEventKSync *>(req);
        if (ksync_event->ksync_error())
            stats->vrouter_error_++;
        break;
    }
    default:
        break;
    }
}

static void SetFlowEventQueueStats(Agent *agent,
                                   const FlowEventQueueBase::Queue *queue,
                                   ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->start_count_ = queue->task_starts();
    stats->busy_time_ = queue->busy_time();
    queue->set_measure_busy_time(agent->MeasureQueueDelay());
    if (agent->MeasureQueueDelay()) {
        queue->ClearStats();
    }
}

static void SetFlowMgmtQueueStats(Agent *agent,
                                  const FlowMgmtManager::FlowMgmtQueue *queue,
                                  ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->start_count_ = queue->task_starts();
    stats->busy_time_ = queue->busy_time();
    queue->set_measure_busy_time(agent->MeasureQueueDelay());
    if (agent->MeasureQueueDelay())
        queue->ClearStats();
}

static void SetPktHandlerQueueStats(Agent *agent,
                                    const PktHandler::PktHandlerQueue *queue,
                                    ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->start_count_ = queue->task_starts();
    stats->busy_time_ = queue->busy_time();
    queue->set_measure_busy_time(agent->MeasureQueueDelay());
    if (agent->MeasureQueueDelay())
        queue->ClearStats();
}

void FlowProto::SetProfileData(ProfileData *data) {
    data->flow_.flow_count_ = FlowCount();
    data->flow_.add_count_ = stats_.add_count_;
    data->flow_.del_count_ = stats_.delete_count_;
    data->flow_.audit_count_ = stats_.audit_count_;
    data->flow_.reval_count_ = stats_.revaluate_count_;
    data->flow_.recompute_count_ = stats_.recompute_count_;
    data->flow_.vrouter_responses_ = stats_.vrouter_responses_;
    data->flow_.vrouter_error_ = stats_.vrouter_error_;
    data->flow_.evict_count_ = stats_.evict_count_;

    PktModule *pkt = agent()->pkt();
    std::vector<FlowMgmtManager *> mgr_list = pkt->flow_mgmt_manager_list();

    data->flow_.flow_event_queue_.resize(flow_table_list_.size());
    data->flow_.flow_delete_queue_.resize(flow_table_list_.size());
    data->flow_.flow_tokenless_queue_.resize(flow_table_list_.size());
    data->flow_.flow_ksync_queue_.resize(flow_table_list_.size());
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        SetFlowMgmtQueueStats(agent(), mgr_list[i]->request_queue(),
                              &data->flow_.flow_mgmt_queue_);
        SetFlowEventQueueStats(agent(), flow_event_queue_[i]->queue(),
                               &data->flow_.flow_event_queue_[i]);
        SetFlowEventQueueStats(agent(), flow_delete_queue_[i]->queue(),
                               &data->flow_.flow_delete_queue_[i]);
        SetFlowEventQueueStats(agent(), flow_tokenless_queue_[i]->queue(),
                               &data->flow_.flow_tokenless_queue_[i]);
        SetFlowEventQueueStats(agent(), flow_ksync_queue_[i]->queue(),
                               &data->flow_.flow_ksync_queue_[i]);
    }
    SetFlowEventQueueStats(agent(), flow_update_queue_.queue(),
                           &data->flow_.flow_update_queue_);
    const PktHandler::PktHandlerQueue *pkt_queue =
        pkt->pkt_handler()->work_queue();
    SetPktHandlerQueueStats(agent(), pkt_queue,
                            &data->flow_.pkt_handler_queue_);

    data->flow_.token_stats_.add_tokens_ = add_tokens_.token_count();
    data->flow_.token_stats_.add_failures_ = add_tokens_.failures();
    data->flow_.token_stats_.add_restarts_ = add_tokens_.restarts();
    data->flow_.token_stats_.ksync_tokens_ = ksync_tokens_.token_count();
    data->flow_.token_stats_.ksync_failures_ = ksync_tokens_.failures();
    data->flow_.token_stats_.ksync_restarts_ = ksync_tokens_.restarts();
    data->flow_.token_stats_.update_tokens_ = update_tokens_.token_count();
    data->flow_.token_stats_.update_failures_ = update_tokens_.failures();
    data->flow_.token_stats_.update_restarts_ = update_tokens_.restarts();
    data->flow_.token_stats_.del_tokens_ = del_tokens_.token_count();
    data->flow_.token_stats_.del_failures_ = del_tokens_.failures();
    data->flow_.token_stats_.del_restarts_ = del_tokens_.restarts();
}

bool FlowProto::FlowStatsUpdate() const {
    agent_->stats()->UpdateFlowMinMaxStats(agent_->stats()->flow_created(),
                                           agent_->stats()->added());
    agent_->stats()->UpdateFlowMinMaxStats(agent_->stats()->flow_aged(),
                                           agent_->stats()->deleted());
    return true;
}

void FlowProto::InterfaceFlowCount(const Interface *intf, uint64_t *created,
                                   uint64_t *aged,
                                   uint32_t *active_flows) const {
    *created = 0;
    *aged = 0;
    *active_flows = 0;
    if (intf == NULL)
        return;
    std::vector<FlowMgmtManager *>::const_iterator it =
        agent_->pkt()->flow_mgmt_manager_iterator_begin();
    while (it != agent_->pkt()->flow_mgmt_manager_iterator_end()) {
        (*it)->InterfaceFlowCount(intf, created, aged, active_flows);
        it++;
    }
}
