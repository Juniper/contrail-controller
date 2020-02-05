/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <bitset>
#include <pkt/flow_table.h>
#include <pkt/flow_mgmt/flow_mgmt_request.h>
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

// setting work queue max size as 4M
#define DEFAULT_SSC_REQUEST_QUEUE_SIZE 4*1024*1024
#define MAX_SSC_REQUEST_QUEUE_ITERATIONS 256

SandeshTraceBufferPtr SessionStatsTraceBuf(SandeshTraceBufferCreate(
    "SessionStats", 4000));

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
                       GetTaskId(kTaskSessionStatsCollectorEvent),
                       instance_id,
                       boost::bind(&SessionStatsCollector::RequestHandler,
                                   this, _1),
                       DEFAULT_SSC_REQUEST_QUEUE_SIZE,
                       MAX_SSC_REQUEST_QUEUE_ITERATIONS ),
        session_msg_list_(agent_uve_->agent()->params()->max_endpoints_per_session_msg(),
                          SessionEndpoint()),
        session_msg_index_(0), instance_id_(instance_id),
        flow_stats_manager_(aging_module), parent_(obj), session_task_(NULL),
        current_time_(GetCurrentTime()), session_task_starts_(0) {
        request_queue_.set_name("Session stats collector event queue");
        request_queue_.set_measure_busy_time
            (agent_uve_->agent()->MeasureQueueDelay());
        request_queue_.SetEntryCallback
            (boost::bind(&SessionStatsCollector::RequestHandlerEntry, this));
        request_queue_.SetExitCallback
            (boost::bind(&SessionStatsCollector::RequestHandlerExit, this, _1));
        request_queue_.SetBounded(true);
        InitDone();
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
        AddSession(flow, req->time());
        break;
    }

    case SessionStatsReq::DELETE_SESSION: {
        DeleteSession(flow, flow->uuid(), req->time(), &req->params());
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
    if (client_port != rhs.client_port) {
        return client_port < rhs.client_port;
    }
    return uuid < rhs.uuid;
}

bool SessionKey::IsEqual(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return false;
    }
    if (client_port != rhs.client_port) {
        return false;
    }
    if (uuid != rhs.uuid) {
        return false;
    }
    return true;
}

void SessionKey::Reset() {
    remote_ip = IpAddress();
    client_port = 0;
    uuid = boost::uuids::nil_uuid();
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
    const string &src_vn = !(fe->data().source_vn_match.empty()) ?
                            fe->data().source_vn_match :
                            fe->data().evpn_source_vn_match;
    const string &dst_vn = !(fe->data().dest_vn_match.empty()) ?
                            fe->data().dest_vn_match :
                            fe->data().evpn_dest_vn_match;

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
    session_key.uuid = fe->uuid();
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

static void BuildTraceTagList(const TagList &slist, vector<string> *dlist) {
    TagList::const_iterator it = slist.begin();
    while (it != slist.end()) {
        dlist->push_back(integerToString(*it));
        ++it;
    }
}

static void TraceSession(const string &op, const SessionEndpointKey &ep,
                         const SessionAggKey &agg, const SessionKey &session,
                         bool rev_flow_params) {
    SessionTraceInfo info;
    info.vmi = ep.vmi_cfg_name;
    info.local_vn = ep.local_vn;
    info.remote_vn = ep.remote_vn;
    BuildTraceTagList(ep.local_tagset, &info.local_tagset);
    BuildTraceTagList(ep.remote_tagset, &info.remote_tagset);
    info.remote_prefix = ep.remote_prefix;
    info.match_policy = ep.match_policy;
    info.is_si = ep.is_si;
    info.is_client = ep.is_client_session;
    info.local_ip = agg.local_ip.to_string();
    info.server_port = agg.server_port;
    info.protocol = agg.proto;
    info.remote_ip = session.remote_ip.to_string();
    info.client_port = session.client_port;
    info.flow_uuid = to_string(session.uuid);
    SESSION_STATS_TRACE(Trace, op, info, rev_flow_params);
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
        FlowToSessionMap rhs_flow_to_session_map(session_key,
                                                 session_agg_key,
                                                 session_endpoint_key);
        if (!(flow_to_session_map.IsEqual(rhs_flow_to_session_map))) {
            DeleteSession(fe_fwd, flow_to_session_map.session_key().uuid,
                          GetCurrentTime(), NULL);
        }
    }

    TraceSession("Add", session_endpoint_key, session_agg_key, session_key,
                 false);
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

void SessionStatsCollector::DeleteSession(FlowEntry* fe,
                                          const boost::uuids::uuid &del_uuid,
                                          uint64_t teardown_time,
                                          const RevFlowDepParams *params) {
    SessionAggKey session_agg_key;
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo  session_agg_info;
    SessionKey session_key;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;
    SessionEndpointInfo  session_endpoint_info;
    SessionEndpointKey session_endpoint_key;
    SessionEndpointMap::iterator session_endpoint_map_iter;
    bool read_flow = true;

    if (del_uuid != fe->uuid()) {
        read_flow = false;
    }

    /*
     * If the given flow uuid is different from the existing one
     * then ignore the read from flow
     */
    FlowSessionMap::iterator flow_session_map_iter;

    flow_session_map_iter = flow_session_map_.find(fe);
    if (flow_session_map_iter != flow_session_map_.end()) {
        if (del_uuid != flow_session_map_iter->second.session_key().uuid) {
            /* We had never seen ADD for del_uuid, ignore delete request. This
             * can happen when the following events occur
             * 1. Add with UUID x
             * 2. Delete with UUID x
             * 3. Add with UUID y
             * 4. Delete with UUID y
             * When events 2 and 3 are suppressed and we receive only 1 and 4
             * In this case initiated delete for entry with UUID x */
            read_flow = false;
            /* Reset params because they correspond to entry with UUID y */
            params = NULL;
        }
        session_endpoint_key = flow_session_map_iter->second.session_endpoint_key();
        session_agg_key = flow_session_map_iter->second.session_agg_key();
        session_key = flow_session_map_iter->second.session_key();
    } else {
        return;
    }
    if (params && params->action_info_.action == 0) {
        params = NULL;
    }

    bool params_valid = true;
    if (params == NULL) {
        params_valid = false;
    }

    TraceSession("Del", session_endpoint_key, session_agg_key, session_key,
                 params_valid);
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
                session_map_iter->second.deleted = true;
                /* Don't read stats for evicted flow, during delete */
                if (!session_map_iter->second.evicted) {
                    SessionStatsChangedUnlocked(session_map_iter,
                        &session_map_iter->second.del_stats);
                }
                if (read_flow) {
                    CopyFlowInfo(session_map_iter->second, params);
                }

                assert(session_map_iter->second.fwd_flow.flow.get() == fe);
                DeleteFlowToSessionMap(fe);
                session_map_iter->second.fwd_flow.flow = NULL;
                session_map_iter->second.rev_flow.flow = NULL;
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

    /* TODO: Evicted msg coming for reverse flow. We currently don't have
     * mapping from reverse_flow to flow_session_map */
    flow_session_map_iter = flow_session_map_.find(flow.get());
    if (flow_session_map_iter == flow_session_map_.end()) {
        return;
    }

    if (flow_session_map_iter->second.session_key().uuid != u) {
        return;
    }

    SessionEndpointKey session_db_ep_key = flow_session_map_iter->second.session_endpoint_key();
    SessionAggKey session_db_agg_key = flow_session_map_iter->second.session_agg_key();
    SessionKey session_db_key = flow_session_map_iter->second.session_key();

    session_ep_map_iter = session_endpoint_map_.find(session_db_ep_key);
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
                /*
                 * update the latest statistics
                 */
                SessionFlowStatsInfo &session_flow = session_map_iter->second.fwd_flow;
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

                SessionStatsParams &estats = session_map_iter->second.
                    evict_stats;
                session_map_iter->second.evicted = true;
                estats.fwd_flow.valid = true;
                estats.fwd_flow.diff_bytes = diff_bytes;
                estats.fwd_flow.diff_packets = diff_packets;
            }
        }
    }
}

void SessionStatsCollector::AddFlowToSessionMap(FlowEntry *fe,
                                                SessionKey session_key,
                                                SessionAggKey session_agg_key,
                                                SessionEndpointKey session_endpoint_key) {
    FlowToSessionMap flow_to_session_map(session_key, session_agg_key,
                                         session_endpoint_key);
    std::pair<FlowSessionMap::iterator, bool> ret =
        flow_session_map_.insert(make_pair(fe, flow_to_session_map));
    if (ret.second == false) {
        FlowToSessionMap &prev = ret.first->second;
        assert(prev.session_key().uuid == fe->uuid());
    }
}

void SessionStatsCollector::DeleteFlowToSessionMap(FlowEntry *fe) {
    FlowSessionMap::iterator flow_session_map_iter;
    flow_session_map_iter = flow_session_map_.find(fe);
    if (flow_session_map_iter != flow_session_map_.end()) {
        flow_session_map_.erase(flow_session_map_iter);
    }
}

int SessionStatsCollector::ComputeSloRate(int rate, SecurityLoggingObject *slo)
    const {
    /* If rate is not configured, it will have -1 as value
     * -1 as value. In this case, pick the rate from SLO */
    if (rate == -1) {
        rate = slo->rate();
    }
    return rate;
}

void SessionStatsCollector::UpdateSloStateRules(SecurityLoggingObject *slo,
                                                SessionSloState *state) {
    vector<autogen::SecurityLoggingObjectRuleEntryType>::const_iterator it;
    it = slo->rules().begin();
    while (it != slo->rules().end()) {
        state->UpdateSessionSloStateRuleEntry(it->rule_uuid, it->rate);
        it++;
    }

    SloRuleList::const_iterator acl_it = slo->firewall_policy_list().begin();
    while (acl_it != slo->firewall_policy_list().end()) {
        AclKey key(acl_it->uuid_);
        AclDBEntry *acl = static_cast<AclDBEntry *>(agent_uve_->agent()->
                        acl_table()->FindActiveEntry(&key));
        if (acl) {
            int index = 0;
            int rate = ComputeSloRate(acl_it->rate_, slo);
            const AclEntry *ae = acl->GetAclEntryAtIndex(index);
            while (ae != NULL) {
                state->UpdateSessionSloStateRuleEntry(ae->uuid(), rate);
                index++;
                ae = acl->GetAclEntryAtIndex(index);
            }
        }
        acl_it++;
    }

    SloRuleList::const_iterator fw_rule_it;
    fw_rule_it = slo->firewall_rule_list().begin();
    while (fw_rule_it != slo->firewall_rule_list().end()) {
        const SloRuleInfo &item = *fw_rule_it;
        int rate = ComputeSloRate(item.rate_, slo);
        state->UpdateSessionSloStateRuleEntry(to_string(item.uuid_), rate);
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

void SessionStatsCollector::AddSloFirewallPolicies(SecurityLoggingObject *slo,
                                                   SessionSloRuleMap *r_map) {
    const SloRuleList &list = slo->firewall_policy_list();
    SloRuleList::const_iterator acl_it = list.begin();
    while (acl_it != list.end()) {
        AclKey key(acl_it->uuid_);
        AclDBEntry *acl = static_cast<AclDBEntry *>(agent_uve_->agent()->
                        acl_table()->FindActiveEntry(&key));
        if (acl) {
            int index = 0;
            const AclEntry *ae = acl->GetAclEntryAtIndex(index);
            int rate = ComputeSloRate(acl_it->rate_, slo);
            while (ae != NULL) {
                AddSessionSloRuleEntry(ae->uuid(), rate, slo, r_map);
                index++;
                ae = acl->GetAclEntryAtIndex(index);
            }
        }
        acl_it++;
    }
}

void SessionStatsCollector::AddSloFirewallRules(SecurityLoggingObject *slo,
                                                SessionSloRuleMap *rule_map) {
    const SloRuleList &list = slo->firewall_rule_list();
    SloRuleList::const_iterator it = list.begin();
    while (it != list.end()) {
        int rate = ComputeSloRate(it->rate_, slo);
        AddSessionSloRuleEntry(to_string(it->uuid_), rate, slo, rule_map);
        it++;
    }
}

void SessionStatsCollector::AddSloEntryRules(SecurityLoggingObject *slo,
                                             SessionSloRuleMap *slo_rule_map) {
    AddSloRules(slo->rules(), slo, slo_rule_map);
    AddSloFirewallPolicies(slo, slo_rule_map);
    AddSloFirewallRules(slo, slo_rule_map);
}

void SessionStatsCollector::AddSloEntry(const boost::uuids::uuid &uuid,
                                       SessionSloRuleMap *slo_rule_map) {
    SecurityLoggingObjectKey slo_key(uuid);
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>
        (agent_uve_->agent()->slo_table()->FindActiveEntry(&slo_key));
    if (slo) {
        if (slo->status()) {
            AddSloEntryRules(slo, slo_rule_map);
        }
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

void SessionStatsCollector::MakeSloList(const FlowEntry *fe,
                                        SessionSloRuleMap *vmi_session_slo_rule_map,
                                        SessionSloRuleMap *vn_session_slo_rule_map) {
    if (fe == NULL) {
        return;
    }
    const Interface *itf = fe->intf_entry();
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
    return;
}

void SessionStatsCollector::BuildSloList(
                                const SessionStatsInfo    &stats_info,
                                const FlowEntry *fe,
                                SessionSloRuleMap *global_session_slo_rule_map,
                                SessionSloRuleMap *vmi_session_slo_rule_map,
                                SessionSloRuleMap *vn_session_slo_rule_map) {

    vmi_session_slo_rule_map->clear();
    vn_session_slo_rule_map->clear();
    global_session_slo_rule_map->clear();

    if (agent_uve_->agent()->oper_db()->global_vrouter()->slo_uuid() !=
        boost::uuids::nil_uuid()) {
        AddSloEntry(
                agent_uve_->agent()->oper_db()->global_vrouter()->slo_uuid(),
                global_session_slo_rule_map);
    }

    if (stats_info.deleted) {
        AddSloList(stats_info.export_info.vmi_slo_list, vmi_session_slo_rule_map);
        AddSloList(stats_info.export_info.vn_slo_list, vn_session_slo_rule_map);
    } else {
        MakeSloList(fe, vmi_session_slo_rule_map, vn_session_slo_rule_map);
    }
}

bool SessionStatsCollector::UpdateSloMatchRuleEntry(
                            const boost::uuids::uuid &slo_uuid,
                            const std::string &match_uuid,
                            bool *match) {
    SecurityLoggingObjectKey slo_key(slo_uuid);
    SecurityLoggingObject *slo = static_cast<SecurityLoggingObject *>
        (agent_uve_->agent()->slo_table()->FindActiveEntry(&slo_key));
    if (slo) {
        SessionSloState *state =
            static_cast<SessionSloState *>(slo->GetState(agent_uve_->agent()->slo_table(),
                                                         slo_listener_id_));
        if (state) {
            return state->UpdateSessionSloStateRuleRefCount(match_uuid, match);
        }
    }
    *match = false;
    return false;
}

bool SessionStatsCollector::CheckPolicyMatch(const SessionSloRuleMap &map,
                                             const std::string &policy_uuid,
                                             const bool &deleted_flag,
                                             bool *match,
                                             const bool &exported_once) {
    SessionSloRuleMap::const_iterator it;
    if (!policy_uuid.empty()) {
        it = map.find(policy_uuid);
        if (it != map.end()) {
            /* Always logging tear down session, which is exported atleast once
             * earlier will be logged other tear down sessions will be reported
             * with SLO rate checking
             */
            if (deleted_flag && exported_once) {
                *match = true;
                return true;
            }
            return UpdateSloMatchRuleEntry(it->second.slo_uuid, policy_uuid, match);
        }
    }
    *match = false;
    return false;
}

bool SessionStatsCollector::FindSloMatchRule(const SessionSloRuleMap &map,
                                             const std::string &fw_policy_uuid,
                                             const std::string &nw_policy_uuid,
                                             const std::string &sg_policy_uuid,
                                             const bool &deleted_flag,
                                             bool *match,
                                             const bool &exported_once) {
    SessionSloRuleMap::const_iterator it;
    bool fw_logged = false, nw_logged = false, sg_logged = false;
    bool fw_match = false, nw_match = false, sg_match = false;

    fw_logged = CheckPolicyMatch(map, fw_policy_uuid, deleted_flag,
                                 &fw_match, exported_once);
    nw_logged = CheckPolicyMatch(map, nw_policy_uuid, deleted_flag,
                                 &nw_match, exported_once);
    sg_logged = CheckPolicyMatch(map, sg_policy_uuid, deleted_flag,
                                 &sg_match, exported_once);

    if (fw_match || nw_match || sg_match) {
        *match = true;
    } else {
        *match = false;
    }

    if (fw_logged || nw_logged || sg_logged) {
        return true;
    }
    return false;
}

bool SessionStatsCollector::MatchSloForFlow(
                            const SessionStatsInfo    &stats_info,
                            const FlowEntry *fe,
                            const std::string &fw_policy_uuid,
                            const std::string &nw_policy_uuid,
                            const std::string &sg_policy_uuid,
                            const bool &deleted_flag,
                            bool *logged,
                            const bool &exported_once) {

    bool is_vmi_slo_logged, is_vn_slo_logged, is_global_slo_logged;
    bool vmi_slo_match, vn_slo_match, global_slo_match;
    SessionSloRuleMap vmi_session_slo_rule_map;
    SessionSloRuleMap vn_session_slo_rule_map;
    SessionSloRuleMap global_session_slo_rule_map;

    /*
     * Get the list of slos need to be matched for the given flow
     */
    BuildSloList(stats_info, fe,
                 &global_session_slo_rule_map,
                 &vmi_session_slo_rule_map,
                 &vn_session_slo_rule_map);
    /*
     * Match each type of policy for the given flow against the slo list
     */

    is_vmi_slo_logged = FindSloMatchRule(vmi_session_slo_rule_map,
                                         fw_policy_uuid,
                                         nw_policy_uuid,
                                         sg_policy_uuid,
                                         deleted_flag,
                                         &vmi_slo_match,
                                         exported_once);

    is_vn_slo_logged = FindSloMatchRule(vn_session_slo_rule_map,
                                        fw_policy_uuid,
                                        nw_policy_uuid,
                                        sg_policy_uuid,
                                        deleted_flag,
                                        &vn_slo_match,
                                        exported_once);

    is_global_slo_logged = FindSloMatchRule(global_session_slo_rule_map,
                                            fw_policy_uuid,
                                            nw_policy_uuid,
                                            sg_policy_uuid,
                                            deleted_flag,
                                            &global_slo_match,
                                            exported_once);
    if ((is_vmi_slo_logged) ||
        (is_vn_slo_logged) ||
        (is_global_slo_logged)) {
        *logged = true;
    }
    if (vmi_slo_match || vn_slo_match || global_slo_match) {
         return true;
    } else {
        return false;
    }
}

void SessionStatsCollector::GetPolicyIdFromDeletedFlow(
                            const SessionFlowExportInfo &flow_info,
                            std::string &fw_policy_uuid,
                            std::string &nw_policy_uuid,
                            std::string &sg_policy_uuid) {
    fw_policy_uuid = flow_info.aps_rule_uuid;
    sg_policy_uuid = flow_info.sg_rule_uuid;
    nw_policy_uuid = flow_info.nw_ace_uuid;
    return;
}

void SessionStatsCollector::GetPolicyIdFromFlow(
                            const FlowEntry *fe,
                            std::string &fw_policy_uuid,
                            std::string &nw_policy_uuid,
                            std::string &sg_policy_uuid) {
    fw_policy_uuid = fe->fw_policy_uuid();
    sg_policy_uuid = fe->sg_rule_uuid();
    nw_policy_uuid = fe->nw_ace_uuid();
    return;
}

bool SessionStatsCollector::FlowLogging(
                            const SessionStatsInfo    &stats_info,
                            const FlowEntry *fe,
                            bool  *logged,
                            const bool  &exported_once) {

    bool matched = false, deleted_flag=false;
    std::string fw_policy_uuid = "", nw_policy_uuid = "", sg_policy_uuid = "";

    GetPolicyIdFromFlow(fe,
                        fw_policy_uuid,
                        nw_policy_uuid,
                        sg_policy_uuid);

    matched = MatchSloForFlow(stats_info,
                              fe,
                              fw_policy_uuid,
                              nw_policy_uuid,
                              sg_policy_uuid,
                              deleted_flag,
                              logged,
                              exported_once);

    return matched;
}

bool SessionStatsCollector::DeletedFlowLogging(
                            const SessionStatsInfo    &stats_info,
                            const SessionFlowExportInfo &flow_info,
                            bool  *logged,
                            const bool  &exported_once) {

    bool matched = false, deleted_flag = true;
    std::string fw_policy_uuid = "", nw_policy_uuid = "", sg_policy_uuid = "";

    GetPolicyIdFromDeletedFlow(flow_info,
                               fw_policy_uuid,
                               nw_policy_uuid,
                               sg_policy_uuid);

    matched = MatchSloForFlow(stats_info,
                              NULL,
                              fw_policy_uuid,
                              nw_policy_uuid,
                              sg_policy_uuid,
                              deleted_flag,
                              logged,
                              exported_once);

    return matched;
}

bool SessionStatsCollector::HandleDeletedFlowLogging(
                            const SessionStatsInfo    &stats_info) {

    bool logged = false;
    const SessionExportInfo &info = stats_info.export_info;

    /*
     * Deleted flow need to to be just checked whether SLO rules matched
     * If SLO is macthed, it should be logged irrespective of the rate
     */
    if (DeletedFlowLogging(stats_info,
                           info.fwd_flow,
                           &logged,
                           stats_info.exported_atleast_once)) {
        CheckFlowLogging(logged);
    } else if (DeletedFlowLogging(stats_info,
                                  info.rev_flow,
                                  &logged,
                                  stats_info.exported_atleast_once)) {
        CheckFlowLogging(logged);
    }
    return false;
}

bool SessionStatsCollector::HandleFlowLogging(
                            const SessionStatsInfo    &stats_info) {
    bool logged = false;

    /*
     * FWD and REV flow of the Session need to be checked for SLO
     * separately. If FWD flow matches or logged then rev flow
     * is not required to check for SLO match.
     * REV flow will be checked for SLO only when FWD flow
     * is not matched for the SLO, since SLO is per session
     */

    if (FlowLogging(stats_info,
                    stats_info.fwd_flow.flow.get(),
                    &logged,
                    stats_info.exported_atleast_once)) {
        CheckFlowLogging(logged);
    } else if (FlowLogging(stats_info,
                           stats_info.rev_flow.flow.get(),
                           &logged,
                           stats_info.exported_atleast_once)) {
        CheckFlowLogging(logged);
    }
    return false;
}

bool SessionStatsCollector::CheckSessionLogging(
                            const SessionStatsInfo    &stats_info) {

    if (!agent_uve_->agent()->global_slo_status()) {
        /* SLO is not enabled */
        flow_stats_manager_->session_global_slo_logging_drops_++;
        return false;
    }

    /*
     * Deleted flow will be logged if SLO is configured.
     * Normal case will be logged only when there is a change in the
     * stats. If there is no change in the session stats, it will
     * not be considered to SLO match and rate. This will avoid logging
     * of each session at least once. Also, idle session will not be
     * considered for the rate count
     */

    if (stats_info.deleted) {
        if(HandleDeletedFlowLogging(stats_info)) {
            return true;
        }
    } else if(HandleFlowLogging(stats_info)) {
        return true;
    }

    flow_stats_manager_->session_slo_logging_drops_++;
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

void SessionStatsCollector::CopyFlowInfoInternal(SessionFlowExportInfo *info,
                                                 const boost::uuids::uuid &u,
                                                 FlowEntry *fe) const {
    if (fe->uuid() != u) {
        return;
    }
    FlowTable::GetFlowSandeshActionParams(fe->data().match_p.action_info,
                                          info->action);
    info->sg_rule_uuid = fe->sg_rule_uuid();
    info->nw_ace_uuid = fe->nw_ace_uuid();
    info->aps_rule_uuid = fe->fw_policy_uuid();
    if (FlowEntry::ShouldDrop(fe->data().match_p.action_info.action)) {
        info->drop_reason = FlowEntry::DropReasonStr(fe->data().drop_reason);
    }
}

void SessionStatsCollector::CopyFlowInfo(SessionStatsInfo &session,
                                         const RevFlowDepParams *params) {
    SessionExportInfo &info = session.export_info;
    FlowEntry *fe = session.fwd_flow.flow.get();
    FlowEntry *rfe = session.rev_flow.flow.get();
    info.valid = true;

    const Interface *itf = fe->intf_entry();
    if ((itf !=  NULL) && (itf->type() == Interface::VM_INTERFACE)) {
        const VmInterface *vmi = static_cast<const VmInterface *>(itf);
        if (vmi != NULL) {
            info.vmi_slo_list = vmi->slo_list();
            if (vmi->vn()) {
                info.vn_slo_list = vmi->vn()->slo_list();
            }
        }
    }

    if (fe->IsIngressFlow()) {
        info.vm_cfg_name = fe->data().vm_cfg_name;
    } else if (rfe) {
        /* TODO: vm_cfg_name should be passed in RevFlowDepParams because rfe
         * may now point to different UUID altogether */
        info.vm_cfg_name = rfe->data().vm_cfg_name;
    }
    string rid = agent_uve_->agent()->router_id().to_string();
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        info.other_vrouter = rid;
    } else {
        info.other_vrouter = fe->peer_vrouter();
    }
    info.underlay_proto = fe->tunnel_type().GetType();
    CopyFlowInfoInternal(&info.fwd_flow, session.fwd_flow.uuid, fe);
    if (params) {
        FlowTable::GetFlowSandeshActionParams(params->action_info_,
                                              info.rev_flow.action);
        info.rev_flow.sg_rule_uuid = params->sg_uuid_;
        info.rev_flow.nw_ace_uuid = params->nw_ace_uuid_;
        if (FlowEntry::ShouldDrop(params->action_info_.action)) {
            info.rev_flow.drop_reason = FlowEntry::DropReasonStr(params->
                                                                 drop_reason_);
        }
    } else if (rfe) {
        CopyFlowInfoInternal(&info.rev_flow, session.rev_flow.uuid, rfe);
    }
}

void SessionStatsCollector::FillSessionFlowStats
(const SessionFlowStatsParams &stats, SessionFlowInfo *flow_info,
 bool is_sampling, bool is_logging) const {
    if (!stats.valid) {
        return;
    }
    flow_info->set_tcp_flags(stats.tcp_flags);
    flow_info->set_underlay_source_port(stats.underlay_src_port);
    if (is_sampling) {
        flow_info->set_sampled_pkts(stats.diff_packets);
        flow_info->set_sampled_bytes(stats.diff_bytes);
    }
    if (is_logging) {
        flow_info->set_logged_pkts(stats.diff_packets);
        flow_info->set_logged_bytes(stats.diff_bytes);
    }
}

void SessionStatsCollector::FillSessionFlowInfo
(const SessionFlowStatsInfo &session_flow, const SessionStatsInfo &sinfo,
 const SessionFlowExportInfo &einfo, SessionFlowInfo *flow_info) const {
    FlowEntry *fe = session_flow.flow.get();
    std::string action_str, drop_reason = "";

    flow_info->set_flow_uuid(session_flow.uuid);
    flow_info->set_setup_time(sinfo.setup_time);
    if (sinfo.teardown_time) {
        flow_info->set_teardown_time(sinfo.teardown_time);
        flow_info->set_teardown_bytes(session_flow.total_bytes);
        flow_info->set_teardown_pkts(session_flow.total_packets);
    }
    if (sinfo.deleted) {
        if (!sinfo.export_info.valid) {
            return;
        }
        if (!einfo.action.empty()) {
            flow_info->set_action(einfo.action);
        }
        if (!einfo.sg_rule_uuid.empty()) {
            flow_info->set_sg_rule_uuid(StringToUuid(einfo.sg_rule_uuid));
        }
        if (!einfo.nw_ace_uuid.empty()) {
            flow_info->set_nw_ace_uuid(StringToUuid(einfo.nw_ace_uuid));
        }
        if (!einfo.drop_reason.empty()) {
            flow_info->set_drop_reason(einfo.drop_reason);
        }
    } else {
        FlowTable::GetFlowSandeshActionParams(fe->data().match_p.action_info,
                                              action_str);
        flow_info->set_action(action_str);
        flow_info->set_sg_rule_uuid(StringToUuid(fe->sg_rule_uuid()));
        flow_info->set_nw_ace_uuid(StringToUuid(fe->nw_ace_uuid()));
        if (FlowEntry::ShouldDrop(fe->data().match_p.action_info.action)) {
            drop_reason = FlowEntry::DropReasonStr(fe->data().drop_reason);
            flow_info->set_drop_reason(drop_reason);
        }
    }
}

bool SessionStatsCollector::CheckAndDeleteSessionStatsFlow(
    SessionPreAggInfo::SessionMap::iterator session_map_iter) {
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    if (fe->deleted()) {
        DeleteSession(fe, session_map_iter->first.uuid,
                          GetCurrentTime(), NULL);
        return true;
    }
    return false;
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

    bool fwd_updated = FetchFlowStats(&session_map_iter->second.fwd_flow,
                                      &params->fwd_flow);
    bool rev_updated = FetchFlowStats(&session_map_iter->second.rev_flow,
                                      &params->rev_flow);
    return (fwd_updated || rev_updated);
}

bool SessionStatsCollector::FetchFlowStats
(SessionFlowStatsInfo *info, SessionFlowStatsParams *params) const {
    vr_flow_stats k_stats;
    KFlowData kinfo;
    uint64_t k_bytes, bytes, k_packets;
    const vr_flow_entry *k_flow = NULL;
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
        ksync_flow_memory();
    /* Update gen-id and flow-handle before reading stats using them. For
     * reverse-flow, it is possible that flow-handle is not set yet
     */
    FlowEntry *fe = info->flow.get();
    if (fe && (info->uuid == fe->uuid())) {
        info->flow_handle = fe->flow_handle();
        info->gen_id = fe->gen_id();
    }

    k_flow = ksync_obj->GetKFlowStatsAndInfo(info->flow->key(),
                                             info->flow_handle,
                                             info->gen_id, &k_stats, &kinfo);
    if (!k_flow) {
        SandeshFlowKey skey;
        skey.set_nh(info->flow->key().nh);
        skey.set_sip(info->flow->key().src_addr.to_string());
        skey.set_dip(info->flow->key().dst_addr.to_string());
        skey.set_src_port(info->flow->key().src_port);
        skey.set_dst_port(info->flow->key().dst_port);
        skey.set_protocol(info->flow->key().protocol);
        SESSION_STATS_TRACE(Err, "Fetching stats failed", info->flow_handle,
                            info->gen_id, skey);
        return false;
    }

    k_bytes = FlowStatsCollector::GetFlowStats(k_stats.flow_bytes_oflow,
                                               k_stats.flow_bytes);
    k_packets = FlowStatsCollector::GetFlowStats(k_stats.flow_packets_oflow,
                                                 k_stats.flow_packets);

    bytes = 0x0000ffffffffffffULL & info->total_bytes;

    if (bytes != k_bytes) {
        uint64_t total_bytes = GetUpdatedSessionFlowBytes(info->total_bytes,
                                                          k_bytes);
        uint64_t total_packets = GetUpdatedSessionFlowPackets
            (info->total_packets, k_packets);
        params->diff_bytes = total_bytes - info->total_bytes;
        params->diff_packets = total_packets - info->total_packets;
        info->total_bytes = total_bytes;
        info->total_packets = total_packets;
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
     SessionIpPort *session_key, bool is_sampling, bool is_logging) const {
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    FillSessionInfoUnlocked(session_map_iter, stats, session_info, session_key, NULL,
                            true, is_sampling, is_logging);
}

void SessionStatsCollector::FillSessionEvictStats
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionInfo *session_info, bool is_sampling, bool is_logging) const {
    const SessionStatsParams &estats = session_map_iter->second.evict_stats;
    if (!estats.fwd_flow.valid) {
        return;
    }
    if (is_logging) {
        session_info->forward_flow_info.set_logged_pkts(estats.fwd_flow.
                                                        diff_packets);
        session_info->forward_flow_info.set_logged_bytes(estats.fwd_flow.
                                                         diff_bytes);
        /* TODO: Evict stats for reverse flow is not supported yet */
        session_info->reverse_flow_info.set_logged_pkts(0);
        session_info->reverse_flow_info.set_logged_bytes(0);
    }
    if (is_sampling) {
        session_info->forward_flow_info.set_sampled_pkts(estats.fwd_flow.
                                                         diff_packets);
        session_info->forward_flow_info.set_sampled_bytes(estats.fwd_flow.
                                                          diff_bytes);
        /* TODO: Evict stats for reverse flow is not supported yet */
        session_info->reverse_flow_info.set_sampled_pkts(0);
        session_info->reverse_flow_info.set_sampled_bytes(0);
    }
}

void SessionStatsCollector::FillSessionInfoUnlocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     const SessionStatsParams &stats,
     SessionInfo *session_info,
     SessionIpPort *session_key,
     const RevFlowDepParams *params,
     bool read_flow, bool is_sampling, bool is_logging) const {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    boost::system::error_code ec;
    /*
     * Fill the session Key
     */
    session_key->set_ip(session_map_iter->first.remote_ip);
    session_key->set_port(session_map_iter->first.client_port);
    FillSessionFlowInfo(session_map_iter->second.fwd_flow,
                        session_map_iter->second,
                        session_map_iter->second.export_info.fwd_flow,
                        &session_info->forward_flow_info);
    FillSessionFlowInfo(session_map_iter->second.rev_flow,
                        session_map_iter->second,
                        session_map_iter->second.export_info.rev_flow,
                        &session_info->reverse_flow_info);
    bool first_time_export = false;
    if (!session_map_iter->second.exported_atleast_once) {
        first_time_export = true;
        /* Mark the flow as exported */
        session_map_iter->second.exported_atleast_once = true;
    }

    const bool &evicted = session_map_iter->second.evicted;
    const bool &deleted = session_map_iter->second.deleted;
    if (evicted) {
        const SessionStatsParams &estats = session_map_iter->second.evict_stats;
        FillSessionEvictStats(session_map_iter, session_info, is_sampling,
                              is_logging);
        flow_stats_manager_->UpdateSessionExportStats(1, first_time_export,
                                                      estats.sampled);
    } else {
        const SessionStatsParams *real_stats = &stats;
        if (deleted) {
            real_stats = &session_map_iter->second.del_stats;
        }
        FillSessionFlowStats(real_stats->fwd_flow,
                             &session_info->forward_flow_info, is_sampling,
                             is_logging);
        FillSessionFlowStats(real_stats->rev_flow,
                             &session_info->reverse_flow_info, is_sampling,
                             is_logging);
        flow_stats_manager_->UpdateSessionExportStats(1, first_time_export,
                                                      real_stats->sampled);
    }
    if (deleted) {
        SessionExportInfo &info = session_map_iter->second.export_info;
        if (info.valid) {
            if (!info.vm_cfg_name.empty()) {
                session_info->set_vm(info.vm_cfg_name);
            }
            session_info->set_other_vrouter_ip(
                AddressFromString(info.other_vrouter, &ec));
            session_info->set_underlay_proto(info.underlay_proto);
        }
    } else {
        session_info->set_vm(fe->data().vm_cfg_name);
        if (fe->is_flags_set(FlowEntry::LocalFlow)) {
            session_info->set_other_vrouter_ip(AddressFromString(rid, &ec));
        } else {
            /* For Egress flows, pick VM name from reverse flow */
            if (!fe->IsIngressFlow() && rfe) {
                session_info->set_vm(rfe->data().vm_cfg_name);
            }
            session_info->set_other_vrouter_ip(
                 AddressFromString(fe->peer_vrouter(), &ec));
        }
        session_info->set_underlay_proto(fe->tunnel_type().GetType());
    }
}

void SessionStatsCollector::UpdateAggregateStats(const SessionInfo &sinfo,
                                                 SessionAggInfo *agg_info,
                                                 bool is_sampling,
                                                 bool is_logging) const {
    if (is_sampling) {
        agg_info->set_sampled_forward_bytes(agg_info->get_sampled_forward_bytes() +
            sinfo.get_forward_flow_info().get_sampled_bytes());
        agg_info->set_sampled_forward_pkts(agg_info->get_sampled_forward_pkts() +
            sinfo.get_forward_flow_info().get_sampled_pkts());
        agg_info->set_sampled_reverse_bytes(agg_info->get_sampled_reverse_bytes() +
            sinfo.get_reverse_flow_info().get_sampled_bytes());
        agg_info->set_sampled_reverse_pkts(agg_info->get_sampled_reverse_pkts() +
            sinfo.get_reverse_flow_info().get_sampled_pkts());
    }
    if (is_logging) {
        agg_info->set_logged_forward_bytes(agg_info->get_logged_forward_bytes() +
            sinfo.get_forward_flow_info().get_logged_bytes());
        agg_info->set_logged_forward_pkts(agg_info->get_logged_forward_pkts() +
            sinfo.get_forward_flow_info().get_logged_pkts());
        agg_info->set_logged_reverse_bytes(agg_info->get_logged_reverse_bytes() +
            sinfo.get_reverse_flow_info().get_logged_bytes());
        agg_info->set_logged_reverse_pkts(agg_info->get_logged_reverse_pkts() +
            sinfo.get_reverse_flow_info().get_logged_pkts());
    }
}

void SessionStatsCollector::FillSessionAggInfo
(SessionEndpointInfo::SessionAggMap::iterator it, SessionIpPortProtocol *key)
 const {
    /*
     * Fill the session agg key
     */
    key->set_local_ip(it->first.local_ip);
    key->set_service_port(it->first.server_port);
    key->set_protocol(it->first.proto);
}

void SessionStatsCollector::FillSessionTags(const TagList &list,
                                            SessionEndpoint *ep) const {
    UveTagData tinfo(UveTagData::SET);
    agent_uve_->BuildTagNamesFromList(list, &tinfo);
    if (!tinfo.application.empty()) {
        ep->set_application(tinfo.application);
    }
    if (!tinfo.tier.empty()) {
        ep->set_tier(tinfo.tier);
    }
    if (!tinfo.site.empty()) {
        ep->set_site(tinfo.site);
    }
    if (!tinfo.deployment.empty()) {
        ep->set_deployment(tinfo.deployment);
    }
    if (tinfo.label_set.size() != 0) {
        ep->set_labels(tinfo.label_set);
    }
    if (tinfo.custom_tag_set.size() != 0) {
        ep->set_custom_tags(tinfo.custom_tag_set);
    }
}

void SessionStatsCollector::FillSessionRemoteTags(const TagList &list,
                                                  SessionEndpoint *ep) const {
    UveTagData tinfo(UveTagData::SET);
    agent_uve_->BuildTagIdsFromList(list, &tinfo);
    if (!tinfo.application.empty()){
        ep->set_remote_application(tinfo.application);
    }
    if (!tinfo.tier.empty()){
        ep->set_remote_tier(tinfo.tier);
    }
    if (!tinfo.site.empty()){
        ep->set_remote_site(tinfo.site);
    }
    if (!tinfo.deployment.empty()){
        ep->set_remote_deployment(tinfo.deployment);
    }
    if (tinfo.label_set.size() != 0) {
        ep->set_remote_labels(tinfo.label_set);
    }
    if (tinfo.custom_tag_set.size() != 0) {
        ep->set_remote_custom_tags(tinfo.custom_tag_set);
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
    if (!it->first.remote_prefix.empty()) {
        session_ep->set_remote_prefix(it->first.remote_prefix);
    }
    session_ep->set_security_policy_rule(it->first.match_policy);
    if (it->first.local_tagset.size() > 0) {
        FillSessionTags(it->first.local_tagset, session_ep);
    }
    if (it->first.remote_tagset.size() > 0) {
        FillSessionRemoteTags(it->first.remote_tagset, session_ep);
    }
    session_ep->set_vrouter_ip(AddressFromString(rid, &ec));
}

bool SessionStatsCollector::ProcessSessionEndpoint
    (const SessionEndpointMap::iterator &it) {
    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionEndpointInfo::SessionAggMap::iterator prev_agg_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter, prev;

    SessionInfo session_info;
    SessionIpPort session_key;
    uint32_t session_count = 0, session_agg_count = 0;
    bool exit = false, ep_completed = true;

    SessionEndpoint &session_ep = session_msg_list_[GetSessionMsgIdx()];

    session_agg_map_iter = it->second.session_agg_map_.
        lower_bound(session_agg_iteration_key_);
    while (session_agg_map_iter != it->second.session_agg_map_.end()) {
        SessionAggInfo session_agg_info;
        SessionIpPortProtocol session_agg_key;
        session_count = 0;
        session_map_iter = session_agg_map_iter->second.session_map_.
            lower_bound(session_iteration_key_);
        while (session_map_iter != session_agg_map_iter->second.session_map_.end()) {
            prev = session_map_iter;
            SessionStatsParams params;
            if (!session_map_iter->second.deleted &&
                !session_map_iter->second.evicted) {
                bool delete_marked = CheckAndDeleteSessionStatsFlow(session_map_iter);
                if (!delete_marked) {
                    bool changed = SessionStatsChangedLocked(session_map_iter,
                                                    &params);
                    if (!changed && session_map_iter->second.exported_atleast_once) {
                        ++session_map_iter;
                        continue;
                    }
                }
            }

            bool is_sampling = true;
            if (IsSamplingEnabled()) {
                is_sampling = SampleSession(session_map_iter, &params);
            }
            bool is_logging = CheckSessionLogging(session_map_iter->second);

            /* Ignore session export if sampling & logging drop the session */
            if (!is_sampling && !is_logging) {
                ++session_map_iter;
                if (prev->second.deleted) {
                    session_agg_map_iter->second.session_map_.erase(prev);
                }
                continue;
            }
            if (session_map_iter->second.deleted) {
                FillSessionInfoUnlocked(session_map_iter, params, &session_info,
                                        &session_key, NULL, true, is_sampling,
                                        is_logging);
            } else {
                FillSessionInfoLocked(session_map_iter, params, &session_info,
                                      &session_key, is_sampling, is_logging);
            }
            session_agg_info.sessionMap.insert(make_pair(session_key,
                                                         session_info));
            UpdateAggregateStats(session_info, &session_agg_info, is_sampling,
                                 is_logging);
            ++session_map_iter;
            ++session_count;
            if (prev->second.deleted) {
                session_agg_map_iter->second.session_map_.erase(prev);
            }
            if (session_count ==
                agent_uve_->agent()->params()->max_sessions_per_aggregate()) {
                exit = true;
                break;
            }
        }
        if (session_count) {
            FillSessionAggInfo(session_agg_map_iter, &session_agg_key);
            session_ep.sess_agg_info.insert(make_pair(session_agg_key,
                                                      session_agg_info));
        }
        if (exit) {
            break;
        }
        session_iteration_key_.Reset();
        prev_agg_iter = session_agg_map_iter;
        session_agg_map_iter++;
        if (prev_agg_iter->second.session_map_.size() == 0) {
            it->second.session_agg_map_.erase(prev_agg_iter);
        }
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
            prev_agg_iter = session_agg_map_iter;
            ++session_agg_map_iter;
            if (prev_agg_iter->second.session_map_.size() == 0) {
                it->second.session_agg_map_.erase(prev_agg_iter);
            }
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
            SessionEndpointMap::iterator prev = it;
            ++it;
            ++session_ep_visited_;
            session_agg_iteration_key_.Reset();
            session_iteration_key_.Reset();
            if (prev->second.session_agg_map_.size() == 0) {
                session_endpoint_map_.erase(prev);
            }
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
    int32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
                        flow_export_rate();
    /* If session export is disabled, update stats and return */
    if (!cfg_rate) {
        flow_stats_manager_->session_export_disable_drops_++;
        return false;
    }
    const bool &deleted = session_map_iter->second.deleted;
    const bool &evicted = session_map_iter->second.evicted;
    SessionStatsParams *stats = params;
    if (evicted) {
        stats = &session_map_iter->second.evict_stats;
    } else if (deleted) {
        stats = &session_map_iter->second.del_stats;
    }
    stats->sampled = false;
    /* For session-sampling diff_bytes should consider the diff bytes for both
     * forward and reverse flow */
    uint64_t diff_bytes = stats->fwd_flow.diff_bytes +
                          stats->rev_flow.diff_bytes;
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
        stats->sampled = true;
        /* Normalize the diff_bytes and diff_packets reported using the
         * probability value */
        if (probability == 0) {
            stats->fwd_flow.diff_bytes = 0;
            stats->fwd_flow.diff_packets = 0;
            stats->rev_flow.diff_bytes = 0;
            stats->rev_flow.diff_packets = 0;
        } else {
            stats->fwd_flow.diff_bytes = stats->fwd_flow.diff_bytes/
                                                probability;
            stats->fwd_flow.diff_packets = stats->fwd_flow.diff_packets/
                                                  probability;
            stats->rev_flow.diff_bytes = stats->rev_flow.diff_bytes/
                                                probability;
            stats->rev_flow.diff_packets = stats->rev_flow.diff_packets/
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
                      const std::string &uuid,
                      bool *match) {
    SessionSloRuleStateMap::iterator  it;
    bool is_logged = false;
    it = session_rule_state_map_.find(uuid);
    if (it != session_rule_state_map_.end()) {
        *match = true;
        if (it->second.ref_count == 0) {
            is_logged = true;
        }
        it->second.ref_count++;
        if (it->second.ref_count == it->second.rate) {
            it->second.ref_count = 0;
        }
    }
    return is_logged;
}
