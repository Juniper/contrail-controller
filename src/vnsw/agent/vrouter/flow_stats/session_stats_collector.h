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

extern SandeshTraceBufferPtr SessionStatsTraceBuf;

#define SESSION_STATS_TRACE(obj, ...)\
do {\
    SessionStats##obj::TraceMsg(SessionStatsTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false)

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
    uint16_t server_port;
    uint16_t proto;
    SessionAggKey() { Reset(); }
    void Reset();
    bool IsLess(const SessionAggKey &rhs) const;
    bool IsEqual(const SessionAggKey &rhs) const;
};

struct SessionKey {
public:
    IpAddress remote_ip;
    uint16_t client_port;
    boost::uuids::uuid uuid;
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

struct SessionFlowExportInfo {
    std::string sg_rule_uuid;
    std::string nw_ace_uuid;
    std::string aps_rule_uuid;
    std::string action;
    std::string drop_reason;
    SessionFlowExportInfo() : sg_rule_uuid(""), nw_ace_uuid(""),
        aps_rule_uuid(""), action(""), drop_reason("") {
    }
};

struct SessionExportInfo {
    bool valid;
    std::string vm_cfg_name;
    std::string other_vrouter;
    uint16_t underlay_proto;
    UuidList vmi_slo_list;
    UuidList vn_slo_list;
    SessionFlowExportInfo fwd_flow;
    SessionFlowExportInfo rev_flow;
    SessionExportInfo() : valid(false), vm_cfg_name(""), other_vrouter(""),
       underlay_proto(0) {}
};

struct SessionFlowStatsParams {
    uint64_t diff_bytes;
    uint64_t diff_packets;
    uint16_t underlay_src_port;
    uint16_t tcp_flags;
    bool valid;
    SessionFlowStatsParams() : diff_bytes(0), diff_packets(0),
        underlay_src_port(0), tcp_flags(0), valid(false) {
    }
};

struct SessionStatsParams {
    bool sampled;
    SessionFlowStatsParams fwd_flow;
    SessionFlowStatsParams rev_flow;
    SessionStatsParams() : sampled(false), fwd_flow(), rev_flow() {}
};

struct SessionStatsInfo {
public:
    uint64_t setup_time;
    uint64_t teardown_time;
    bool exported_atleast_once;
    bool deleted;
    SessionStatsParams del_stats;
    bool evicted;
    SessionStatsParams evict_stats;
    SessionExportInfo export_info;
    SessionFlowStatsInfo fwd_flow;
    SessionFlowStatsInfo rev_flow;
    SessionStatsInfo() : setup_time(0), teardown_time(0),
        exported_atleast_once(false), deleted(false), del_stats(),
        evicted(false), evict_stats(), export_info(), fwd_flow(), rev_flow() {}
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
    #define CheckFlowLogging(logged) { \
        if (logged) { \
            return true; \
        } \
    }
    int ComputeSloRate(int rate, SecurityLoggingObject *slo) const;
    bool FetchFlowStats(SessionFlowStatsInfo *info,
                        SessionFlowStatsParams *params) const;
    uint64_t threshold() const;
    bool IsSamplingEnabled() const;
    bool SampleSession(SessionPreAggInfo::SessionMap::iterator session_map_iter,
                       SessionStatsParams *params) const;
    bool SessionStatsChangedLocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         SessionStatsParams *params) const;
    bool SessionStatsChangedUnlocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         SessionStatsParams *params) const;
    bool ProcessSessionEndpoint(const SessionEndpointMap::iterator &it);
    uint64_t GetUpdatedSessionFlowBytes(uint64_t info_bytes,
                                        uint64_t k_flow_bytes) const;
    uint64_t GetUpdatedSessionFlowPackets(uint64_t info_packets,
                                          uint64_t k_flow_pkts) const;
    void FillSessionEvictStats
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         SessionInfo *session_info, bool is_sampling, bool is_logging) const;
    void FillSessionFlowStats(const SessionFlowStatsParams &stats,
                              SessionFlowInfo *flow_info,
                              bool is_sampling,
                              bool is_logging) const;
    void FillSessionFlowInfo(const SessionFlowStatsInfo &session_flow,
                             const SessionStatsInfo &sinfo,
                             const SessionFlowExportInfo &einfo,
                             SessionFlowInfo *flow_info) const;
    void CopyFlowInfoInternal(SessionFlowExportInfo *info,
                              const boost::uuids::uuid &u,
                              FlowEntry *fe) const;
    void CopyFlowInfo(SessionStatsInfo &session,
                      const RevFlowDepParams *params);
    void UpdateAggregateStats(const SessionInfo &sinfo,
                              SessionAggInfo *agg_info,
                              bool is_sampling, bool is_logging) const;
    void FillSessionInfoLocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         const SessionStatsParams &stats, SessionInfo *session_info,
         SessionIpPort *session_key, bool is_sampling, bool is_logging) const;
    void FillSessionInfoUnlocked
        (SessionPreAggInfo::SessionMap::iterator session_map_iter,
         const SessionStatsParams &stats, SessionInfo *session_info,
         SessionIpPort *session_key,
         const RevFlowDepParams *params,
         bool read_flow, bool is_sampling, bool is_logging) const;
    void FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator it,
                            SessionIpPortProtocol *session_agg_key) const;
    void FillSessionEndpoint(SessionEndpointMap::iterator it,
                             SessionEndpoint *session_ep) const;
    void FillSessionTags(const TagList &list, SessionEndpoint *ep) const;
    void FillSessionRemoteTags(const TagList &list, SessionEndpoint *ep) const;
    static uint64_t GetCurrentTime();
    void UpdateSessionFlowStatsInfo(FlowEntry* fe,
                                    SessionFlowStatsInfo *session_flow) const;
    void UpdateSessionStatsInfo(FlowEntry* fe, uint64_t setup_time,
                                SessionStatsInfo *session) const;
    void AddSession(FlowEntry* fe, uint64_t setup_time);
    void DeleteSession(FlowEntry* fe, const boost::uuids::uuid &del_uuid,
                       uint64_t teardown_time,
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

    bool UpdateSloMatchRuleEntry(const boost::uuids::uuid &slo_uuid,
                                 const std::string &match_uuid,
                                 bool *match);
    bool CheckPolicyMatch(const SessionSloRuleMap &map,
                          const std::string &policy_uuid,
                          const bool &deleted_flag,
                          bool *match,
                          const bool  &exported_once);
    bool FindSloMatchRule(const SessionSloRuleMap &map,
                          const std::string &fw_policy_uuid,
                          const std::string &nw_policy_uuid,
                          const std::string &sg_policy_uuid,
                          const bool &deleted_flag,
                          bool *match,
                          const bool  &exported_once);

    void GetPolicyIdFromFlow(const FlowEntry *fe,
                             std::string &fw_policy_uuid,
                             std::string &nw_policy_uuid,
                             std::string &sg_policy_uuid);

    void GetPolicyIdFromDeletedFlow(const SessionFlowExportInfo& flow_info,
                                    std::string& fw_policy_uuid,
                                    std::string& nw_policy_uuid,
                                    std::string& sg_policy_uuid);

    bool MatchSloForFlow(const SessionStatsInfo &stats_info,
                         const FlowEntry *fe,
                         const std::string& fw_policy_uuid,
                         const std::string& nw_policy_uuid,
                         const std::string& sg_policy_uuid,
                         const bool &deleted_flag,
                         bool  *logged,
                         const bool  &exported_once);

    void BuildSloList(const SessionStatsInfo &stats_info,
                      const FlowEntry *fe,
                      SessionSloRuleMap *global_session_slo_rule_map,
                      SessionSloRuleMap *vmi_session_slo_rule_map,
                      SessionSloRuleMap *vn_session_slo_rule_map);
    void MakeSloList(const FlowEntry *fe,
                     SessionSloRuleMap *vmi_session_slo_rule_map,
                     SessionSloRuleMap *vn_session_slo_rule_map);

    bool FlowLogging(const SessionStatsInfo    &stats_info,
                     const FlowEntry *fe,
                     bool *logged,
                     const bool  &exported_once);

    bool DeletedFlowLogging(const SessionStatsInfo    &stats_info,
                            const SessionFlowExportInfo &flow_info,
                            bool  *logged,
                            const bool  &exported_once);

    bool HandleDeletedFlowLogging(const SessionStatsInfo &stats_info);
    bool HandleFlowLogging(const SessionStatsInfo &stats_info);
    bool CheckSessionLogging(const SessionStatsInfo &stats_info);
    void AddSloList(const UuidList &slo_list, SessionSloRuleMap *slo_rule_map);
    void AddSloEntry(const boost::uuids::uuid &uuid,
                     SessionSloRuleMap *slo_rule_map);
    void AddSloEntryRules(SecurityLoggingObject *slo,
                          SessionSloRuleMap *slo_rule_map);
    void AddSloFirewallPolicies(SecurityLoggingObject *slo,
                                SessionSloRuleMap *r_map);
    void AddSloFirewallRules(SecurityLoggingObject *slo,
                             SessionSloRuleMap *rule_map);
    void AddSloRules(
        const std::vector<autogen::SecurityLoggingObjectRuleEntryType> &list,
        SecurityLoggingObject *slo,
        SessionSloRuleMap *slo_rule_map);
    void AddSessionSloRuleEntry(const std::string &uuid, int rate,
                                SecurityLoggingObject *slo,
                                SessionSloRuleMap *slo_rule_map);
    void SloNotify(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateSloStateRules(SecurityLoggingObject *slo,
                             SessionSloState *state);
    bool CheckAndDeleteSessionStatsFlow(
        SessionPreAggInfo::SessionMap::iterator session_map_iter);

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
    FlowToSessionMap(SessionKey &session_key,
                     SessionAggKey &session_agg_key,
                     SessionEndpointKey &session_endpoint_key) :
        session_key_(session_key),
        session_agg_key_(session_agg_key),
        session_endpoint_key_(session_endpoint_key) {
    }
    bool IsEqual(FlowToSessionMap &rhs);
    SessionKey session_key() { return session_key_; }
    SessionAggKey session_agg_key() { return session_agg_key_; }
    SessionEndpointKey session_endpoint_key() { return session_endpoint_key_; }
private:
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
    bool UpdateSessionSloStateRuleRefCount(const std::string &uuid, bool *matc);
    SessionSloState() {}
    ~SessionSloState() {
        session_rule_state_map_.clear();
    }
private:
    SessionSloRuleStateMap session_rule_state_map_;
};

#endif //vnsw_agent_session_stats_collector_h
