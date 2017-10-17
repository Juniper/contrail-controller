/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

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
    if (dst_port != rhs.dst_port) {
        return dst_port < rhs.dst_port;
    }
    return proto < rhs.proto;
}

bool SessionAggKey::IsEqual(const SessionAggKey &rhs) const {
    if (local_ip != rhs.local_ip) {
        return false;
    }
    if (dst_port != rhs.dst_port) {
        return false;
    }
    if (proto != rhs.proto) {
        return false;
    }
    return true;
}

void SessionAggKey::Reset() {
    local_ip = IpAddress();
    dst_port = 0;
    proto = 0;
}

bool SessionKey::IsLess(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return remote_ip < rhs.remote_ip;
    }
    return src_port < rhs.src_port;
}

bool SessionKey::IsEqual(const SessionKey &rhs) const {
    if (remote_ip != rhs.remote_ip) {
        return false;
    }
    if (src_port != rhs.src_port) {
        return false;
    }
    return true;
}

void SessionKey::Reset() {
    remote_ip = IpAddress();
    src_port = 0;
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

void SessionStatsCollector::UpdateSessionFlowStatsInfo(FlowEntry *fe,
                                        SessionFlowStatsInfo *session_flow) {
    session_flow->flow = fe;
    session_flow->gen_id= fe->gen_id();
    session_flow->flow_handle = fe->flow_handle();
    session_flow->uuid = fe->uuid();
}

void SessionStatsCollector::UpdateSessionStatsInfo(FlowEntry* fe,
                                                   uint64_t setup_time,
                                                   SessionStatsInfo &session) {
    FlowEntry *rfe = fe->reverse_flow_entry();

    session.setup_time = setup_time;
    UpdateSessionFlowStatsInfo(fe, &session.fwd_flow);
    UpdateSessionFlowStatsInfo(rfe, &session.rev_flow);
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

    UpdateSessionStatsInfo(fe_fwd, setup_time, session);

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
                FillSessionInfoUnlocked(session_map_iter, &session_info,
                                        &session_key, NULL, false);
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
                                                 SessionFlowInfo *flow_info)
                                                 const {
    FlowEntry *fe = session_flow.flow.get();
    uint32_t flow_handle = session_flow.flow_handle;
    uint16_t gen_id = session_flow.gen_id;
    vr_flow_stats k_stats;
    KFlowData kinfo;
    uint64_t k_bytes, bytes, total_bytes, diff_bytes = 0;
    uint64_t k_packets, total_packets, diff_packets = 0;
    const vr_flow_entry *k_flow = NULL;
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
        ksync_flow_memory();

    k_flow = ksync_obj->GetKFlowStatsAndInfo(fe->key(),
                                             flow_handle,
                                             gen_id, &k_stats, &kinfo);
    if (!k_flow) {
        flow_info->set_logged_pkts(diff_packets);
        flow_info->set_logged_bytes(diff_bytes);
        flow_info->set_sampled_pkts(diff_packets);
        flow_info->set_sampled_bytes(diff_bytes);
        return;
    }

    flow_info->set_tcp_flags(kinfo.tcp_flags);
    flow_info->set_underlay_source_port(kinfo.underlay_src_port);

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
        flow_info->set_logged_pkts(diff_packets);
        flow_info->set_logged_bytes(diff_bytes);
        flow_info->set_sampled_pkts(diff_packets);
        flow_info->set_sampled_bytes(diff_bytes);
    }
}

void SessionStatsCollector::FillSessionFlowInfo(SessionFlowStatsInfo &session_flow,
                                                uint64_t setup_time,
                                                uint64_t teardown_time,
                                                const RevFlowDepParams *params,
                                                bool read_flow,
                                                SessionFlowInfo *flow_info)
                                                const {
    FlowEntry *fe = session_flow.flow.get();
    std::string action_str;

    FillSessionFlowStats(session_flow, flow_info);
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

void SessionStatsCollector::FillSessionInfoLocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionInfo *session_info,
     SessionIpPort *session_key) const {
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    FlowEntry *rfe = session_map_iter->second.rev_flow.flow.get();
    FLOW_LOCK(fe, rfe, FlowEvent::FLOW_MESSAGE);
    FillSessionInfoUnlocked(session_map_iter, session_info, session_key, NULL,
                            true);
}

void SessionStatsCollector::FillSessionInfoUnlocked
    (SessionPreAggInfo::SessionMap::iterator session_map_iter,
     SessionInfo *session_info,
     SessionIpPort *session_key,
     const RevFlowDepParams *params,
     bool read_flow) const {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *fe = session_map_iter->second.fwd_flow.flow.get();
    boost::system::error_code ec;
    /*
     * Fill the session Key
     */
    session_key->set_ip(session_map_iter->first.remote_ip);
    session_key->set_port(session_map_iter->first.src_port);
    FillSessionFlowInfo(session_map_iter->second.fwd_flow,
                        session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time,
                        NULL, read_flow, &session_info->forward_flow_info);
    FillSessionFlowInfo(session_map_iter->second.rev_flow,
                        session_map_iter->second.setup_time,
                        session_map_iter->second.teardown_time,
                        params, read_flow, &session_info->reverse_flow_info);
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
    session_agg_key->set_port(session_agg_map_iter->first.dst_port);
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

    SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter;
    SessionPreAggInfo::SessionMap::iterator session_map_iter;

    SessionInfo session_info;
    SessionIpPort session_key;
    SessionAggInfo session_agg_info;
    SessionIpPortProtocol session_agg_key;

    SessionEndpoint &session_ep = session_msg_list_[GetSessionMsgIdx()];

    FillSessionInfoUnlocked(session_it, &session_info, &session_key, params,
                            read_flow);
    session_agg_info.sessionMap.insert(make_pair(session_key, session_info));
    FillSessionAggInfo(agg_it, &session_agg_info, &session_agg_key,
                       session_info.get_forward_flow_info().get_logged_bytes(),
                       session_info.get_forward_flow_info().get_logged_pkts(),
                       session_info.get_reverse_flow_info().get_logged_bytes(),
                       session_info.get_reverse_flow_info().get_logged_pkts());
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
            FillSessionInfoLocked(session_map_iter, &session_info,
                                  &session_key);
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
            session_map_iter++;
            ++session_count;
            if (session_count ==
                agent_uve_->agent()->params()->max_sessions_per_aggregate()) {
                exit = true;
                break;
            }
        }
        FillSessionAggInfo(session_agg_map_iter, &session_agg_info, &session_agg_key,
                           total_fwd_bytes, total_fwd_packets, total_rev_bytes,
                           total_rev_packets);
        session_ep.sess_agg_info.insert(make_pair(session_agg_key, session_agg_info));
        if (exit) {
            break;
        }
        session_agg_map_iter++;
        ++session_agg_count;
        if (session_agg_count ==
            agent_uve_->agent()->params()->max_aggregates_per_session_endpoint()) {
            break;
        }
    }
    FillSessionEndpoint(it, &session_ep);
    EnqueueSessionMsg();

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
