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
        session_ep_to_visit_(0),
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
        timers_per_scan_ = TimersPerSessionScan();
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

    // Update number of session to visit in flow.
    UpdateSessionEpToVisit();

    // Start task to scan the entries
    if (session_task_ == NULL) {
        session_task_starts_++;

        if (session_debug_) {
            LOG(DEBUG,
                UTCUsecToString(ClockMonotonicUsec())
                << " SessionTasks Num " << session_task_starts_
                << " session_ep visited " << session_ep_visited_
                << " Request count " << request_queue_.Length());
        }
        session_ep_visited_ = 0;
        session_task_ = new SessionTask(this);
        agent_uve_->agent()->task_scheduler()->Enqueue(session_task_);
    }
    return true;
}

// We want to scan the session endpoint table every 25% of session scan time.
// Compute number of timer fires needed to scan the table once.
uint32_t SessionStatsCollector::TimersPerSessionScan() {
    uint64_t scan_time_millisec;

    // Convert scan-time configured in micro-sec to millisecond
    scan_time_millisec = SessionScanTime / 1000;

    // Compute time in which we must scan the complete table to honor the
    scan_time_millisec = (scan_time_millisec * kSessionScanTime) / 100;

    // Enforce min value on scan-time
    if (scan_time_millisec < kSessionStatsTimerInterval) {
        scan_time_millisec = kSessionStatsTimerInterval;
    }

    // Number of timer fires needed to scan table once
    return scan_time_millisec / kSessionStatsTimerInterval;
}

// Update session_ep_to_visit_ based on total session_endpoints
// Timer fires every kSessionScanTime. Its possible that we may not have visited
// all entries by the time next timer fires. So, keep accumulating the number
// of entries to visit into session_ep_to_visit_
//
// A lower-bound and an upper-bound are enforced on session_ep_to_visit_
void SessionStatsCollector::UpdateSessionEpToVisit() {
    // Compute number of session_ep to visit per scan-time
    uint32_t count = session_endpoint_map_.size();
    uint32_t entries = count / timers_per_scan_;

    // Update number of entries to visit in flow.
    // The scan for previous timer may still be in progress. So, accmulate
    // number of entries to visit
    session_ep_to_visit_ += entries;

    // Cap number of entries to visit to 25% of table
    if (session_ep_to_visit_ > ((count * kSessionScanTime)/100))
        session_ep_to_visit_ = (count * kSessionScanTime)/100;

    // Apply lower-limit
    if (session_ep_to_visit_ < kMinSessionsPerTimer)
        session_ep_to_visit_ = kMinSessionsPerTimer;

    return;
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
    FlowEntry *flow = req->flow();
    FlowEntry *rflow = req->reverse_flow();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);

    switch (req->event()) {
    case SessionStatsReq::ADD_SESSION: {
        AddSession(req->flow(), req->time());
        break;
    }

    case SessionStatsReq::DELETE_SESSION: {
        DeleteSession(req->flow(), req->time());
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

void SessionStatsCollector::UpdateSessionFlowStatsInfo(FlowEntry* fe,
                                        SessionFlowStatsInfo &session_flow) {
    session_flow.flow = fe;
    session_flow.gen_id= fe->gen_id();
    session_flow.flow_handle = fe->flow_handle();
    session_flow.uuid = fe->uuid();
}

void SessionStatsCollector::UpdateSessionStatsInfo(FlowEntry* fe,
                                                   uint64_t setup_time,
                                                   SessionStatsInfo &session) {
    FlowEntry *rfe = fe->reverse_flow_entry();

    session.setup_time = setup_time;
    UpdateSessionFlowStatsInfo(fe, session.fwd_flow);
    UpdateSessionFlowStatsInfo(rfe, session.rev_flow);
}

void SessionStatsCollector::AddSession(FlowEntry* fe, uint64_t setup_time) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info = {};
    SessionStatsInfo session = {};
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;
    SessionEndpointInfo  session_endpoint_info = {};
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

    UpdateSessionStatsInfo(fe_fwd, setup_time, session);

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
                if (session_map_iter->second.fwd_flow.uuid  != fe_fwd->uuid()) {
                    /*
                     * Delete the existing session and send the stats
                     */
                    DeleteSession(session_map_iter->second.fwd_flow.flow.get(),
                                  GetCurrentTime());
                    /*
                     * Add the newer session
                     */
                    AddSession(fe_fwd, setup_time);
                }
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
                            GetCurrentTime(),
                            true);
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

uint64_t SessionStatsCollector::GetUpdatedSessionFlowBytes(uint64_t info_bytes,
                                                    uint64_t k_flow_bytes) {
    uint64_t oflow_bytes = 0xffff000000000000ULL & info_bytes;
    uint64_t old_bytes = 0x0000ffffffffffffULL & info_bytes;
    if (old_bytes > k_flow_bytes) {
        oflow_bytes += 0x0001000000000000ULL;
    }
    return (oflow_bytes |= k_flow_bytes);
}

uint64_t SessionStatsCollector::GetUpdatedSessionFlowPackets(
                                                    uint64_t info_packets,
                                                    uint64_t k_flow_pkts) {
    uint64_t oflow_pkts = 0xffffff0000000000ULL & info_packets;
    uint64_t old_pkts = 0x000000ffffffffffULL & info_packets;
    if (old_pkts > k_flow_pkts) {
        oflow_pkts += 0x0000010000000000ULL;
    }
    return (oflow_pkts |= k_flow_pkts);
}

void SessionStatsCollector::FillSessionFlowStats(SessionFlowStatsInfo &session_flow,
                                                 KSyncFlowMemory *ksync_obj,
                                                 SessionFlowInfo &flow_info) {
    FlowEntry *fe = session_flow.flow.get();
    uint32_t flow_handle = session_flow.flow_handle;
    uint16_t gen_id = session_flow.gen_id;
    vr_flow_stats k_stats;
    KFlowData kinfo;
    uint64_t k_bytes, bytes, total_bytes, diff_bytes = 0;
    uint64_t k_packets, total_packets, diff_packets = 0;

    ksync_obj->GetKFlowStatsAndInfo(fe->key(),
                                    flow_handle,
                                    gen_id, &k_stats, &kinfo);
    flow_info.set_tcp_flags(kinfo.tcp_flags);
    flow_info.set_underlay_source_port(kinfo.underlay_src_port);

    k_bytes = FlowStatsCollector::GetFlowStats(k_stats.flow_bytes_oflow,
                          k_stats.flow_bytes);
    k_packets = FlowStatsCollector::GetFlowStats(k_stats.flow_packets_oflow,
                          k_stats.flow_packets);

    bytes = 0x0000ffffffffffffULL & session_flow.total_bytes;

    if (bytes != k_bytes) {
        total_bytes = GetUpdatedSessionFlowBytes(session_flow.total_bytes,
                                                 k_bytes);
        total_packets = GetUpdatedSessionFlowPackets(session_flow.total_packets,
                                                   k_packets);
        diff_bytes = total_bytes - session_flow.total_bytes;
        diff_packets = total_packets - session_flow.total_packets;
        session_flow.total_bytes = total_bytes;
        session_flow.total_packets = total_packets;
        flow_info.set_logged_pkts(diff_packets);
        flow_info.set_logged_bytes(diff_bytes);
    }
}

void SessionStatsCollector::FillSessionFlowInfo(SessionFlowStatsInfo &session_flow,
                                                uint64_t setup_time,
                                                uint64_t teardown_time,
                                                KSyncFlowMemory *ksync_obj,
                                                SessionFlowInfo &flow_info) {
    FlowEntry *fe = session_flow.flow.get();
    std::string action_str;

    FillSessionFlowStats(session_flow, ksync_obj, flow_info);
    flow_info.set_flow_uuid(session_flow.uuid);
    flow_info.set_setup_time(setup_time);
    flow_info.set_teardown_time(teardown_time);
    FlowStatsCollector::GetFlowSandeshActionParams(
                                    fe->data().match_p.action_info,
                                    action_str);
    flow_info.set_action(action_str);
    flow_info.set_sg_rule_uuid(StringToUuid(fe->sg_rule_uuid()));
    flow_info.set_nw_ace_uuid(StringToUuid(fe->nw_ace_uuid()));
    flow_info.set_drop_reason(fe->data().drop_reason);
}

void SessionStatsCollector::FillSessionInfo(SessionPreAggInfo::SessionMap::iterator session_map_iter,
                                            KSyncFlowMemory *ksync_obj,
                                            SessionInfo &session_info,
                                            SessionIpPort &session_key,
                                            bool from_config) {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    boost::system::error_code ec;
    if (!from_config) {
        FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    }
    /*
     * Fill the session Key
     */
    session_key.set_ip(session_map_iter->first.remote_ip);
    session_key.set_port(session_map_iter->first.src_port);
    FillSessionFlowInfo(session_map_iter->second.fwd_flow,
                        session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time,
                        ksync_obj, session_info.forward_flow_info);
    FillSessionFlowInfo(session_map_iter->second.rev_flow,
            session_map_iter->second.setup_time,
            session_map_iter->second.teardown_time,
            ksync_obj, session_info.reverse_flow_info);
    session_info.set_vm(fe->data().vm_cfg_name);
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        session_info.set_other_vrouter_ip(
                boost::asio::ip::address::from_string(rid, ec));
    } else {
        session_info.set_other_vrouter_ip(
                boost::asio::ip::address::from_string(fe->peer_vrouter(), ec));
    }
    session_info.set_underlay_proto(fe->tunnel_type().GetType());
}

void SessionStatsCollector::FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter,
                                               SessionAggInfo &session_agg_info,
                                               SessionIpPortProtocol &session_agg_key,
                                               uint64_t total_fwd_bytes,
                                               uint64_t total_fwd_packets,
                                               uint64_t total_rev_bytes,
                                               uint64_t total_rev_packets) {
    /*
     * Fill the session agg key
     */
    session_agg_key.set_ip(session_agg_map_iter->first.local_ip);
    session_agg_key.set_port(session_agg_map_iter->first.dst_port);
    session_agg_key.set_protocol(session_agg_map_iter->first.proto);
    session_agg_info.set_logged_forward_bytes(total_fwd_bytes);
    session_agg_info.set_logged_forward_pkts(total_fwd_packets);
    session_agg_info.set_logged_reverse_bytes(total_rev_bytes);
    session_agg_info.set_logged_reverse_pkts(total_rev_packets);
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
                                         uint64_t curr_time,
                                         bool from_config) {
    uint32_t count = 1;
    uint64_t total_fwd_bytes, total_rev_bytes;
    uint64_t total_fwd_packets, total_rev_packets;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;

    SessionEndpoint &session_ep =  session_msg_list_[GetSessionMsgIdx()];

    session_agg_map_iter = it->second.session_agg_map_.begin();
    while (session_agg_map_iter != it->second.session_agg_map_.end()) {
        total_fwd_bytes = total_rev_bytes = 0;
        total_fwd_packets = total_rev_packets = 0;
        session_map_iter = session_agg_map_iter->second.session_map_.begin();
        while (session_map_iter != session_agg_map_iter->second.session_map_.end()) {
            FillSessionInfo(session_map_iter, ksync_obj, session_info, session_key, from_config);
            session_agg_info.sessionMap.insert(make_pair(session_key, session_info));
            total_fwd_bytes +=
                session_info.get_forward_flow_info().get_logged_bytes();
            total_fwd_packets +=
                session_info.get_forward_flow_info().get_logged_pkts();
            total_rev_bytes +=
                session_info.get_reverse_flow_info().get_logged_bytes();
            total_rev_packets +=
                session_info.get_reverse_flow_info().get_logged_pkts();
            session_map_iter++;
        }
        FillSessionAggInfo(session_agg_map_iter, session_agg_info, session_agg_key,
                           total_fwd_bytes, total_fwd_packets, total_rev_bytes,
                           total_rev_packets);
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
        count += ProcessSessionEndpoint(it, ksync_obj, curr_time, false);
        it++;
    }
    return count;
}

bool SessionStatsCollector::RunSessionStatsTask() {
    uint32_t count = RunSessionEndpointStats(kSessionsPerTask);
    // Update number of entries visited
    if (count < session_ep_to_visit_)
        session_ep_to_visit_ -= count;
    else
        session_ep_to_visit_ = 0;
    // Done with task if we reach end of tree or count is exceeded
    if (session_ep_iteration_key_.vmi == NULL || session_ep_to_visit_ == 0) {
        session_ep_to_visit_ = 0;
        session_task_ = NULL;
        return true;
    }

    // More entries to visit. Continue the task
    return false;
}

uint32_t SessionStatsCollector::RunSessionEndpointStats(uint32_t max_count) {
    SessionEndpointMap::iterator it;
    if (session_ep_iteration_key_.vmi == NULL) {
        it = session_endpoint_map_.begin();
    } else {
        it = session_endpoint_map_.find(session_ep_iteration_key_);
        // We will continue from begining on next timer
        if (it == session_endpoint_map_.end()) {
            session_ep_iteration_key_.vmi = NULL;
            return session_ep_to_visit_;
        }
    }

    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
        ksync_flow_memory();
    uint64_t curr_time = GetCurrentTime();
    uint32_t count = 0;
    while (count < max_count) {
        if (it == session_endpoint_map_.end()) {
            break;
        }

        count += ProcessSessionEndpoint(it, ksync_obj, curr_time, false);
        it++;
        session_ep_visited_++;
    }

    //Send any pending session export messages
    DispatchPendingSessionMsg();

    // Update iterator for next pass
    if (it == session_endpoint_map_.end()) {
        session_ep_iteration_key_.vmi = NULL;
    } else {
        session_ep_iteration_key_ = it->first;
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
    return ssc_->RunSessionStatsTask();
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

FlowEntry* SessionStatsReq::reverse_flow() const {
    FlowEntry *rflow = NULL;
    if (flow_.get()) {
        rflow = flow_->reverse_flow_entry();
    }
    return rflow;
}

