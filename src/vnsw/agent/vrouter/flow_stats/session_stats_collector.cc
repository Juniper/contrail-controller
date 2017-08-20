/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <bitset>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>
#include <base/string_util.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/global_vrouter.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <uve/agent_uve.h>
#include <vrouter/flow_stats/session_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <uve/interface_uve_stats_table.h>
#include <uve/vrouter_uve_entry.h>
#include <algorithm>
#include <pkt/flow_proto.h>
#include <pkt/flow_mgmt.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_types.h>

bool session_debug_ = false;
SessionStatsCollector::SessionStatsCollector(boost::asio::io_service &io,
                                       AgentUveBase *uve,
                                       uint32_t instance_id,
                                       FlowStatsManager *aging_module,
                                       SessionStatsCollectorObject *obj) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       (kTaskSessionStatsCollector), instance_id,
                       io, kSessionStatsTimerInterval, "Session stats collector"),
        agent_uve_(uve),
        task_id_(uve->agent()->task_scheduler()->GetTaskId
                 (kTaskSessionStatsCollector)),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId(kTaskSessionStatsCollector),
                       instance_id,
                       boost::bind(&SessionStatsCollector::RequestHandler, 
                                   this, _1)),
        instance_id_(instance_id),
        flow_stats_manager_(aging_module), parent_(obj),
        session_task_(NULL),
        current_time_(GetCurrentTime()), 
        session_task_starts_(0) {
        deleted_ = false;
        request_queue_.set_name("Session stats collector");
        request_queue_.set_measure_busy_time
            (agent_uve_->agent()->MeasureQueueDelay());
        request_queue_.SetEntryCallback
            (boost::bind(&SessionStatsCollector::RequestHandlerEntry, this));
        request_queue_.SetExitCallback
            (boost::bind(&SessionStatsCollector::RequestHandlerExit, this, _1));
}

SessionStatsCollector::~SessionStatsCollector() {
    flow_stats_manager_->FreeIndex(instance_id_);
}

uint64_t SessionStatsCollector::GetCurrentTime() {
    return UTCTimestampUsec();
}

void SessionStatsCollector::Shutdown() {
    StatsCollector::Shutdown();
    request_queue_.Shutdown();
}

bool SessionStatsCollector::Run() {
    if ((client_session_agg_map_.size() == 0) &&
        (server_session_agg_map_.size() == 0)) {
        return true;
     }

    // Start task to scan the entries
    if (session_task_ == NULL) {
        session_task_starts_++;

        if (session_debug_) {
            LOG(DEBUG,
                UTCUsecToString(ClockMonotonicUsec())
                << " SessionTasks Num " << session_task_starts_
                << " Request count " << request_queue_.Length());
        }
        session_task_ = new SessionTask(this);
        agent_uve_->agent()->task_scheduler()->Enqueue(session_task_);
    }
    return true;
}
  
/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void SessionStatsCollector::AddEvent(const FlowEntryPtr &flow) {
    boost::shared_ptr<SessionStatsReq>
        req(new SessionStatsReq(SessionStatsReq::ADD_SESSION, flow));
    request_queue_.Enqueue(req);
}

void SessionStatsCollector::DeleteEvent(const FlowEntryPtr &flow) {
    boost::shared_ptr<SessionStatsReq>
        req(new SessionStatsReq(SessionStatsReq::DELETE_SESSION, flow));
    request_queue_.Enqueue(req);
}

bool SessionStatsCollector::RequestHandlerEntry() {
    current_time_ = GetCurrentTime();
    return true;
}

void SessionStatsCollector::RequestHandlerExit(bool done) {
}

bool SessionStatsCollector::RequestHandler(boost::shared_ptr<SessionStatsReq> req) {
    FlowEntry *flow = req->flow().get();
    FlowEntry *rflow = req->flow()->reverse_flow_entry();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);

    switch (req->event()) {
    case SessionStatsReq::ADD_SESSION: {
        AddSession(req->flow().get());
        break;
    }

    case SessionStatsReq::DELETE_SESSION: {
        DeleteSession(req->flow().get());
        break;
    }

    default:
         assert(0);
    }

    return true;
}

bool SessionAggKey::IsLess (const SessionAggKey &rhs) const {
    if (local_ip != rhs.local_ip) {
        return local_ip < rhs.local_ip;
    }
    if (dst_port != rhs.dst_port) {
        return dst_port < rhs.dst_port;
    }
    return proto < rhs.proto;
}

bool SessionKey::IsLess (const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return remote_ip < rhs.remote_ip;
    }
    return src_port < rhs.src_port;
}

void SessionStatsCollector::GetSessionKey (FlowEntry* fe,
                                        SessionAggKey &session_agg_key,
                                        SessionKey    &session_key,
                                        SessionAggMap **session_agg_map) {
    FlowEntry *fe_fwd = fe;
    *session_agg_map = NULL;

    /*
     * For non local flows always get the key for forward flow entry
     * If vms are in same compute node (local route)
     * Vrouter A  (Client)                <==>     Vrouter B (Server)
     * A->B (Ingress + Forwarding + Local)
     * B->A (Ingress + Reverse + Local)
     *
     * If vms are in different compute nodes
     * Vrouter A (Client)               <==>     Vrouter B (Server)
     * A->B (Ingress + Forwarding)             A->B (Egresss + Forward)
     * B->A (Egress + Reverse)                 B->A (Ingress + Reverse)
     */
    if (!(fe->is_flags_set(FlowEntry::LocalFlow))) {
        if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
            fe_fwd = fe->reverse_flow_entry();
        }
    }

    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        if ((fe->is_flags_set(FlowEntry::IngressDir)) &&
            (!(fe->is_flags_set(FlowEntry::ReverseFlow)))) {
             session_agg_key.local_ip = fe_fwd->key().src_addr;
             session_key.remote_ip = fe_fwd->key().dst_addr;
             *session_agg_map = &client_session_agg_map_;
        } else if ((fe->is_flags_set(FlowEntry::IngressDir)) &&
                   (fe->is_flags_set(FlowEntry::ReverseFlow))) {
            session_agg_key.local_ip = fe_fwd->key().dst_addr;
            session_key.remote_ip = fe_fwd->key().src_addr;
            *session_agg_map = &server_session_agg_map_;
        }
        else {
            return;
        }

    } else if (((fe->is_flags_set(FlowEntry::IngressDir)) &&
         (!(fe->is_flags_set(FlowEntry::ReverseFlow)))) ||
        ((!(fe->is_flags_set(FlowEntry::IngressDir))) &&
         (fe->is_flags_set(FlowEntry::ReverseFlow)))) {
        session_agg_key.local_ip = fe_fwd->key().src_addr;
        session_key.remote_ip = fe_fwd->key().dst_addr;
        *session_agg_map = &client_session_agg_map_;
    }else if (((!(fe->is_flags_set(FlowEntry::IngressDir))) &&
         (!(fe->is_flags_set(FlowEntry::ReverseFlow)))) ||
        ((fe->is_flags_set(FlowEntry::IngressDir)) &&
         (fe->is_flags_set(FlowEntry::ReverseFlow)))) {
        session_agg_key.local_ip = fe_fwd->key().dst_addr;
        session_key.remote_ip = fe_fwd->key().src_addr;
        *session_agg_map = &server_session_agg_map_;
    } else {
        return;
    }
    session_agg_key.dst_port = fe_fwd->key().dst_port;
    session_agg_key.proto = fe_fwd->key().protocol;
    session_key.src_port = fe_fwd->key().src_port;
    return;

}
void SessionStatsCollector::AddSession(FlowEntry* fe) {
    SessionAggKey session_agg_key;
    SessionAggMap *session_agg_map = NULL;
    SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    if (NULL == fe->reverse_flow_entry()) {
        return;
    }

    GetSessionKey(fe, session_agg_key, session_key, &session_agg_map);
    if (NULL == session_agg_map) {
        return;
    }

    session_agg_map_iter = session_agg_map->find(session_agg_key);
    if (session_agg_map_iter == session_agg_map->end()) {
        session_agg_info.session_map_.insert(make_pair(session_key, fe));
        session_agg_map->insert(make_pair(session_agg_key, session_agg_info));
    } else {
        session_map_iter =
            session_agg_map_iter->second.session_map_.find(session_key);
        if (session_map_iter ==
                session_agg_map_iter->second.session_map_.end()) {
            session_agg_map_iter->second.session_map_.insert(make_pair(session_key, fe));
        }
    }
}

void SessionStatsCollector::DeleteSession(FlowEntry* fe) {
    SessionAggKey session_agg_key;
    SessionAggMap *session_agg_map = NULL;
    SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    /*
     *  If it is non local reverse flow then NOP, actual
     *  delete will be handled in fwd flow
     */
    if ((!(fe->is_flags_set(FlowEntry::LocalFlow))) && 
        (fe->is_flags_set(FlowEntry::ReverseFlow))) {
        return;
    }

    GetSessionKey(fe, session_agg_key, session_key, &session_agg_map);
    if (NULL == session_agg_map) {
        return;
    }

    session_agg_map_iter = session_agg_map->find(session_agg_key);
    if (session_agg_map_iter == session_agg_map->end()) {
        /*
         * Check the reverse flow, if any
         */
        if (NULL != fe->reverse_flow_entry()) {
            FlowEntry* rfe = fe->reverse_flow_entry();
            GetSessionKey(rfe, session_agg_key, session_key, &session_agg_map);
            if (NULL == session_agg_map) {
                return;
            }
            session_agg_map_iter = session_agg_map->find(session_agg_key);
        }
    }

    if (session_agg_map_iter != session_agg_map->end()) {
        session_map_iter =
            session_agg_map_iter->second.session_map_.find(session_key);
        if (session_map_iter !=
                session_agg_map_iter->second.session_map_.end()) {
            session_agg_map_iter->second.session_map_.erase(session_map_iter);
            if (session_agg_map_iter->second.session_map_.size() == 0) {
                session_agg_map->erase(session_agg_map_iter);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
//TBD

/////////////////////////////////////////////////////////////////////////////
// Session Stats task
/////////////////////////////////////////////////////////////////////////////
SessionStatsCollector::SessionTask::SessionTask(SessionStatsCollector *ssc) :
    Task(ssc->task_id(), ssc->instance_id()), ssc_(ssc) {
}

SessionStatsCollector::SessionTask::~SessionTask() {
}

std::string SessionStatsCollector::SessionTask::Description() const {
    return "Session Stats Collector Task";
}

bool SessionStatsCollector::SessionTask::Run() {
    return true;
}
/////////////////////////////////////////////////////////////////////////////
// SessionStatsCollectorObject methods
/////////////////////////////////////////////////////////////////////////////
SessionStatsCollectorObject::SessionStatsCollectorObject(Agent *agent,
                                                   FlowStatsManager *mgr) {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        uint32_t instance_id = mgr->AllocateIndex();
        collectors[i].reset(
            AgentObjectFactory::Create<SessionStatsCollector>(
                *(agent->event_manager()->io_service()),
                agent->uve(), instance_id, mgr, this));
    }
}

SessionStatsCollector* SessionStatsCollectorObject::GetCollector(uint8_t idx) const {
    if (idx >= 0 && idx < kMaxSessionCollectors) {
        return collectors[idx].get();
    }
    return NULL;
}

void SessionStatsCollectorObject::SetExpiryTime(int time) {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        collectors[i]->set_expiry_time(time);
    }
}

int SessionStatsCollectorObject::GetExpiryTime() const {
    /* Same expiry time would be configured for all the collectors. Pick value
     * from any one of them */
    return collectors[0]->expiry_time();
}

void SessionStatsCollectorObject::MarkDelete() {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        collectors[i]->set_deleted(true);
    }
}

void SessionStatsCollectorObject::ClearDelete() {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        collectors[i]->set_deleted(false);
    }
}

bool SessionStatsCollectorObject::IsDeleted() const {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        if (!collectors[i]->deleted()) {
            return false;
        }
    }
    return true;
}

bool SessionStatsCollectorObject::CanDelete() const {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        if (collectors[i]->client_session_agg_map_.size() != 0 ||
            collectors[i]->server_session_agg_map_.size() != 0 ||
            collectors[i]->request_queue_.IsQueueEmpty() == false) {
            return false;
        }
    }
    return true;
}

SessionStatsCollector* SessionStatsCollectorObject::FlowToCollector
    (const FlowEntry *flow) {
    uint8_t idx = 0;
    FlowTable *table = flow->flow_table();
    if (table) {
        idx = table->table_index() % kMaxSessionCollectors;
    }
    return collectors[idx].get();
}

void SessionStatsCollectorObject::Shutdown() {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        collectors[i]->Shutdown();
        collectors[i].reset();
    }
}

size_t SessionStatsCollectorObject::Size() const {
    size_t size = 0;
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        size += collectors[i]->Size();
    }
    return size;
}
