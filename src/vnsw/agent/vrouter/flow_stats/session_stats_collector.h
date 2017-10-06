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

struct SessionEndpointKey {
public:
    const VmInterface *vmi;
    std::string local_vn;
    std::string remote_vn;
    TagList local_tagset;
    TagList remote_tagset;
    std::string remote_prefix;
    std::string match_policy;
    bool is_client_session;
    bool is_si;

    bool IsLess(const SessionEndpointKey &rhs) const;
    bool IsEqual(const SessionEndpointKey &rhs) const;
};

struct SessionAggKey {
public:
    IpAddress local_ip;
    uint16_t dst_port;
    uint16_t proto;
    bool IsLess(const SessionAggKey &rhs) const;
    bool IsEqual(const SessionAggKey &rhs) const;
};

struct SessionKey {
public:
    IpAddress remote_ip;
    uint16_t src_port;
    bool IsLess(const SessionKey &rhs) const;
    bool IsEqual(const SessionKey &rhs) const;
};

struct SessionKeyCmp {
    bool operator() (const SessionKey &lhs,
                     const SessionKey &rhs) const {
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
    typedef std::map<const SessionKey, SessionStatsInfo, SessionKeyCmp> SessionMap;
    SessionMap session_map_;
};

struct SessionAggKeyCmp {
    bool operator() (const SessionAggKey &lhs,
                     const SessionAggKey &rhs) const {
        return lhs.IsLess(rhs);
    }
};

struct SessionEndpointInfo {
public:
    typedef std::map<const SessionAggKey, SessionPreAggInfo, SessionAggKeyCmp> SessionAggMap;
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

    static const uint8_t  kMaxSessionMsgsPerSend = 16;
    static const uint32_t kSessionStatsTimerInterval = 100;
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


    SessionStatsCollector(boost::asio::io_service &io,
                       AgentUveBase *uve, uint32_t instance_id,
                       FlowStatsManager *aging_module,
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
    uint32_t ProcessSessionEndpoint(SessionEndpointMap::iterator &it,
                                     const RevFlowDepParams *params,
                                     bool from_config,
                                     bool read_flow);
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
         SessionIpPort *session_key,
         const RevFlowDepParams *params,
         bool read_flow) const;
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

    AgentUveBase *agent_uve_;
    int task_id_;
    SessionEndpointKey session_ep_iteration_key_;
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
    DISALLOW_COPY_AND_ASSIGN(SessionStatsCollector);
};

class SessionStatsCollectorObject {
public:
    static const int kMaxSessionCollectors = 1;
    typedef boost::shared_ptr<SessionStatsCollector> SessionStatsCollectorPtr;
    SessionStatsCollectorObject(Agent *agent,
                             FlowStatsManager *mgr);
    SessionStatsCollector* GetCollector(uint8_t idx) const;
    void SetExpiryTime(int time);
    int GetExpiryTime() const;
    SessionStatsCollector* FlowToCollector(const FlowEntry *flow);
    void Shutdown();
    size_t Size() const;
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
#endif //vnsw_agent_session_stats_collector_h
