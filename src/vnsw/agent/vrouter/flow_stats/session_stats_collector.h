/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_session_stats_collector_h
#define vnsw_agent_session_stats_collector_h

#include <vrouter/flow_stats/flow_stats_manager.h>
// Forward declaration
class FlowStatsManager;
class SessionStatsReq;
class FlowToSessionMap;
struct SessionSloRuleEntry;
class SessionSloState;

struct SessionEndpointKey {
public:
    std::string vmi_cfg_name;
    std::string local_vn;
    std::string remote_vn;
    TagList local_tagset;
    TagList remote_tagset;
    std::string remote_prefix;
    std::string match_policy;
    bool is_client_session;
    bool is_si;
    SessionEndpointKey() { Reset(); }

    void Reset();
    bool IsLess(const SessionEndpointKey &rhs) const;
    bool IsEqual(const SessionEndpointKey &rhs) const;
};

struct SessionAggKey {
public:
    IpAddress local_ip;
    uint16_t dst_port;
    uint16_t proto;
    SessionAggKey() { Reset(); }
    void Reset();
    bool IsLess(const SessionAggKey &rhs) const;
    bool IsEqual(const SessionAggKey &rhs) const;
};

struct SessionKey {
public:
    IpAddress remote_ip;
    uint16_t src_port;
    SessionKey() { Reset(); }

    void Reset();
    bool IsLess(const SessionKey &rhs) const;
    bool IsEqual(const SessionKey &rhs) const;
};

struct SessionKeyCmp {
    bool operator()(const SessionKey &lhs, const SessionKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};

struct SessionFlowStatsInfo {
public:
    FlowEntryPtr flow;
    uint8_t gen_id;
    uint32_t flow_handle;
    boost::uuids::uuid uuid;
    uint64_t total_bytes;
    uint64_t total_packets;
};

struct SessionStatsInfo {
public:
    uint64_t setup_time;
    uint64_t teardown_time;
    SessionFlowStatsInfo fwd_flow;
    SessionFlowStatsInfo rev_flow;
};

struct SessionPreAggInfo {
public:
    typedef std::map<const SessionKey, SessionStatsInfo,
                     SessionKeyCmp> SessionMap;
    SessionMap session_map_;
};

struct SessionAggKeyCmp {
    bool operator()(const SessionAggKey &lhs, const SessionAggKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};

struct SessionEndpointInfo {
public:
    typedef std::map<const SessionAggKey, SessionPreAggInfo,
                     SessionAggKeyCmp> SessionAggMap;
    SessionAggMap session_agg_map_;
};

struct SessionEndpointKeyCmp {
    bool operator() (const SessionEndpointKey &lhs,
                     const SessionEndpointKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};

class SessionStatsCollector : public StatsCollector {
public:
    typedef std::map<const SessionEndpointKey, SessionEndpointInfo,
                     SessionEndpointKeyCmp> SessionEndpointMap;
    typedef WorkQueue<boost::shared_ptr<SessionStatsReq> > Queue;
    typedef std::map<FlowEntryPtr, FlowToSessionMap> FlowSessionMap;
    typedef std::map<std::string, SessionSloRuleEntry> SessionSloRuleMap;

    static const uint32_t kSessionStatsTimerInterval = 1000;
    static const uint32_t kSessionsPerTask = 256;

    uint32_t RunSessionEndpointStats(uint32_t max_count);

    class SessionTask : public Task {
    public:
        SessionTask(SessionStatsCollector *ssc);
        virtual ~SessionTask();
        std::string Description() const;
        bool Run();
    private:
        SessionStatsCollector *ssc_;
    };


    SessionStatsCollector(boost::asio::io_service &io, AgentUveBase *uve,
                          uint32_t instance_id, FlowStatsManager *fsm,
                          SessionStatsCollectorObject *obj);
    virtual ~SessionStatsCollector();
    bool Run();

    int task_id() const { return task_id_; }
    uint32_t instance_id() const { return instance_id_; }
    const Queue *queue() const { return &request_queue_; }
    size_t Size() const { return session_endpoint_map_.size(); }
    friend class FlowStatsManager;
    friend class SessionStatsCollectorObject;
protected:
    virtual void DispatchSessionMsg(const std::vector<SessionEndpoint> &lst);
private:
    bool ProcessSessionEndpoint(const SessionEndpointMap::iterator &it);
    void ProcessSessionDelete
        (const SessionEndpointMap::iterator &ep_it,
         const SessionEndpointInfo::SessionAggMap::iterator &agg_it,
         const SessionPreAggInfo::SessionMap::iterator &session_it,
         const RevFlowDepParams *params, bool read_flow);
    uint64_t GetUpdatedSessionFlowBytes(uint64_t info_bytes,
                                        uint64_t k_flow_bytes) const;
    uint64_t GetUpdatedSessionFlowPackets(uint64_t info_packets,
                                          uint64_t k_flow_pkts) const;
    void FillSessionFlowStats(SessionFlowStatsInfo &session_flow,
                              SessionFlowInfo *flow_info) const;
    void FillSessionFlowInfo(SessionFlowStatsInfo &session_flow,
                             uint64_t setup_time,
                             uint64_t teardown_time,
                             const RevFlowDepParams *params,
                             bool read_flow,
                             SessionFlowInfo *flow_info) const;
    void FillSessionInfoLocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         SessionInfo *session_info,
         SessionIpPort *session_key) const;
    void FillSessionInfoUnlocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         SessionInfo *session_info,
         SessionIpPort *session_key,
         const RevFlowDepParams *params,
         bool read_flow) const;
    void FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter,
                            SessionAggInfo *session_agg_info,
                            SessionIpPortProtocol *session_agg_key,
                            uint64_t total_fwd_bytes,
                            uint64_t total_fwd_packets,
                            uint64_t total_rev_bytes,
                            uint64_t total_rev_packets) const;
    void FillSessionEndpoint(SessionEndpointMap::iterator it,
                             SessionEndpoint *session_ep) const;
    void FillSessionTagInfo(const TagList &list,
                            SessionEndpoint *session_ep,
                            bool is_remote) const;
    static uint64_t GetCurrentTime();
    void UpdateSessionFlowStatsInfo(FlowEntry* fe,
                                    SessionFlowStatsInfo *session_flow);
    void UpdateSessionStatsInfo(FlowEntry* fe,
                                uint64_t setup_time,
                                SessionStatsInfo &session);
    void AddSession(FlowEntry* fe, uint64_t setup_time);
    void DeleteSession(FlowEntry* fe, uint64_t teardown_time,
                       const RevFlowDepParams *params);
    void EvictedSessionStatsUpdate(const FlowEntryPtr &flow,
                                   uint32_t bytes,
                                   uint32_t packets,
                                   uint32_t oflow_bytes,
                                   const boost::uuids::uuid &u);
    bool GetSessionKey(FlowEntry* fe, SessionAggKey &session_agg_key,
                       SessionKey    &session_key,
                       SessionEndpointKey &session_endpoint_key);
    void AddFlowToSessionMap(FlowEntry *fe,
                             SessionKey session_key,
                             SessionAggKey session_agg_key,
                             SessionEndpointKey session_endpoint_key);
    void DeleteFlowToSessionMap(FlowEntry *fe);
    void Shutdown();
    void RegisterDBClients();
    void AddEvent(const FlowEntryPtr &flow);
    void DeleteEvent(const FlowEntryPtr &flow, const RevFlowDepParams &params);
    void UpdateSessionStatsEvent(const FlowEntryPtr &flow,
                                 uint32_t bytes,
                                 uint32_t packets,
                                 uint32_t oflow_bytes,
                                 const boost::uuids::uuid &u);
    bool RequestHandlerEntry();
    void RequestHandlerExit(bool done);
    bool RequestHandler(boost::shared_ptr<SessionStatsReq> req);
    void EnqueueSessionMsg();
    void DispatchPendingSessionMsg();
    uint8_t GetSessionMsgIdx();

    void UpdateSloMatchRuleEntry(boost::uuids::uuid slo_uuid,
                                 std::string match_uuid,
                                 bool *is_logged);
    bool FindSloMatchRule(SessionSloRuleMap &map,
                          std::string match_uuid);
    bool MatchSloForSession(SessionFlowStatsInfo &session_flow,
                            std::string match_uuid);
    void BuildSloList(SessionFlowStatsInfo &session_flow,
                      SessionSloRuleMap *global_session_slo_rule_map,
                      SessionSloRuleMap *vmi_session_slo_rule_map,
                      SessionSloRuleMap *vn_session_slo_rule_map);
    void AddSloList(const UuidList &slo_list, SessionSloRuleMap *slo_rule_map);
    void AddSloEntry(const boost::uuids::uuid &uuid,
                     SessionSloRuleMap *slo_rule_map);
    void AddSloEntryRules(SecurityLoggingObject *slo,
                          SessionSloRuleMap *slo_rule_map);
    void AddSloFirewallPolicies(UuidList &list, int rate,
                                SecurityLoggingObject *slo,
                                SessionSloRuleMap *slo_rule_map);
    void AddSloFirewallRules(UuidList &list, int rate,
                             SecurityLoggingObject *slo,
                             SessionSloRuleMap *slo_rule_map);
    void AddSloRules(
        const std::vector<autogen::SecurityLoggingObjectRuleEntryType> &list,
        SecurityLoggingObject *slo,
        SessionSloRuleMap *slo_rule_map);
    void AddSessionSloRuleEntry(std::string uuid, int rate,
                                SecurityLoggingObject *slo,
                                SessionSloRuleMap *slo_rule_map);
    void SloNotify(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateSloStateRules(SecurityLoggingObject *slo,
                             SessionSloState *state);

    AgentUveBase *agent_uve_;
    int task_id_;
    SessionEndpointKey session_ep_iteration_key_;
    SessionAggKey session_agg_iteration_key_;
    SessionKey session_iteration_key_;
    SessionEndpointMap session_endpoint_map_;
    FlowSessionMap flow_session_map_;
    Queue request_queue_;
    std::vector<SessionEndpoint> session_msg_list_;
    uint8_t session_msg_index_;
    uint32_t instance_id_;
    FlowStatsManager *flow_stats_manager_;
    SessionStatsCollectorObject *parent_;
    SessionTask *session_task_;
    // Cached UTC Time stamp
    // The timestamp is taken once on SessionStatsCollector::RequestHandlerEntry()
    // and used for all requests in current run
    uint64_t current_time_;
    uint64_t session_task_starts_;
    uint32_t session_ep_visited_;
    DBTable::ListenerId slo_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(SessionStatsCollector);
};

class SessionStatsCollectorObject {
public:
    static const int kMaxSessionCollectors = 1;
    typedef boost::shared_ptr<SessionStatsCollector> SessionStatsCollectorPtr;
    SessionStatsCollectorObject(Agent *agent, FlowStatsManager *mgr);
    SessionStatsCollector* GetCollector(uint8_t idx) const;
    void SetExpiryTime(int time);
    int GetExpiryTime() const;
    SessionStatsCollector* FlowToCollector(const FlowEntry *flow);
    void Shutdown();
    size_t Size() const;
    void RegisterDBClients();
private:
    SessionStatsCollectorPtr collectors[kMaxSessionCollectors];
    DISALLOW_COPY_AND_ASSIGN(SessionStatsCollectorObject);
};

class SessionStatsReq {
public:
    enum Event {
        INVALID,
        ADD_SESSION,
        DELETE_SESSION,
        UPDATE_SESSION_STATS,
    };

    SessionStatsReq(Event ev, const FlowEntryPtr &flow, uint64_t time):
        event_(ev), flow_(flow), time_(time) {
    }
    SessionStatsReq(Event ev, const FlowEntryPtr &flow, uint64_t time,
                    const RevFlowDepParams &p) :
        event_(ev), flow_(flow), time_(time), params_(p) {
    }
    SessionStatsReq(Event event, const FlowEntryPtr &flow, uint32_t bytes,
                  uint32_t packets, uint32_t oflow_bytes,
                  const boost::uuids::uuid &u) :
                  event_(event), flow_(flow), bytes_(bytes), packets_(packets),
                  oflow_bytes_(oflow_bytes), uuid_(u) {
    }

    ~SessionStatsReq() { }

    Event event() const { return event_; }
    FlowEntry* flow() const { return flow_.get(); }
    FlowEntry* reverse_flow() const;
    uint64_t time() const { return time_; }
    const RevFlowDepParams& params() const { return params_; }
    uint32_t bytes() const { return bytes_;}
    uint32_t packets() const { return packets_;}
    uint32_t oflow_bytes() const { return oflow_bytes_;}
    boost::uuids::uuid uuid() const { return uuid_; }

private:
    Event event_;
    FlowEntryPtr flow_;
    uint64_t time_;
    RevFlowDepParams params_;
    uint32_t bytes_;
    uint32_t packets_;
    uint32_t oflow_bytes_;
    boost::uuids::uuid uuid_;
    DISALLOW_COPY_AND_ASSIGN(SessionStatsReq);
};

class FlowToSessionMap {
public:
    FlowToSessionMap(const boost::uuids::uuid &uuid,
                     SessionKey &session_key,
                     SessionAggKey &session_agg_key,
                     SessionEndpointKey &session_endpoint_key) :
        uuid_(uuid),
        session_key_(session_key),
        session_agg_key_(session_agg_key),
        session_endpoint_key_(session_endpoint_key) {
        }
    bool IsEqual(FlowToSessionMap &rhs);
    boost::uuids::uuid uuid() { return uuid_; }
    SessionKey session_key() { return session_key_; }
    SessionAggKey session_agg_key() { return session_agg_key_; }
    SessionEndpointKey session_endpoint_key() { return session_endpoint_key_; }
private:
    boost::uuids::uuid uuid_;
    SessionKey session_key_;
    SessionAggKey session_agg_key_;
    SessionEndpointKey session_endpoint_key_;
};

struct SessionSloRuleEntry {
public:
    SessionSloRuleEntry(int rate, const boost::uuids::uuid &uuid):
        rate(rate), slo_uuid(uuid) {}

    int rate;
    boost::uuids::uuid slo_uuid;
};

struct SessionSloRuleState {
public:
    int rate;
    int ref_count;
};

class SessionSloState : public DBState {
public:
    typedef std::map<std::string, SessionSloRuleState> SessionSloRuleStateMap;
    void DeleteSessionSloStateRuleEntry(std::string uuid);
    void UpdateSessionSloStateRuleEntry(std::string uuid, int rate);
    void UpdateSessionSloStateRuleRefCount(std::string uuid,
                                           bool *is_logged);
    SessionSloState() {}
    ~SessionSloState() {
        session_rule_state_map_.clear();
    }
private:
    SessionSloRuleStateMap session_rule_state_map_;
};

#endif //vnsw_agent_session_stats_collector_h
