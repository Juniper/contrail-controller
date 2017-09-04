/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <sandesh/common/flow_types.h>
#include <cmn/agent_factory.h>
#include <vrouter/flow_stats/session_stats_collector.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/tag.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

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
        session_msg_list_(kMaxSessionMsgsPerSend, SessionEndpoint()), session_msg_index_(0),
        instance_id_(instance_id),
        flow_stats_manager_(aging_module), parent_(obj),
        session_task_(NULL),
        current_time_(GetCurrentTime()),
        session_task_starts_(0) {
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
    if (session_endpoint_map_.size() == 0) {
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
        req(new SessionStatsReq(SessionStatsReq::ADD_SESSION, flow,
                                GetCurrentTime()));
    request_queue_.Enqueue(req);
}

void SessionStatsCollector::DeleteEvent(const FlowEntryPtr &flow) {
    boost::shared_ptr<SessionStatsReq>
        req(new SessionStatsReq(SessionStatsReq::DELETE_SESSION, flow,
                                GetCurrentTime()));
    request_queue_.Enqueue(req);
}

void SessionStatsCollector::DispatchSessionMsg(const std::vector<SessionEndpoint> &lst) {
    flow_stats_manager_->UpdateSessionMsgExportStats(1);
    flow_stats_manager_->UpdateSessionSampleExportStats(lst.size());
    SESSION_ENDPOINT_OBJECT_LOG("", SandeshLevel::SYS_INFO, lst);
}

void SessionStatsCollector::EnqueueSessionMsg() {
    session_msg_index_++;
    if (session_msg_index_ == kMaxSessionMsgsPerSend) {
        DispatchSessionMsg(session_msg_list_);
        session_msg_index_ = 0;
    }
}

void SessionStatsCollector::DispatchPendingSessionMsg() {
    if (session_msg_index_ == 0) {
        return;
    }

    vector<SessionEndpoint>::const_iterator first = session_msg_list_.begin();
    vector<SessionEndpoint>::const_iterator last = session_msg_list_.begin() +
                                                    session_msg_index_;
    vector<SessionEndpoint> new_list(first, last);
    DispatchSessionMsg(new_list);
    session_msg_index_ = 0;
}

uint8_t SessionStatsCollector::GetSessionMsgIdx() {
    SessionEndpoint &obj = session_msg_list_[session_msg_index_];
    obj = SessionEndpoint();
    return session_msg_index_;
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
        AddSession(req->flow().get(), req->time());
        break;
    }

    case SessionStatsReq::DELETE_SESSION: {
        DeleteSession(req->flow().get(), req->time());
        break;
    }

    default:
         assert(0);
    }

    return true;
}

bool SessionEndpointKey::IsLess(const SessionEndpointKey &rhs) const {
    if (vmi != rhs.vmi) {
        return vmi < rhs.vmi;
    }
    if (local_vn != rhs.local_vn) {
        return local_vn < rhs.local_vn;
    }
    if (remote_vn != rhs.remote_vn) {
        return remote_vn < rhs.remote_vn;
    }
    if (local_tagset != rhs.local_tagset) {
        return local_tagset < rhs.local_tagset;
    }
    if (remote_tagset != rhs.remote_tagset) {
        return remote_tagset < rhs.remote_tagset;
    }
    if (remote_prefix != rhs.remote_prefix) {
        return remote_prefix < rhs.remote_prefix;
    }
    if (is_client_session != rhs.is_client_session) {
        return is_client_session < rhs.is_client_session;
    }
    return is_si < rhs.is_si;
}

bool SessionAggKey::IsLess(const SessionAggKey &rhs) const {
    if (local_ip != rhs.local_ip) {
        return local_ip < rhs.local_ip;
    }
    if (dst_port != rhs.dst_port) {
        return dst_port < rhs.dst_port;
    }
    return proto < rhs.proto;
}

bool SessionKey::IsLess(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return remote_ip < rhs.remote_ip;
    }
    return src_port < rhs.src_port;
}

bool SessionStatsCollector::GetSessionKey(FlowEntry* fe,
                                        SessionAggKey &session_agg_key,
                                        SessionKey    &session_key,
                                        SessionEndpointKey &session_endpoint_key) {
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

    /*
     *  If it is non local reverse flow then NOP, actual
     *  config will be handled in fwd flow
     */
    if ((!(fe->is_flags_set(FlowEntry::LocalFlow))) &&
        (fe->is_flags_set(FlowEntry::ReverseFlow))) {
        return false;
    }

    const Interface *itf = fe->intf_entry();
    if (!itf) {
        return false;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        return false;
    }

    const VmInterface *vmi = static_cast<const VmInterface *>(itf);
    const string &src_vn = fe->data().source_vn_match;
    const string &dst_vn = fe->data().dest_vn_match;

    session_endpoint_key.vmi = vmi;
    session_endpoint_key.local_tagset = fe->local_tagset();
    session_endpoint_key.remote_tagset = fe->remote_tagset();
    session_endpoint_key.remote_prefix = fe->RemotePrefix();
    /*
     * TBD
     */
    session_endpoint_key.is_si = false;

    if (fe->IsClientFlow()) {
        session_agg_key.local_ip = fe->key().src_addr;
        session_key.remote_ip = fe->key().dst_addr;
        session_endpoint_key.local_vn = src_vn;
        session_endpoint_key.remote_vn = dst_vn;
        session_endpoint_key.is_client_session = true;
    } else if (fe->IsServerFlow()) {
        /*
         *  If it is local flow, then reverse flow
         *  (Ingress + Reverse) will be used to create
         *  the server side session (Egress + Forward)
         */
        if (fe->is_flags_set(FlowEntry::LocalFlow)) {
            session_agg_key.local_ip = fe->key().src_addr;
            session_key.remote_ip = fe->key().dst_addr;
            session_endpoint_key.local_vn = src_vn;
            session_endpoint_key.remote_vn = dst_vn;
        } else {
            session_agg_key.local_ip = fe->key().dst_addr;
            session_key.remote_ip = fe->key().src_addr;
            session_endpoint_key.local_vn = dst_vn;
            session_endpoint_key.remote_vn = src_vn;
        }
        session_endpoint_key.is_client_session = false;
    } else {
        return false;
    }
    session_agg_key.dst_port = fe->key().dst_port;
    session_agg_key.proto = fe->key().protocol;
    session_key.src_port = fe->key().src_port;
    return true;
}

void SessionStatsCollector::AddSession(FlowEntry* fe, uint64_t setup_time) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionFlowStatsInfo session;
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;
    SessionEndpointInfo  session_endpoint_info;
    SessionEndpointKey session_endpoint_key;
    SessionEndpointMap::iterator session_endpoint_map_iter;
    FlowEntry *fe_fwd = fe;
    bool success;

    if (NULL == fe->reverse_flow_entry()) {
        return;
    }

    if (!(fe->is_flags_set(FlowEntry::LocalFlow))) {
        if (fe->is_flags_set(FlowEntry::ReverseFlow)) {
            fe_fwd = fe->reverse_flow_entry();
        }
    }

    if (!fe_fwd) {
        return;
    }

    success = GetSessionKey(fe_fwd, session_agg_key, session_key, session_endpoint_key);
    if (!success) {
        return;
    }

    session.flow = fe_fwd;
    session.setup_time = setup_time;

    session_endpoint_map_iter = session_endpoint_map_.find(
                                            session_endpoint_key);
    if (session_endpoint_map_iter == session_endpoint_map_.end()) {
        session_agg_info.session_map_.insert(make_pair(session_key, session));
        session_endpoint_info.session_agg_map_.insert(
                                make_pair(session_agg_key, session_agg_info));
        session_endpoint_map_.insert(make_pair(session_endpoint_key,
                                                session_endpoint_info));
    } else {
        session_agg_map_iter = session_endpoint_map_iter->
                                        second.session_agg_map_.find(
                                                        session_agg_key);
        if (session_agg_map_iter ==
                session_endpoint_map_iter->second.session_agg_map_.end()) {
            session_agg_info.session_map_.insert(make_pair(session_key, session));
            session_endpoint_map_iter->second.session_agg_map_.insert(
                                make_pair(session_agg_key, session_agg_info));
        } else {
            session_map_iter =
                session_agg_map_iter->second.session_map_.find(session_key);
            if (session_map_iter ==
                    session_agg_map_iter->second.session_map_.end()) {
                session_agg_map_iter->second.session_map_.insert(
                                        make_pair(session_key, session));
            } else {
                assert(session_map_iter->second.flow.get() == fe_fwd);
            }
        }
    }
}

void SessionStatsCollector::DeleteSession(FlowEntry* fe, uint64_t teardown_time) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;
    SessionEndpointInfo  session_endpoint_info;
    SessionEndpointKey session_endpoint_key;
    SessionEndpointMap::iterator session_endpoint_map_iter;
    bool success;

    success = GetSessionKey(fe, session_agg_key, session_key, session_endpoint_key);
    if (!success) {
        return;
    }

    session_endpoint_map_iter = session_endpoint_map_.find(
                                            session_endpoint_key);
    if (session_endpoint_map_iter != session_endpoint_map_.end()) {
        session_agg_map_iter = session_endpoint_map_iter->
                                        second.session_agg_map_.find(
                                                        session_agg_key);
        if (session_agg_map_iter !=
                session_endpoint_map_iter->second.session_agg_map_.end()) {
            session_map_iter =
                session_agg_map_iter->second.session_map_.find(session_key);
            if (session_map_iter !=
                    session_agg_map_iter->second.session_map_.end()) {
                /*
                 * Process the stats collector
                 */
                session_map_iter->second.teardown_time = teardown_time;
                ProcessSessionEndpoint(session_endpoint_map_iter,
                            agent_uve_->agent()->ksync()->ksync_flow_memory(),
                            GetCurrentTime());
                session_agg_map_iter->second.session_map_.erase(session_map_iter);
                if (session_agg_map_iter->second.session_map_.size() == 0) {
                    session_endpoint_map_iter->
                        second.session_agg_map_.erase(session_agg_map_iter);
                    if (session_endpoint_map_iter->second.session_agg_map_.size() == 0) {
                        session_endpoint_map_.erase(session_endpoint_map_iter);
                    }
                }
            }
        }
    }
}

void SessionStatsCollector::FillSessionFlowInfo(FlowEntry *fe,
                                                uint64_t setup_time,
                                                uint64_t teardown_time,
                                                KSyncFlowMemory *ksync_obj,
                                                SessionFlowInfo &flow_info) {
    uint32_t flow_handle = fe->flow_handle();
    uint16_t gen_id = fe->gen_id();
    std::string action_str;
    vr_flow_stats k_stats;
    KFlowData kinfo;

    ksync_obj->GetKFlowStatsAndInfo(fe->key(),
                                    flow_handle,
                                    gen_id, &k_stats, &kinfo);
    flow_info.set_logged_pkts(k_stats.flow_packets);
    flow_info.set_logged_bytes(k_stats.flow_bytes);
    flow_info.set_flow_uuid(fe->uuid());
    flow_info.set_tcp_flags(kinfo.tcp_flags);
    flow_info.set_setup_time(setup_time);
    flow_info.set_teardown_time(teardown_time);
    FlowStatsCollector::GetFlowSandeshActionParams(
                                    fe->data().match_p.action_info,
                                    action_str);
    flow_info.set_action(action_str);
    flow_info.set_sg_rule_uuid(StringToUuid(fe->sg_rule_uuid()));
    flow_info.set_nw_ace_uuid(StringToUuid(fe->nw_ace_uuid()));
    flow_info.set_underlay_source_port(kinfo.underlay_src_port);
    flow_info.set_drop_reason(fe->data().drop_reason);
}

void SessionStatsCollector::FillSessionInfo(SessionPreAggInfo::SessionMap::iterator session_map_iter,
                                            KSyncFlowMemory *ksync_obj,
                                            SessionInfo &session_info,
                                            SessionIpPort &session_key) {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *fe = session_map_iter->second.flow.get();
    FlowEntry *rfe = fe->reverse_flow_entry();
    /*
     * Fill the session Key
     */
    session_key.set_ip(session_map_iter->first.remote_ip);
    session_key.set_port(session_map_iter->first.src_port);
    FillSessionFlowInfo(fe, session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time,
                        ksync_obj, session_info.forward_flow_info);
    if (rfe) {
        FillSessionFlowInfo(rfe, session_map_iter->second.setup_time,
                            session_map_iter->second.teardown_time,
                            ksync_obj, session_info.reverse_flow_info);
    }
    session_info.set_vm(fe->data().vm_cfg_name);
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        session_info.set_other_vrouter_ip(
                boost::asio::ip::address::from_string(rid));
    } else {
        session_info.set_other_vrouter_ip(
                boost::asio::ip::address::from_string(fe->peer_vrouter()));
    }
    session_info.set_underlay_proto(fe->tunnel_type().GetType());
}

void SessionStatsCollector::FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter,
                                               SessionAggInfo &session_agg_info,
                                               SessionIpPortProtocol &session_agg_key) {
    /*
     * Fill the session agg key
     */
    session_agg_key.set_ip(session_agg_map_iter->first.local_ip);
    session_agg_key.set_port(session_agg_map_iter->first.dst_port);
    session_agg_key.set_protocol(session_agg_map_iter->first.proto);
}

void SessionStatsCollector::FillSessionTagInfo(const TagList &list,
                                               SessionEndpoint &session_ep,
                                               bool is_remote) {
    std::set<std::string>   labels;
    TagList::const_iterator it = list.begin();
    while (it != list.end()) {
        uint32_t    ttype = (uint32_t)*it >> TagEntry::kTagTypeBitShift;
        std::string tag_type = TagEntry::GetTypeStr(ttype);
        if (tag_type == "site") {
            if (is_remote) {
                session_ep.set_remote_site(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep.set_site(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "deployment") {
            if (is_remote) {
                session_ep.set_remote_deployment(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep.set_deployment(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "tier") {
            if (is_remote) {
                session_ep.set_remote_tier(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep.set_tier(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "application") {
            if (is_remote) {
                session_ep.set_remote_application(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep.set_application(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if(tag_type == "label") {
            labels.insert(agent_uve_->agent()->tag_table()->TagName(*it));
        }
    }
    if (labels.size() != 0) {
        if (is_remote) {
            session_ep.set_labels(labels);
        } else {
            session_ep.set_remote_labels(labels);
        }
    }

}

void SessionStatsCollector::FillSessionEndpoint(SessionEndpointMap::iterator it,
                                                SessionEndpoint &session_ep) {
    session_ep.set_vmi(UuidToString(it->first.vmi->vmi_cfg_uuid()));
    session_ep.set_vn(it->first.local_vn);
    session_ep.set_remote_vn(it->first.remote_vn);
    session_ep.set_is_client_session(it->first.is_client_session);
    session_ep.set_is_si(it->first.is_si);
    session_ep.set_remote_prefix(it->first.remote_prefix);
    FillSessionTagInfo(it->first.local_tagset, session_ep, false);
    FillSessionTagInfo(it->first.remote_tagset, session_ep, true);
}

uint32_t SessionStatsCollector::ProcessSessionEndpoint(SessionEndpointMap::iterator &it,
                                         KSyncFlowMemory *ksync_obj,
                                         uint64_t curr_time) {
    uint32_t count = 1;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;

    SessionEndpoint &session_ep =  session_msg_list_[GetSessionMsgIdx()];

    session_agg_map_iter = it->second.session_agg_map_.begin();
    while (session_agg_map_iter != it->second.session_agg_map_.end()) {
        session_map_iter = session_agg_map_iter->second.session_map_.begin();
        while (session_map_iter != session_agg_map_iter->second.session_map_.end()) {
            FillSessionInfo(session_map_iter, ksync_obj, session_info, session_key);
            session_agg_info.sessionMap.insert(make_pair(session_key, session_info));
            session_map_iter++;
        }
        FillSessionAggInfo(session_agg_map_iter, session_agg_info, session_agg_key);
        session_ep.sess_agg_info.insert(make_pair(session_agg_key, session_agg_info));
        session_agg_map_iter++;
    }
    FillSessionEndpoint(it, session_ep);

    EnqueueSessionMsg();
    return count;
}

uint32_t SessionStatsCollector::RunSessionStatsCollect() {
    SessionEndpointMap::iterator it;
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->ksync_flow_memory();
    uint64_t curr_time = GetCurrentTime();
    uint32_t count = 0;

    it = session_endpoint_map_.begin();
    while (it != session_endpoint_map_.end()) {
        count += ProcessSessionEndpoint(it, ksync_obj, curr_time);
        it++;
    }
    return count;
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
    return ssc_->RunSessionStatsCollect();
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
