/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <bitset>
#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <sandesh/common/flow_types.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>
#include <vrouter/flow_stats/session_stats_collector.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/tag.h>
#include <oper/security_logging_object.h>
#include <oper/global_vrouter.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
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
        session_ep_iteration_key_(), session_agg_iteration_key_(),
        session_iteration_key_(),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId(kTaskSessionStatsCollector),
                       instance_id,
                       boost::bind(&SessionStatsCollector::RequestHandler,
                                   this, _1)),
        session_msg_list_(agent_uve_->agent()->params()->max_endpoints_per_session_msg(),
                          SessionEndpoint()),
        session_msg_index_(0), instance_id_(instance_id),
        flow_stats_manager_(aging_module), parent_(obj), session_task_(NULL),
        current_time_(GetCurrentTime()), session_task_starts_(0) {
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

void SessionStatsCollector::RegisterDBClients() {
    if (agent_uve_->agent()->slo_table()) {
        slo_listener_id_ = agent_uve_->agent()->slo_table()->Register(
                            boost::bind(&SessionStatsCollector::SloNotify, this,
                                        _1, _2));
    }
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
                << " session_ep visited " << session_ep_visited_
                << " Request count " << request_queue_.Length());
        }
        session_ep_visited_ = 0;
        session_task_ = new SessionTask(this);
        agent_uve_->agent()->task_scheduler()->Enqueue(session_task_);
    }
    return true;
}


bool FlowStatsManager::UpdateSessionThreshold() {
    uint64_t curr_time = FlowStatsCollector::GetCurrentTime();
    bool export_rate_calculated = false;
    uint32_t exp_rate_without_sampling = 0;

    /* If flows are not being exported, no need to update threshold */
    if (!session_export_count_) {
        return true;
    }

    // Calculate Flow Export rate
    if (prev_flow_export_rate_compute_time_) {
        uint64_t diff_secs = 0;
        uint64_t diff_micro_secs = curr_time -
            prev_flow_export_rate_compute_time_;
        if (diff_micro_secs) {
            diff_secs = diff_micro_secs/1000000;
        }
        if (diff_secs) {
            uint32_t session_export_count = session_export_count_reset();
            session_export_rate_ = session_export_count/diff_secs;
            exp_rate_without_sampling =
                session_export_without_sampling_reset()/diff_secs;
            prev_flow_export_rate_compute_time_ = curr_time;
            export_rate_calculated = true;
        }
    } else {
        prev_flow_export_rate_compute_time_ = curr_time;
        session_export_count_ = 0;
        return true;
    }

    uint32_t cfg_rate = agent_->oper_db()->global_vrouter()->
                            flow_export_rate();
    /* No need to update threshold when flow_export_rate is NOT calculated
     * and configured flow export rate has not changed */
    if (!export_rate_calculated &&
        (cfg_rate == prev_cfg_flow_export_rate_)) {
        return true;
    }
    uint64_t cur_t = threshold(), new_t = 0;
    // Update sampling threshold based on flow_export_rate_
    if (session_export_rate_ < ((double)cfg_rate) * 0.8) {
        /* There are two reasons why we can be here.
         * 1. None of the flows were sampled because we never crossed
         *    80% of configured flow-export-rate.
         * 2. In scale setups, the threshold was updated to high value because
         *    of which flow-export-rate has dropped drastically.
         * Threshold should be updated here depending on which of the above two
         * situations we are in. */
        if (!sessions_sampled_atleast_once_) {
            UpdateThreshold(kDefaultFlowSamplingThreshold, false);
        } else {
            if (session_export_rate_ < ((double)cfg_rate) * 0.5) {
                UpdateThreshold((threshold_ / 4), false);
            } else {
                UpdateThreshold((threshold_ / 2), false);
            }
        }
    } else if (session_export_rate_ > (cfg_rate * 3)) {
        UpdateThreshold((threshold_ * 4), true);
    } else if (session_export_rate_ > (cfg_rate * 2)) {
        UpdateThreshold((threshold_ * 3), true);
    } else if (session_export_rate_ > ((double)cfg_rate) * 1.25) {
        UpdateThreshold((threshold_ * 2), true);
    }
    prev_cfg_flow_export_rate_ = cfg_rate;
    new_t = threshold();
    FLOW_EXPORT_STATS_TRACE(session_export_rate_, exp_rate_without_sampling,
                            cur_t, new_t);
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

void SessionStatsCollector::DeleteEvent(const FlowEntryPtr &flow,
                                        const RevFlowDepParams &params) {
    boost::shared_ptr<SessionStatsReq>
        req(new SessionStatsReq(SessionStatsReq::DELETE_SESSION, flow,
                                GetCurrentTime(), params));
    request_queue_.Enqueue(req);
}

void SessionStatsCollector::UpdateSessionStatsEvent(const FlowEntryPtr &flow,
                                          uint32_t bytes,
                                          uint32_t packets,
                                          uint32_t oflow_bytes,
                                          const boost::uuids::uuid &u) {
    boost::shared_ptr<SessionStatsReq>
        req(new SessionStatsReq(SessionStatsReq::UPDATE_SESSION_STATS, flow,
                                bytes, packets, oflow_bytes, u));
    request_queue_.Enqueue(req);
}

void SessionStatsCollector::DispatchSessionMsg(const std::vector<SessionEndpoint> &lst) {
    flow_stats_manager_->UpdateSessionMsgExportStats(1);
    flow_stats_manager_->UpdateSessionSampleExportStats(lst.size());
    SESSION_ENDPOINT_OBJECT_LOG("", SandeshLevel::SYS_INFO, lst);
}

void SessionStatsCollector::EnqueueSessionMsg() {
    session_msg_index_++;
    if (session_msg_index_ ==
        agent_uve_->agent()->params()->max_endpoints_per_session_msg()) {
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
        DeleteSession(req->flow(), req->time(), &req->params());
        break;
    }

    case SessionStatsReq::UPDATE_SESSION_STATS: {
        EvictedSessionStatsUpdate(req->flow(), req->bytes(), req->packets(),
                                  req->oflow_bytes(), req->uuid());
        break;
    }

    default:
         assert(0);
    }

    return true;
}

bool SessionEndpointKey::IsLess(const SessionEndpointKey &rhs) const {
    if (vmi_cfg_name != rhs.vmi_cfg_name) {
        return vmi_cfg_name < rhs.vmi_cfg_name;
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
    if (match_policy != rhs.match_policy) {
        return match_policy < rhs.match_policy;
    }
    if (is_client_session != rhs.is_client_session) {
        return is_client_session < rhs.is_client_session;
    }
    return is_si < rhs.is_si;
}

bool SessionEndpointKey::IsEqual(const SessionEndpointKey &rhs) const {
    if (vmi_cfg_name != rhs.vmi_cfg_name) {
        return false;
    }
    if (local_vn != rhs.local_vn) {
        return false;
    }
    if (remote_vn != rhs.remote_vn) {
        return false;
    }
    if (local_tagset != rhs.local_tagset) {
        return false;
    }
    if (remote_tagset != rhs.remote_tagset) {
        return false;
    }
    if (remote_prefix != rhs.remote_prefix) {
        return false;
    }
    if (match_policy != rhs.match_policy) {
        return false;
    }
    if (is_client_session != rhs.is_client_session) {
        return false;
    }
    if (is_si != rhs.is_si) {
        return false;
    }
    return true;
}

void SessionEndpointKey::Reset() {
    vmi_cfg_name = "";
    local_vn = "";
    remote_vn = "";
    local_tagset.clear();
    remote_tagset.clear();
    remote_prefix = "";
    match_policy = "";
    is_client_session = false;
    is_si = false;
}

bool SessionAggKey::IsLess(const SessionAggKey &rhs) const {
    if (local_ip != rhs.local_ip) {
        return local_ip < rhs.local_ip;
    }
    if (server_port != rhs.server_port) {
        return server_port < rhs.server_port;
    }
    return proto < rhs.proto;
}

bool SessionAggKey::IsEqual(const SessionAggKey &rhs) const {
    if (local_ip != rhs.local_ip) {
        return false;
    }
    if (server_port != rhs.server_port) {
        return false;
    }
    if (proto != rhs.proto) {
        return false;
    }
    return true;
}

void SessionAggKey::Reset() {
    local_ip = IpAddress();
    server_port = 0;
    proto = 0;
}

bool SessionKey::IsLess(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return remote_ip < rhs.remote_ip;
    }
    return client_port < rhs.client_port;
}

bool SessionKey::IsEqual(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return false;
    }
    if (client_port != rhs.client_port) {
        return false;
    }
    return true;
}

void SessionKey::Reset() {
    remote_ip = IpAddress();
    client_port = 0;
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
    if (vmi->cfg_name().empty()) {
        return false;
    }
    const string &src_vn = fe->data().source_vn_match;
    const string &dst_vn = fe->data().dest_vn_match;

    session_endpoint_key.vmi_cfg_name = vmi->cfg_name();
    session_endpoint_key.local_tagset = fe->local_tagset();
    session_endpoint_key.remote_tagset = fe->remote_tagset();
    session_endpoint_key.remote_prefix = fe->RemotePrefix();
    session_endpoint_key.match_policy = fe->fw_policy_name_uuid();
    if (vmi->service_intf_type().empty()) {
        session_endpoint_key.is_si = false;
    } else {
        session_endpoint_key.is_si= true;
    }

    if (fe->IsClientFlow()) {
        session_agg_key.local_ip = fe->key().src_addr;
        session_agg_key.server_port = fe->key().dst_port;
        session_key.remote_ip = fe->key().dst_addr;
        session_key.client_port = fe->key().src_port;
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
            session_agg_key.server_port = fe->key().src_port;
            session_key.remote_ip = fe->key().dst_addr;
            session_key.client_port = fe->key().dst_port;
            session_endpoint_key.local_vn = src_vn;
            session_endpoint_key.remote_vn = dst_vn;
        } else {
            session_agg_key.local_ip = fe->key().dst_addr;
            session_agg_key.server_port = fe->key().dst_port;
            session_key.remote_ip = fe->key().src_addr;
            session_key.client_port = fe->key().src_port;
            session_endpoint_key.local_vn = dst_vn;
            session_endpoint_key.remote_vn = src_vn;
        }
        session_endpoint_key.is_client_session = false;
    } else {
        return false;
    }
    session_agg_key.proto = fe->key().protocol;
    return true;
}

void SessionStatsCollector::UpdateSessionFlowStatsInfo(FlowEntry *fe,
    SessionFlowStatsInfo *session_flow) const {
    session_flow->flow = fe;
    session_flow->gen_id= fe->gen_id();
    session_flow->flow_handle = fe->flow_handle();
    session_flow->uuid = fe->uuid();
    session_flow->total_bytes = 0;
    session_flow->total_packets = 0;
}

void SessionStatsCollector::UpdateSessionStatsInfo(FlowEntry* fe,
    uint64_t setup_time, SessionStatsInfo *session) const {
    FlowEntry *rfe = fe->reverse_flow_entry();

    session->setup_time = setup_time;
    session->teardown_time = 0;
    session->exported_atleast_once = false;
    UpdateSessionFlowStatsInfo(fe, &session->fwd_flow);
    UpdateSessionFlowStatsInfo(rfe, &session->rev_flow);
}

void SessionStatsCollector::AddSession(FlowEntry* fe, uint64_t setup_time) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo session_agg_info;
    SessionStatsInfo session;
    SessionKey session_key;
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

    success = GetSessionKey(fe_fwd, session_agg_key, session_key,
                            session_endpoint_key);
    if (!success) {
        return;
    }

    /*
     * If the flow is part of session endpoint DB then
     * delete the existing one
     * Flow add comes for the existing flow for the following cases
     * - flow uuid changes (add-delete-add : will be compressed)
     * - any key changes from session endpoint, aggregate or session
     */
    FlowSessionMap::iterator flow_session_map_iter;
    flow_session_map_iter = flow_session_map_.find(fe_fwd);
    if (flow_session_map_iter != flow_session_map_.end()) {

        FlowToSessionMap &flow_to_session_map = flow_session_map_iter->second;
        FlowToSessionMap rhs_flow_to_session_map(fe_fwd->uuid(),
                                                 session_key,
                                                 session_agg_key,
                                                 session_endpoint_key);
        if (!(flow_to_session_map.IsEqual(rhs_flow_to_session_map))) {
            DeleteSession(fe_fwd, GetCurrentTime(), NULL);
        }
    }

    UpdateSessionStatsInfo(fe_fwd, setup_time, &session);

    session_endpoint_map_iter = session_endpoint_map_.find(
                                            session_endpoint_key);
    if (session_endpoint_map_iter == session_endpoint_map_.end()) {
        session_agg_info.session_map_.insert(make_pair(session_key, session));
        session_endpoint_info.session_agg_map_.insert(
                                make_pair(session_agg_key, session_agg_info));
        session_endpoint_map_.insert(make_pair(session_endpoint_key,
                                                session_endpoint_info));
        AddFlowToSessionMap(fe_fwd, session_key, session_agg_key,
                            session_endpoint_key);
    } else {
        session_agg_map_iter = session_endpoint_map_iter->
                                        second.session_agg_map_.find(
                                                        session_agg_key);
        if (session_agg_map_iter ==
                session_endpoint_map_iter->second.session_agg_map_.end()) {
            session_agg_info.session_map_.insert(make_pair(session_key, session));
            session_endpoint_map_iter->second.session_agg_map_.insert(
                                make_pair(session_agg_key, session_agg_info));
            AddFlowToSessionMap(fe_fwd, session_key, session_agg_key,
                                session_endpoint_key);
        } else {
            session_map_iter =
                session_agg_map_iter->second.session_map_.find(session_key);
            if (session_map_iter ==
                    session_agg_map_iter->second.session_map_.end()) {
                session_agg_map_iter->second.session_map_.insert(
                                        make_pair(session_key, session));
                AddFlowToSessionMap(fe_fwd, session_key, session_agg_key,
                                    session_endpoint_key);
            } else {
                /*
                 * existing flow should match with the incoming add flow
                 */
                assert(session.fwd_flow.uuid == fe_fwd->uuid());
            }
        }
    }
}

void SessionStatsCollector::DeleteSession(FlowEntry* fe, uint64_t teardown_time,
                                          const RevFlowDepParams *params) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionKey    session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;
    SessionEndpointInfo  session_endpoint_info;
    SessionEndpointKey session_endpoint_key;
    SessionEndpointMap::iterator session_endpoint_map_iter;
    bool read_flow=true;

    /*
     * If the given flow uuid is different from the exisiting one
     * then ignore the read from flow
     */
    FlowSessionMap::iterator flow_session_map_iter;

    flow_session_map_iter = flow_session_map_.find(fe);
    if (flow_session_map_iter != flow_session_map_.end()) {
        if (fe->uuid() != flow_session_map_iter->second.uuid()) {
            read_flow = false;
        }
        session_endpoint_key = flow_session_map_iter->second.session_endpoint_key();
        session_agg_key = flow_session_map_iter->second.session_agg_key();
        session_key = flow_session_map_iter->second.session_key();
    } else {
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
                ProcessSessionDelete(session_endpoint_map_iter,
                                     session_agg_map_iter, session_map_iter,
                                     params, read_flow);

                DeleteFlowToSessionMap(session_map_iter->second.fwd_flow.flow.get());
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

void SessionStatsCollector::EvictedSessionStatsUpdate(const FlowEntryPtr &flow,
                                                uint32_t bytes,
                                                uint32_t packets,
                                                uint32_t oflow_bytes,
                                                const boost::uuids::uuid &u) {
    FlowSessionMap::iterator flow_session_map_iter;
    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;
    SessionEndpointMap::iterator session_ep_map_iter;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    flow_session_map_iter = flow_session_map_.find(flow.get());
    if (flow_session_map_iter == flow_session_map_.end()) {
        return;
    }

    if (flow.get()->uuid() != u) {
        return;
    }

    SessionEndpointKey session_db_ep_key = flow_session_map_iter->second.session_endpoint_key();
    SessionAggKey session_db_agg_key = flow_session_map_iter->second.session_agg_key();
    SessionKey session_db_key = flow_session_map_iter->second.session_key();

    session_ep_map_iter = session_endpoint_map_.find(
                                            session_db_ep_key);
    if (session_ep_map_iter != session_endpoint_map_.end()) {
        session_agg_map_iter = session_ep_map_iter->
                                        second.session_agg_map_.find(
                                                        session_db_agg_key);
        if (session_agg_map_iter !=
                session_ep_map_iter->second.session_agg_map_.end()) {
            session_map_iter =
                session_agg_map_iter->second.session_map_.find(session_db_key);
            if (session_map_iter !=
                    session_agg_map_iter->second.session_map_.end()) {
                SessionStatsParams params;
                SessionStatsChangedUnlocked(session_map_iter, &params);
                FillSessionInfoUnlocked(session_map_iter, params, &session_info,
                                        &session_key, NULL, false, true);
                /*
                 * update the latest statistics
                 */
                SessionFlowStatsInfo &session_flow = session_map_iter->second.fwd_flow;
                SessionFlowInfo &flow_info = session_info.forward_flow_info;
                uint64_t k_bytes, total_bytes, diff_bytes = 0;
                uint64_t k_packets, total_packets, diff_packets = 0;
                k_bytes = FlowStatsCollector::GetFlowStats((oflow_bytes & 0xFFFF),
                                                           bytes);
                k_packets = FlowStatsCollector::GetFlowStats((oflow_bytes & 0xFFFF0000),
                                                             packets);
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
                flow_info.set_sampled_pkts(diff_packets);
                flow_info.set_sampled_bytes(diff_bytes);

                /*
                 * inert to agg map
                 */
                session_agg_info.sessionMap.insert(make_pair(session_key, session_info));
                FillSessionAggInfo(session_agg_map_iter, &session_agg_info, &session_agg_key,
                                   session_info.get_forward_flow_info().get_logged_bytes(),
                                   session_info.get_forward_flow_info().get_logged_pkts(),
                                   session_info.get_reverse_flow_info().get_logged_bytes(),
                                   session_info.get_reverse_flow_info().get_logged_pkts());
                /*
                 * insert to session ep map
                 */
                SessionEndpoint &session_ep =  session_msg_list_[GetSessionMsgIdx()];
                session_ep.sess_agg_info.insert(make_pair(session_agg_key, session_agg_info));
                FillSessionEndpoint(session_ep_map_iter, &session_ep);

                /*
                 * export the session ep
                 */
                EnqueueSessionMsg();
            }
        }
    }
}

void SessionStatsCollector::AddFlowToSessionMap(FlowEntry *fe,
                                                SessionKey session_key,
                                                SessionAggKey session_agg_key,
                                                SessionEndpointKey session_endpoint_key) {
    FlowToSessionMap flow_to_session_map(fe->uuid(), session_key,
                                         session_agg_key,
                                         session_endpoint_key);
    std::pair<FlowSessionMap::iterator, bool> ret =
        flow_session_map_.insert(make_pair(fe, flow_to_session_map));
    if (ret.second == false) {
        FlowToSessionMap &prev = ret.first->second;
        assert(prev.uuid() == fe->uuid());
    }
}

void SessionStatsCollector::DeleteFlowToSessionMap(FlowEntry *fe) {
    FlowSessionMap::iterator flow_session_map_iter;
    flow_session_map_iter = flow_session_map_.find(fe);
    if (flow_session_map_iter != flow_session_map_.end()) {
        flow_session_map_.erase(flow_session_map_iter);
    }
}

void SessionStatsCollector::UpdateSloStateRules(SecurityLoggingObject *slo,
                                                SessionSloState *state) {
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator it;
    it = slo->rules().begin();
    while (it != slo->rules().end()) {
        state->UpdateSessionSloStateRuleEntry(it->rule_uuid, it->rate);
        it++;
    }

    UuidList::const_iterator acl_it;
    acl_it = slo->firewall_policy_list().begin();
    while (acl_it != slo->firewall_policy_list().end()) {
        AclKey key(*acl_it);
        AclDBEntry *acl = static_cast<AclDBEntry *>(agent_uve_->agent()->
                        acl_table()->FindActiveEntry(&key));
        if (acl) {
            int index = 0;
            const AclEntry *ae = acl->GetAclEntryAtIndex(index);
            while (ae != NULL) {
                state->UpdateSessionSloStateRuleEntry(ae->uuid(), slo->rate());
                index++;
                ae = acl->GetAclEntryAtIndex(index);
            }
        }
        acl_it++;
    }

    UuidList::const_iterator fw_rule_it;
    fw_rule_it = slo->firewall_rule_list().begin();
    while (fw_rule_it != slo->firewall_rule_list().end()) {
        state->UpdateSessionSloStateRuleEntry(UuidToString(*fw_rule_it),
                                           slo->rate());
        fw_rule_it++;
    }

}

void SessionStatsCollector::SloNotify(DBTablePartBase *partition,
                                        DBEntryBase *e) {
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>(e);
    SessionSloState *state =
        static_cast<SessionSloState *>(slo->GetState(partition->parent(),
                                                     slo_listener_id_));
    if (slo->IsDeleted()) {
        if (!state)
            return;
        slo->ClearState(partition->parent(), slo_listener_id_);
        delete state;
        return;
    }

    if (!state) {
        state = new SessionSloState();
        slo->SetState(slo->get_table(), slo_listener_id_, state);
    }
    UpdateSloStateRules(slo, state);
}

void SessionStatsCollector::AddSessionSloRuleEntry(const std::string &uuid,
                                                   int rate,
                                                   SecurityLoggingObject *slo,
                                                   SessionSloRuleMap *slo_rule_map) {
    SessionSloRuleEntry slo_rule_entry(rate, slo->uuid());
    std::pair<SessionSloRuleMap::iterator, bool> ret;
    ret = slo_rule_map->insert(make_pair(uuid,
                                          slo_rule_entry));
}

void SessionStatsCollector::AddSloRules(
        const std::vector<autogen::SecurityLoggingObjectRuleEntryType> &list,
        SecurityLoggingObject *slo,
        SessionSloRuleMap *slo_rule_map) {
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator it;
    it = list.begin();
    while (it != list.end()) {
        AddSessionSloRuleEntry(it->rule_uuid, it->rate, slo, slo_rule_map);
        it++;
    }
}

void SessionStatsCollector::AddSloFirewallPolicies(const UuidList &list, int rate,
                                                   SecurityLoggingObject *slo,
                                                   SessionSloRuleMap *slo_rule_map) {
    UuidList::const_iterator acl_it;
    acl_it = list.begin();
    while (acl_it != list.end()) {
        AclKey key(*acl_it);
        AclDBEntry *acl = static_cast<AclDBEntry *>(agent_uve_->agent()->
                        acl_table()->FindActiveEntry(&key));
        if (acl) {
            int index = 0;
            const AclEntry *ae = acl->GetAclEntryAtIndex(index);
            while (ae != NULL) {
                AddSessionSloRuleEntry(ae->uuid(), rate, slo, slo_rule_map);
                index++;
                ae = acl->GetAclEntryAtIndex(index);
            }
        }
        acl_it++;
    }
}

void SessionStatsCollector::AddSloFirewallRules(const UuidList &list, int rate,
                                                SecurityLoggingObject *slo,
                                                SessionSloRuleMap *slo_rule_map) {
    UuidList::const_iterator it;
    it = list.begin();
    while (it != list.end()) {
        AddSessionSloRuleEntry(UuidToString(*it), rate, slo, slo_rule_map);
        it++;
    }
}

void SessionStatsCollector::AddSloEntryRules(SecurityLoggingObject *slo,
                                             SessionSloRuleMap *slo_rule_map) {
    AddSloRules(slo->rules(), slo, slo_rule_map);
    AddSloFirewallPolicies(slo->firewall_policy_list(), slo->rate(),
                           slo, slo_rule_map);
    AddSloFirewallRules(slo->firewall_rule_list(), slo->rate(),
                        slo, slo_rule_map);
}

void SessionStatsCollector::AddSloEntry(const boost::uuids::uuid &uuid,
                                       SessionSloRuleMap *slo_rule_map) {
    SecurityLoggingObjectKey slo_key(uuid);
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>
        (agent_uve_->agent()->slo_table()->FindActiveEntry(&slo_key));
    if (slo) {
        AddSloEntryRules(slo, slo_rule_map);
    }
}

void SessionStatsCollector::AddSloList(const UuidList &slo_list,
                                       SessionSloRuleMap *slo_rule_map) {
    UuidList::const_iterator sit = slo_list.begin();
    while (sit != slo_list.end()) {
        AddSloEntry(*sit, slo_rule_map);
        sit++;
    }
}
void SessionStatsCollector::BuildSloList(
                                const SessionFlowStatsInfo &session_flow,
                                SessionSloRuleMap *global_session_slo_rule_map,
                                SessionSloRuleMap *vmi_session_slo_rule_map,
                                SessionSloRuleMap *vn_session_slo_rule_map) {
    const FlowEntry *fe = session_flow.flow.get();
    const Interface *itf = fe->intf_entry();

    vmi_session_slo_rule_map->clear();
    vn_session_slo_rule_map->clear();
    global_session_slo_rule_map->clear();

    if (agent_uve_->agent()->oper_db()->global_vrouter()->slo_uuid() !=
                                                                nil_uuid()) {
        AddSloEntry(
                agent_uve_->agent()->oper_db()->global_vrouter()->slo_uuid(),
                global_session_slo_rule_map);
    }

    if (!itf) {
        return;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        return;
    }

    const VmInterface *vmi = static_cast<const VmInterface *>(itf);
    AddSloList(vmi->slo_list(), vmi_session_slo_rule_map);
    if (vmi->vn()) {
        AddSloList(vmi->vn()->slo_list(), vn_session_slo_rule_map);
    }
}

bool SessionStatsCollector::UpdateSloMatchRuleEntry(
                            const boost::uuids::uuid &slo_uuid,
                            const std::string &match_uuid) {
    SecurityLoggingObjectKey slo_key(slo_uuid);
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>
        (agent_uve_->agent()->slo_table()->FindActiveEntry(&slo_key));
    if (slo) {
        SessionSloState *state =
            static_cast<SessionSloState *>(slo->GetState(agent_uve_->agent()->slo_table(),
                                                         slo_listener_id_));
       return state->UpdateSessionSloStateRuleRefCount(match_uuid);
    }

    return false;
}

bool SessionStatsCollector::FindSloMatchRule(const SessionSloRuleMap &map,
                                             const std::string &match_uuid) {
    SessionSloRuleMap::const_iterator it;
    bool is_logged = false;
    it = map.find(match_uuid);
    if (it != map.end()) {
        is_logged = UpdateSloMatchRuleEntry(it->second.slo_uuid, match_uuid);
    }
    return is_logged;
}
bool SessionStatsCollector::MatchSloForSession(
        const SessionFlowStatsInfo &session_flow,
        const std::string &match_uuid) {
    bool is_vmi_slo_logged, is_vn_slo_logged, is_global_slo_logged;
    SessionSloRuleMap vmi_session_slo_rule_map;
    SessionSloRuleMap vn_session_slo_rule_map;
    SessionSloRuleMap global_session_slo_rule_map;

    /*
     * Get the list of slos need to be matched for the given flow
     */
    BuildSloList(session_flow,
                 &global_session_slo_rule_map,
                 &vmi_session_slo_rule_map,
                 &vn_session_slo_rule_map);
    /*
     * Match the policy_match for the given flow against the slo list
     */
    is_vmi_slo_logged = FindSloMatchRule(vmi_session_slo_rule_map, match_uuid);
    is_vn_slo_logged = FindSloMatchRule(vn_session_slo_rule_map, match_uuid);
    is_global_slo_logged = FindSloMatchRule(global_session_slo_rule_map,
                                             match_uuid);
    if ((is_vmi_slo_logged) ||
        (is_vn_slo_logged) ||
        (is_global_slo_logged)) {
        return true;
    }
    return false;
}

uint64_t SessionStatsCollector::GetUpdatedSessionFlowBytes(uint64_t info_bytes,
                                                 uint64_t k_flow_bytes) const {
    uint64_t oflow_bytes = 0xffff000000000000ULL & info_bytes;
    uint64_t old_bytes = 0x0000ffffffffffffULL & info_bytes;
    if (old_bytes > k_flow_bytes) {
        oflow_bytes += 0x0001000000000000ULL;
    }
    return (oflow_bytes |= k_flow_bytes);
}

uint64_t SessionStatsCollector::GetUpdatedSessionFlowPackets(
                                                  uint64_t info_packets,
                                                  uint64_t k_flow_pkts) const {
    uint64_t oflow_pkts = 0xffffff0000000000ULL & info_packets;
    uint64_t old_pkts = 0x000000ffffffffffULL & info_packets;
    if (old_pkts > k_flow_pkts) {
        oflow_pkts += 0x0000010000000000ULL;
    }
    return (oflow_pkts |= k_flow_pkts);
}

void SessionStatsCollector::FillSessionFlowStats(SessionFlowStatsInfo &session_flow,
                                                 const SessionFlowStatsParams &stats,
                                                 SessionFlowInfo *flow_info,
                                                 bool is_sampling)
                                                 const {
    if (!stats.valid) {
        return;
    }
    session_flow.total_bytes = stats.total_bytes;
    session_flow.total_packets = stats.total_packets;
    flow_info->set_tcp_flags(stats.tcp_flags);
    flow_info->set_underlay_source_port(stats.underlay_src_port);
    if (is_sampling) {
        flow_info->set_sampled_pkts(stats.diff_packets);
        flow_info->set_sampled_bytes(stats.diff_bytes);
    } else {
        flow_info->set_logged_pkts(stats.diff_packets);
        flow_info->set_logged_bytes(stats.diff_bytes);
    }
}

void SessionStatsCollector::FillSessionFlowInfo(SessionFlowStatsInfo &session_flow,
                                                uint64_t setup_time,
                                                uint64_t teardown_time,
                                                const SessionFlowStatsParams &stats,
                                                const RevFlowDepParams *params,
                                                bool read_flow,
                                                SessionFlowInfo *flow_info,
                                                bool sampling_selected)
                                                const {
    FlowEntry *fe = session_flow.flow.get();
    std::string action_str;

    FillSessionFlowStats(session_flow, stats, flow_info, sampling_selected);
    flow_info->set_flow_uuid(session_flow.uuid);
    flow_info->set_setup_time(setup_time);
    flow_info->set_teardown_time(teardown_time);
    if (params) {
        GetFlowSandeshActionParams(params->action_info_, action_str);
        flow_info->set_action(action_str);
        flow_info->set_sg_rule_uuid(StringToUuid(params->sg_uuid_));
        flow_info->set_nw_ace_uuid(StringToUuid(params->nw_ace_uuid_));
        flow_info->set_drop_reason(params->drop_reason_);
    } else if (read_flow) {
        GetFlowSandeshActionParams(fe->data().match_p.action_info, action_str);
        flow_info->set_action(action_str);
        flow_info->set_sg_rule_uuid(StringToUuid(fe->sg_rule_uuid()));
        flow_info->set_nw_ace_uuid(StringToUuid(fe->nw_ace_uuid()));
        flow_info->set_drop_reason(fe->data().drop_reason);
    }
}

bool SessionStatsCollector::SessionStatsChangedLocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionStatsParams *params) const {
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    return SessionStatsChangedUnlocked(session_map_iter, params);
}

bool SessionStatsCollector::SessionStatsChangedUnlocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionStatsParams *params) const {

    bool fwd_updated = FetchFlowStats(session_map_iter->second.fwd_flow,
                                      &params->fwd_flow);
    bool rev_updated = FetchFlowStats(session_map_iter->second.rev_flow,
                                      &params->rev_flow);
    return (fwd_updated || rev_updated);
}

bool SessionStatsCollector::FetchFlowStats
(const SessionFlowStatsInfo &info, SessionFlowStatsParams *params) const {
    vr_flow_stats k_stats;
    KFlowData kinfo;
    uint64_t k_bytes, bytes, k_packets;
    const vr_flow_entry *k_flow = NULL;
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
        ksync_flow_memory();

    k_flow = ksync_obj->GetKFlowStatsAndInfo(info.flow->key(),
                                             info.flow_handle,
                                             info.gen_id, &k_stats, &kinfo);
    if (!k_flow) {
        return false;
    }

    k_bytes = FlowStatsCollector::GetFlowStats(k_stats.flow_bytes_oflow,
                                               k_stats.flow_bytes);
    k_packets = FlowStatsCollector::GetFlowStats(k_stats.flow_packets_oflow,
                                                 k_stats.flow_packets);

    bytes = 0x0000ffffffffffffULL & info.total_bytes;

    if (bytes != k_bytes) {
        params->total_bytes = GetUpdatedSessionFlowBytes(info.total_bytes,
                                                         k_bytes);
        params->total_packets = GetUpdatedSessionFlowPackets(info.total_packets,
                                                             k_packets);
        params->diff_bytes = params->total_bytes - info.total_bytes;
        params->diff_packets = params->total_packets - info.total_packets;
        params->tcp_flags = kinfo.tcp_flags;
        params->underlay_src_port = kinfo.underlay_src_port;
        params->valid = true;
        return true;
    }
    return false;
}

void SessionStatsCollector::FillSessionInfoLocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     const SessionStatsParams &stats, SessionInfo *session_info,
     SessionIpPort *session_key, bool is_sampling) const {
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    FillSessionInfoUnlocked(session_map_iter, stats, session_info, session_key, NULL,
                            true, is_sampling);
}

void SessionStatsCollector::FillSessionInfoUnlocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     const SessionStatsParams &stats,
     SessionInfo *session_info,
     SessionIpPort *session_key,
     const RevFlowDepParams *params,
     bool read_flow, bool is_sampling) const {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    boost::system::error_code ec;
    /*
     * Fill the session Key
     */
    session_key->set_ip(session_map_iter->first.remote_ip);
    session_key->set_port(session_map_iter->first.client_port);
    FillSessionFlowInfo(session_map_iter->second.fwd_flow,
                        session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time, stats.fwd_flow,
                        NULL, read_flow, &session_info->forward_flow_info,
                        is_sampling);
    FillSessionFlowInfo(session_map_iter->second.rev_flow,
                        session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time, stats.rev_flow,
                        params, read_flow, &session_info->reverse_flow_info,
                        is_sampling);
    if (read_flow) {
        session_info->set_vm(fe->data().vm_cfg_name);
        if (fe->is_flags_set(FlowEntry::LocalFlow)) {
            session_info->set_other_vrouter_ip(
                    boost::asio::ip::address::from_string(rid, ec));
        } else {
            session_info->set_other_vrouter_ip(
                boost::asio::ip::address::from_string(fe->peer_vrouter(), ec));
        }
        session_info->set_underlay_proto(fe->tunnel_type().GetType());
    }
    bool first_time_export = false;
    if (!session_map_iter->second.exported_atleast_once) {
        first_time_export = true;
        /* Mark the flow as exported */
        session_map_iter->second.exported_atleast_once = true;
    }
    flow_stats_manager_->UpdateSessionExportStats(1, first_time_export,
                                                  stats.sampled);
}

void SessionStatsCollector::FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter,
                                               SessionAggInfo *session_agg_info,
                                               SessionIpPortProtocol *session_agg_key,
                                               uint64_t total_fwd_bytes,
                                               uint64_t total_fwd_packets,
                                               uint64_t total_rev_bytes,
                                               uint64_t total_rev_packets)
                                               const {
    /*
     * Fill the session agg key
     */
    session_agg_key->set_ip(session_agg_map_iter->first.local_ip);
    session_agg_key->set_port(session_agg_map_iter->first.server_port);
    session_agg_key->set_protocol(session_agg_map_iter->first.proto);
    session_agg_info->set_logged_forward_bytes(total_fwd_bytes);
    session_agg_info->set_logged_forward_pkts(total_fwd_packets);
    session_agg_info->set_logged_reverse_bytes(total_rev_bytes);
    session_agg_info->set_logged_reverse_pkts(total_rev_packets);
    session_agg_info->set_sampled_forward_bytes(total_fwd_bytes);
    session_agg_info->set_sampled_forward_pkts(total_fwd_packets);
    session_agg_info->set_sampled_reverse_bytes(total_rev_bytes);
    session_agg_info->set_sampled_reverse_pkts(total_rev_packets);
}

void SessionStatsCollector::FillSessionTagInfo(const TagList &list,
                                               SessionEndpoint *session_ep,
                                               bool is_remote) const {
    std::set<std::string>   labels;
    TagList::const_iterator it = list.begin();
    while (it != list.end()) {
        uint32_t    ttype = (uint32_t)*it >> TagEntry::kTagTypeBitShift;
        std::string tag_type = TagEntry::GetTypeStr(ttype);
        if (tag_type == "site") {
            if (is_remote) {
                session_ep->set_remote_site(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep->set_site(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "deployment") {
            if (is_remote) {
                session_ep->set_remote_deployment(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep->set_deployment(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "tier") {
            if (is_remote) {
                session_ep->set_remote_tier(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep->set_tier(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if (tag_type == "application") {
            if (is_remote) {
                session_ep->set_remote_application(agent_uve_->agent()->tag_table()->TagName(*it));
            } else {
                session_ep->set_application(agent_uve_->agent()->tag_table()->TagName(*it));
            }
        } else if(tag_type == "label") {
            labels.insert(agent_uve_->agent()->tag_table()->TagName(*it));
        }
        it++;
    }
    if (labels.size() != 0) {
        if (is_remote) {
            session_ep->set_labels(labels);
        } else {
            session_ep->set_remote_labels(labels);
        }
    }

}

void SessionStatsCollector::FillSessionEndpoint(SessionEndpointMap::iterator it,
                                                SessionEndpoint *session_ep)
                                                const {
    string rid = agent_uve_->agent()->router_id().to_string();
    boost::system::error_code ec;

    session_ep->set_vmi(it->first.vmi_cfg_name);
    session_ep->set_vn(it->first.local_vn);
    session_ep->set_remote_vn(it->first.remote_vn);
    session_ep->set_is_client_session(it->first.is_client_session);
    session_ep->set_is_si(it->first.is_si);
    session_ep->set_remote_prefix(it->first.remote_prefix);
    session_ep->set_security_policy_rule(it->first.match_policy);
    FillSessionTagInfo(it->first.local_tagset, session_ep, false);
    FillSessionTagInfo(it->first.remote_tagset, session_ep, true);
    session_ep->set_vrouter_ip(
                    boost::asio::ip::address::from_string(rid, ec));
}

void SessionStatsCollector::ProcessSessionDelete
    (const SessionEndpointMap::iterator &ep_it,
     const SessionEndpointInfo::SessionAggMap::iterator &agg_it,
     const SessionPreAggInfo::SessionMap::iterator &session_it,
     const RevFlowDepParams *params, bool read_flow) {

    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;

    SessionStatsParams stats;
    SessionStatsChangedUnlocked(session_it, &stats);
    bool selected = true;
    if (IsSamplingEnabled()) {
        selected = SampleSession(session_it, &stats);
    }
    /* Ignore the session export if sampling drops the session */
    if (!selected) {
        return;
    }
    FillSessionInfoUnlocked(session_it, stats, &session_info, &session_key,
                            params, read_flow, true);
    session_agg_info.sessionMap.insert(make_pair(session_key, session_info));
    FillSessionAggInfo(agg_it, &session_agg_info, &session_agg_key,
                       session_info.get_forward_flow_info().get_logged_bytes(),
                       session_info.get_forward_flow_info().get_logged_pkts(),
                       session_info.get_reverse_flow_info().get_logged_bytes(),
                       session_info.get_reverse_flow_info().get_logged_pkts());
    SessionEndpoint &session_ep = session_msg_list_[GetSessionMsgIdx()];

    session_ep.sess_agg_info.insert(make_pair(session_agg_key,
                                              session_agg_info));
    FillSessionEndpoint(ep_it, &session_ep);
    EnqueueSessionMsg();
    DispatchPendingSessionMsg();
}

bool SessionStatsCollector::ProcessSessionEndpoint
    (const SessionEndpointMap::iterator &it) {
    uint64_t total_fwd_bytes, total_rev_bytes;
    uint64_t total_fwd_packets, total_rev_packets;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;
    uint32_t session_count = 0, session_agg_count = 0;
    bool exit = false, ep_completed = true;

    SessionEndpoint &session_ep = session_msg_list_[GetSessionMsgIdx()];

    session_agg_map_iter = it->second.session_agg_map_.
        lower_bound(session_agg_iteration_key_);
    while (session_agg_map_iter != it->second.session_agg_map_.end()) {
        total_fwd_bytes = total_rev_bytes = 0;
        total_fwd_packets = total_rev_packets = 0;
        session_count = 0;
        session_map_iter = session_agg_map_iter->second.session_map_.
            lower_bound(session_iteration_key_);
        while (session_map_iter != session_agg_map_iter->second.session_map_.end()) {
            SessionStatsParams params;
            bool changed = SessionStatsChangedLocked(session_map_iter, &params);
            if (!changed && session_map_iter->second.exported_atleast_once) {
                ++session_map_iter;
                continue;
            }

            std::string match_policy_uuid = session_map_iter->
                second.fwd_flow.flow.get()->fw_policy_uuid();
            bool is_fwd_logged = MatchSloForSession(
                                    session_map_iter->second.fwd_flow,
                                    match_policy_uuid);
            match_policy_uuid = session_map_iter->
                second.rev_flow.flow.get()->fw_policy_uuid();
            bool is_rev_logged = MatchSloForSession(
                                    session_map_iter->second.rev_flow,
                                    match_policy_uuid);

            bool sampling_selected = true;
            if (IsSamplingEnabled()) {
                sampling_selected = SampleSession(session_map_iter, &params);
            }

            /* Ignore the session export if sampling drops the session */
            if (!sampling_selected && !is_fwd_logged && !is_rev_logged) {
                ++session_map_iter;
                continue;
            }

            FillSessionInfoLocked(session_map_iter, params, &session_info,
                                  &session_key, sampling_selected);
            session_agg_info.sessionMap.insert(make_pair(session_key,
                                                         session_info));
            total_fwd_bytes +=
                session_info.get_forward_flow_info().get_logged_bytes();
            total_fwd_packets +=
                session_info.get_forward_flow_info().get_logged_pkts();
            total_rev_bytes +=
                session_info.get_reverse_flow_info().get_logged_bytes();
            total_rev_packets +=
                session_info.get_reverse_flow_info().get_logged_pkts();
            ++session_map_iter;
            ++session_count;
            if (session_count ==
                agent_uve_->agent()->params()->max_sessions_per_aggregate()) {
                exit = true;
                break;
            }
        }
        if (session_count) {
            FillSessionAggInfo(session_agg_map_iter, &session_agg_info,
                               &session_agg_key, total_fwd_bytes,
                               total_fwd_packets, total_rev_bytes,
                               total_rev_packets);
            session_ep.sess_agg_info.insert(make_pair(session_agg_key,
                                                      session_agg_info));
        }
        if (exit) {
            break;
        }
        session_iteration_key_.Reset();
        session_agg_map_iter++;
        ++session_agg_count;
        if (session_agg_count ==
            agent_uve_->agent()->params()->max_aggregates_per_session_endpoint()) {
            break;
        }
    }
    /* Don't export SessionEndpoint if there are 0 aggregates */
    if (session_ep.sess_agg_info.size()) {
        FillSessionEndpoint(it, &session_ep);
        EnqueueSessionMsg();
    }

    if (session_count ==
        agent_uve_->agent()->params()->max_sessions_per_aggregate()) {
        ep_completed = false;
        session_ep_iteration_key_ = it->first;
        session_agg_iteration_key_ = session_agg_map_iter->first;
        if (session_map_iter == session_agg_map_iter->second.session_map_.end()) {
            ++session_agg_map_iter;
            if (session_agg_map_iter == it->second.session_agg_map_.end()) {
                /* session_iteration_key_ and session_agg_iteration_key_ are
                 * both reset when ep_completed is returned as true in the
                 * calling function */
                ep_completed = true;
            } else {
                session_iteration_key_.Reset();
                session_agg_iteration_key_ = session_agg_map_iter->first;
            }
        } else {
            session_iteration_key_ = session_map_iter->first;
        }
    } else if (session_agg_count ==
               agent_uve_->agent()->params()->
               max_aggregates_per_session_endpoint()) {
        ep_completed = false;
        session_ep_iteration_key_ = it->first;
        if (session_agg_map_iter == it->second.session_agg_map_.end()) {
            /* session_iteration_key_ and session_agg_iteration_key_ are both
             * reset when ep_completed is returned as true in the calling
             * function */
            ep_completed = true;
        } else {
            session_agg_iteration_key_ = session_agg_map_iter->first;
            session_iteration_key_.Reset();
        }
    }
    return ep_completed;
}

uint32_t SessionStatsCollector::RunSessionEndpointStats(uint32_t max_count) {
    SessionEndpointMap::iterator it = session_endpoint_map_.
        lower_bound(session_ep_iteration_key_);
    if (it == session_endpoint_map_.end()) {
        it = session_endpoint_map_.begin();
    }
    if (it == session_endpoint_map_.end()) {
        return 0;
    }

    uint32_t count = 0;
    while (count < max_count) {
        if (it == session_endpoint_map_.end()) {
            break;
        }

        /* ProcessSessionEndpoint will build 1 SessionEndpoint message. This
         * may or may not include all aggregates and all sessions within each
         * aggregate. It returns true if the built message includes all
         * aggregates and all sessions of each aggregate */
        bool ep_completed = ProcessSessionEndpoint(it);
        ++count;
        if (ep_completed) {
            ++it;
            ++session_ep_visited_;
            session_agg_iteration_key_.Reset();
            session_iteration_key_.Reset();
        }
    }

    //Send any pending session export messages
    DispatchPendingSessionMsg();

    // Update iterator for next pass
    if (it == session_endpoint_map_.end()) {
        session_ep_iteration_key_.Reset();
    } else {
        session_ep_iteration_key_ = it->first;
    }

    session_task_ = NULL;
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
    ssc_->RunSessionEndpointStats(kSessionsPerTask);
    return true;
}

void SessionStatsCollector::GetFlowSandeshActionParams
    (const FlowAction &action_info, std::string &action_str) const {
    std::bitset<32> bs(action_info.action);
    for (unsigned int i = 0; i <= bs.size(); i++) {
        if (bs[i]) {
            if (!action_str.empty()) {
                action_str += "|";
            }
            action_str += TrafficAction::ActionToString(
                static_cast<TrafficAction::Action>(i));
        }
    }
}

bool SessionStatsCollector::IsSamplingEnabled() const {
    int32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
                        flow_export_rate();
    if (cfg_rate == GlobalVrouter::kDisableSampling) {
        return false;
    }
    return true;
}

bool SessionStatsCollector::SampleSession
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionStatsParams *params) const {
    params->sampled = false;
    int32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
                        flow_export_rate();
    /* If session export is disabled, update stats and return */
    if (!cfg_rate) {
        flow_stats_manager_->session_export_disable_drops_++;
        return false;
    }
    /* For session-sampling diff_bytes should consider the diff bytes for both
     * forward and reverse flow */
    uint64_t diff_bytes = params->fwd_flow.diff_bytes +
                          params->rev_flow.diff_bytes;
    const SessionStatsInfo &info = session_map_iter->second;
    /* Subject a flow to sampling algorithm only when all of below is met:-
     * a. actual session-export-rate is >= 80% of configured flow-export-rate.
     *    This is done only for first time.
     * b. diff_bytes is lesser than the threshold
     * c. Flow-sample does not have teardown time or the sample for the flow is
     *    not exported earlier.
     */
    bool subject_flows_to_algorithm = false;
    if ((diff_bytes < threshold()) &&
        (!info.teardown_time || !info.exported_atleast_once) &&
        ((!flow_stats_manager_->sessions_sampled_atleast_once_ &&
         flow_stats_manager_->session_export_rate() >= ((double)cfg_rate) * 0.8)
         || flow_stats_manager_->sessions_sampled_atleast_once_)) {
        subject_flows_to_algorithm = true;
        flow_stats_manager_->set_sessions_sampled_atleast_once();
    }

    if (subject_flows_to_algorithm) {
        params->sampled = true;
        double probability = diff_bytes/threshold();
        uint32_t num = rand() % threshold();
        if (num > diff_bytes) {
            /* Do not export the flow, if the random number generated is more
             * than the diff_bytes */
            flow_stats_manager_->session_export_sampling_drops_++;
            /* The second part of the if condition below is not required but
             * added for better readability. It is not required because
             * exported_atleast_once() will always be false if teardown time is
             * set. If both teardown_time and exported_atleast_once are true we
             * will never be here */
            if (info.teardown_time && !info.exported_atleast_once) {
                /* This counter indicates the number of sessions that were
                 * never exported */
                flow_stats_manager_->session_export_drops_++;
            }
            return false;
        }
        /* Normalize the diff_bytes and diff_packets reported using the
         * probability value */
        if (probability == 0) {
            params->fwd_flow.diff_bytes = 0;
            params->fwd_flow.diff_packets = 0;
            params->rev_flow.diff_bytes = 0;
            params->rev_flow.diff_packets = 0;
        } else {
            params->fwd_flow.diff_bytes = params->fwd_flow.diff_bytes/
                                                  probability;
            params->fwd_flow.diff_packets = params->fwd_flow.diff_packets/
                                                    probability;
            params->rev_flow.diff_bytes = params->rev_flow.diff_bytes/
                                                  probability;
            params->rev_flow.diff_packets = params->rev_flow.diff_packets/
                                                    probability;
        }
    }

    return true;
}

uint64_t SessionStatsCollector::threshold() const {
    return flow_stats_manager_->threshold();
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

void SessionStatsCollectorObject::RegisterDBClients() {
    for (int i = 0; i < kMaxSessionCollectors; i++) {
        if (collectors[i].get()) {
            collectors[i].get()->RegisterDBClients();
        }
    }
}

FlowEntry* SessionStatsReq::reverse_flow() const {
    FlowEntry *rflow = NULL;
    if (flow_.get()) {
        rflow = flow_->reverse_flow_entry();
    }
    return rflow;
}

bool FlowToSessionMap::IsEqual(FlowToSessionMap &rhs) {
    if (uuid_ != rhs.uuid()) {
        return false;
    }
    if (!(session_key_.IsEqual(rhs.session_key()))) {
        return false;
    }
    if (!(session_agg_key_.IsEqual(rhs.session_agg_key()))) {
        return false;
    }
    if (!(session_endpoint_key_.IsEqual(rhs.session_endpoint_key()))) {
        return false;
    }
     return true;
}

void SessionSloState::DeleteSessionSloStateRuleEntry(std::string uuid) {
    SessionSloRuleStateMap::iterator  it;
    it = session_rule_state_map_.find(uuid);
    if (it != session_rule_state_map_.end()) {
        session_rule_state_map_.erase(it);
    }
}

void SessionSloState::UpdateSessionSloStateRuleEntry(std::string uuid,
                                                     int rate) {
    SessionSloRuleStateMap::iterator  it;
    it = session_rule_state_map_.find(uuid);
    if (it == session_rule_state_map_.end()) {
        SessionSloRuleState slo_state_rule_entry = {};
        slo_state_rule_entry.rate = rate;
        session_rule_state_map_.insert(make_pair(uuid, slo_state_rule_entry));
    } else {
        SessionSloRuleState &prev = it->second;
        if (prev.rate != rate) {
            prev.rate = rate;
            prev.ref_count = 0;
        }
    }

}

bool SessionSloState::UpdateSessionSloStateRuleRefCount(
                      const std::string &uuid) {
    SessionSloRuleStateMap::iterator  it;
    bool is_logged = false;
    it = session_rule_state_map_.find(uuid);
    if (it != session_rule_state_map_.end()) {
        it->second.ref_count++;
        if (it->second.ref_count == it->second.rate) {
            is_logged = true;
        }
    }
    return is_logged;
}

