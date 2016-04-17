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

static void UpdateStats(FlowEvent::Event event, FlowStats *stats);

FlowProto::FlowProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, kTaskFlowEvent, PktHandler::FLOW, io),
    flow_update_queue_(agent->task_scheduler()->GetTaskId(kTaskFlowUpdate), 0,
                       boost::bind(&FlowProto::FlowEventHandler, this, _1,
                                   static_cast<FlowTable *>(NULL))),
    use_vrouter_hash_(false), stats_() {
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
        char name[128];
        sprintf(name, "Flow Event Queue-%d", i);
        FlowEventQueue *queue =
            new FlowEventQueue(task_id, i, boost::bind
                               (&FlowProto::FlowEventHandler, this,
                                _1, flow_table_list_[i]));
        queue->set_name(name);
        flow_event_queue_.push_back(queue);

        sprintf(name, "Flow Delete Queue-%d", i);
        queue = new FlowEventQueue(task_id, i,
                                   boost::bind(&FlowProto::FlowEventHandler,
                                               this, _1, flow_table_list_[i]));
        queue->set_name(name);
        flow_delete_queue_.push_back(queue);
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
    STLDeleteValues(&flow_delete_queue_);
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
        flow_delete_queue_[i]->Shutdown();
    }
    flow_update_queue_.Shutdown();
}

static std::size_t HashCombine(std::size_t hash, uint64_t val) {
    boost::hash_combine(hash, val);
    return hash;
}

static std::size_t HashIp(const IpAddress &ip, std::size_t hash) {
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
        hash = HashIp(sip, hash);
        hash = HashIp(dip, hash);
    } else {
        hash = HashIp(dip, hash);
        hash = HashIp(sip, hash);
    }

    if (sport < dport) {
        HashCombine(hash, sport);
        HashCombine(hash, dport);
    } else {
        HashCombine(hash, dport);
        HashCombine(hash, sport);
    }
    HashCombine(hash, proto);
    return (hash % (flow_event_queue_.size()));
}

FlowHandler *FlowProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    uint32_t index = FlowTableIndex(info->ip_saddr, info->ip_daddr,
                                    info->ip_proto, info->sport, info->dport,
                                    info->agent_hdr.cmd_param);
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

FlowTable *FlowProto::GetFlowTable(const FlowKey &key,
                                   uint32_t flow_handle) const {
    uint32_t index = FlowTableIndex(key.src_addr, key.dst_addr, key.protocol,
                                    key.src_port, key.dst_port, flow_handle);
    return flow_table_list_[index];
}

bool FlowProto::Enqueue(boost::shared_ptr<PktInfo> msg) {
    if (Validate(msg.get()) == false) {
        return true;
    }
    EnqueueFlowEvent(new FlowEvent(FlowEvent::VROUTER_FLOW_MSG, msg));
    return true;
}

void FlowProto::DisableFlowEventQueue(uint32_t index, bool disabled) {
    flow_event_queue_[index]->set_disable(disabled);
    flow_delete_queue_[index]->set_disable(disabled);
}

void FlowProto::DisableFlowUpdateQueue(bool disabled) {
    flow_update_queue_.set_disable(disabled);
}

size_t FlowProto::FlowUpdateQueueLength() {
    return flow_update_queue_.Length();
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
    // Keep UpdateStats in-sync on add of new events
    UpdateStats(event->event(), &stats_);
    switch (event->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        PktInfo *info = event->pkt_info().get();
        uint32_t index = FlowTableIndex(info->ip_saddr, info->ip_daddr,
                                        info->ip_proto, info->sport,
                                        info->dport,
                                        info->agent_hdr.cmd_param);
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
        FlowTable *table = GetTable(event->table_index());
        flow_delete_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::EVICT_FLOW:
    case FlowEvent::FREE_FLOW_REF: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::FREE_DBENTRY: {
        flow_update_queue_.Enqueue(event);
        break;
    }

    case FlowEvent::REVALUATE_FLOW: {
        FlowEntry *flow = event->flow();
        FlowTable *table = flow->flow_table();
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        flow_update_queue_.Enqueue(event);
        break;
    }

    case FlowEvent::AUDIT_FLOW: {
        FlowTable *table = GetFlowTable(event->get_flow_key(),
                                        event->flow_handle());
        flow_event_queue_[table->table_index()]->Enqueue(event);
        break;
    }

    case FlowEvent::GROW_FREE_LIST: {
        FlowTable *table = GetTable(event->table_index());
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

    case FlowEvent::REENTRANT: {
        uint32_t index = event->table_index();
        flow_event_queue_[index]->Enqueue(event);
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
    // concurrency check to ensure all request are in right partitions
    // flow-update-queue doenst happen table pointer. Skip concurrency check
    // for flow-update-queue
    if (table) {
        assert(table->ConcurrencyCheck() == true);
    }

    switch (req->event()) {
    case FlowEvent::VROUTER_FLOW_MSG: {
        // packet parsing is not done, invoke the same here
        uint8_t *pkt = req->pkt_info()->packet_buffer()->data();
        PktInfoPtr info = req->pkt_info();
        PktHandler::PktModuleName mod =
            agent()->pkt()->pkt_handler()->ParseFlowPacket(info, pkt);
        // if packet wasnt for flow module, it would've got enqueued to the
        // correct module in the above call. Nothing else to do.
        if (mod != PktHandler::FLOW) {
            break;
        }
        FreeBuffer(info.get());
        ProcessProto(req->pkt_info());
        break;
    }

    case FlowEvent::REENTRANT:
    case FlowEvent::FLOW_MESSAGE: {
        FlowHandler *handler = new FlowHandler(agent(), req->pkt_info(), io_,
                                               this, table->table_index());
        RunProtoHandler(handler);
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
    case FlowEvent::REVALUATE_DBENTRY: {
        FlowEntry *flow = req->flow();
        flow->flow_table()->FlowResponseHandler(req);
        break;
    }

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

    case FlowEvent::DELETE_FLOW: {
        table->ProcessFlowEvent(req);
        break;
    }

    // Check if flow-handle changed. This can happen if vrouter tries to
    // setup the flow which was evicted earlier
    case FlowEvent::EVICT_FLOW:
    // Flow was waiting for an index. Index is available now. Retry acquiring
    // the index
    case FlowEvent::FLOW_HANDLE_UPDATE:
    case FlowEvent::REVALUATE_FLOW:
    case FlowEvent::KSYNC_EVENT:
    case FlowEvent::KSYNC_VROUTER_ERROR: {
        table->ProcessFlowEvent(req);
        break;
    }

    default: {
        assert(0);
        break;
    }
    }

    return true;
}

void FlowProto::DeleteFlowRequest(const FlowKey &flow_key, bool del_rev_flow,
                                  uint32_t table_index) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::DELETE_FLOW, flow_key,
                                   del_rev_flow, table_index));
    return;
}

void FlowProto::EvictFlowRequest(FlowEntry *flow, uint32_t flow_handle,
                                 uint8_t gen_id) {
    FlowEvent *event = new FlowEvent(FlowEvent::EVICT_FLOW, flow,
                                     flow_handle, gen_id);
    EnqueueFlowEvent(event);
   return;
}

void FlowProto::CreateAuditEntry(const FlowKey &key, uint32_t flow_handle,
                                 uint8_t gen_id) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::AUDIT_FLOW, key, flow_handle,
                                   gen_id));
    return;
}


void FlowProto::GrowFreeListRequest(const FlowKey &key, FlowTable *table) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::GROW_FREE_LIST, key, false,
                                   table->table_index()));
    return;
}

void FlowProto::KSyncEventRequest(KSyncEntry *ksync_entry,
                                  KSyncEntry::KSyncEvent event) {
    EnqueueFlowEvent(new FlowEvent(ksync_entry, event));
    return;
}

void FlowProto::KSyncFlowHandleRequest(KSyncEntry *ksync_entry,
                                       uint32_t flow_handle,
                                       uint8_t gen_id) {
    EnqueueFlowEvent(new FlowEvent(ksync_entry, flow_handle, gen_id));
    return;
}

void FlowProto::KSyncFlowErrorRequest(KSyncEntry *ksync_entry, int error) {
    FlowEvent *event = new FlowEvent(ksync_entry);
    event->set_ksync_error(error);
    EnqueueFlowEvent(event);
    return;
}

void FlowProto::MessageRequest(InterTaskMsg *msg) {
    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(PktHandler::FLOW, msg));
    FreeBuffer(pkt_info.get());
    EnqueueFlowEvent(new FlowEvent(FlowEvent::FLOW_MESSAGE, pkt_info));
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

bool FlowProto::EnqueueReentrant(boost::shared_ptr<PktInfo> msg,
                                 uint8_t table_index) {
    EnqueueFlowEvent(new FlowEvent(FlowEvent::REENTRANT,
                                   msg, table_index));
    return true;
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

static void SetFlowEventQueueStats(const FlowProto::FlowEventQueue *queue,
                                   ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->task_start_count_ = queue->task_starts();
}

static void SetFlowMgmtQueueStats(const FlowMgmtManager::FlowMgmtQueue *queue,
                                  ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->task_start_count_ = queue->task_starts();
}

static void SetPktHandlerQueueStats(const PktHandler::PktHandlerQueue *queue,
                                    ProfileData::WorkQueueStats *stats) {
    stats->name_ = queue->Description();
    stats->queue_count_ = queue->Length();
    stats->enqueue_count_ = queue->NumEnqueues();
    stats->dequeue_count_ = queue->NumDequeues();
    stats->max_queue_count_ = queue->max_queue_len();
    stats->task_start_count_ = queue->task_starts();
}

void FlowProto::SetProfileData(ProfileData *data) {
    data->flow_.flow_count_ = FlowCount();
    data->flow_.add_count_ = stats_.add_count_;
    data->flow_.del_count_ = stats_.delete_count_;
    data->flow_.audit_count_ = stats_.audit_count_;
    data->flow_.reval_count_ = stats_.revaluate_count_;
    data->flow_.handle_update_ = stats_.handle_update_;
    data->flow_.vrouter_error_ = stats_.vrouter_error_;

    PktModule *pkt = agent()->pkt();
    const FlowMgmtManager::FlowMgmtQueue *flow_mgmt =
        pkt->flow_mgmt_manager()->request_queue();
    SetFlowMgmtQueueStats(flow_mgmt, &data->flow_.flow_mgmt_queue_);

    data->flow_.flow_event_queue_.resize(flow_table_list_.size());
    data->flow_.flow_delete_queue_.resize(flow_table_list_.size());
    for (uint16_t i = 0; i < flow_table_list_.size(); i++) {
        SetFlowEventQueueStats(flow_event_queue_[i],
                               &data->flow_.flow_event_queue_[i]);
        SetFlowEventQueueStats(flow_delete_queue_[i],
                               &data->flow_.flow_delete_queue_[i]);
    }
    SetFlowEventQueueStats(&flow_update_queue_, &data->flow_.flow_update_queue_);
    const PktHandler::PktHandlerQueue *pkt_queue =
        pkt->pkt_handler()->work_queue();
    SetPktHandlerQueueStats(pkt_queue, &data->flow_.pkt_handler_queue_);
}
