/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_session_stats_collector_h
#define vnsw_agent_session_stats_collector_h

#include <vrouter/flow_stats/flow_stats_manager.h>
// Forward declaration
class FlowStatsManager;
class SessionStatsReq;

struct SessionEndpointKey {
public:
    const VmInterface *vmi;
    std::string local_vn;
    std::string remote_vn;
    TagList local_tagset;
    TagList remote_tagset;
    std::string remote_prefix;
    bool is_client_session;
    bool is_si;

    bool IsLess(const SessionEndpointKey &rhs) const;
};

struct SessionAggKey {
public:
    IpAddress local_ip;
    uint16_t dst_port;
    uint16_t proto;
    bool IsLess(const SessionAggKey &rhs) const;
};

struct SessionKey {
public:
    IpAddress remote_ip;
    uint16_t src_port;
    bool IsLess(const SessionKey &rhs) const;
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
    uint64_t setup_time;
    uint64_t teardown_time;
};
struct SessionPreAggInfo {
public:
    typedef std::map<const SessionKey, SessionFlowStatsInfo, SessionKeyCmp> SessionMap;
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
    typedef std::map<const SessionEndpointKey, SessionEndpointInfo, SessionEndpointKeyCmp> SessionEndpointMap;
    typedef WorkQueue<boost::shared_ptr<SessionStatsReq> > Queue;

    static const uint32_t kSessionStatsTimerInterval = 100;
    static const uint8_t  kMaxSessionMsgsPerSend = 16;

    uint32_t RunSessionStatsCollect();
    uint32_t ProcessSessionEndpoint(SessionEndpointMap::iterator &it,
                                     KSyncFlowMemory *ksync_obj,
                                     uint64_t curr_time);
    void FillSessionFlowInfo(FlowEntry *fe,
                             uint64_t setup_time,
                             uint64_t teardown_time,
                             KSyncFlowMemory *ksync_obj,
                             SessionFlowInfo &flow_info);
    void FillSessionInfo(SessionPreAggInfo::SessionMap::iterator session_map_iter,
                         KSyncFlowMemory *ksync_obj,
                         SessionInfo &session_info, SessionIpPort &session_key);
    void FillSessionAggInfo(SessionEndpointInfo::SessionAggMap::iterator session_agg_map_iter,
                            SessionAggInfo &session_agg_info,
                            SessionIpPortProtocol &session_agg_key);
    void FillSessionEndpoint(SessionEndpointMap::iterator it,
                             SessionEndpoint &session_ep);
    void FillSessionTagInfo(const TagList &list,
                            SessionEndpoint &session_ep,
                            bool is_remote);

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
    static uint64_t GetCurrentTime();
    void AddSession(FlowEntry* fe, uint64_t setup_time);
    void DeleteSession(FlowEntry* fe, uint64_t teardown_time);
    bool GetSessionKey(FlowEntry* fe, SessionAggKey &session_agg_key,
                       SessionKey    &session_key,
                       SessionEndpointKey &session_endpoint_key);
    void Shutdown();
    void AddEvent(const FlowEntryPtr &flow);
    void DeleteEvent(const FlowEntryPtr &flow);
    bool RequestHandlerEntry();
    void RequestHandlerExit(bool done);
    bool RequestHandler(boost::shared_ptr<SessionStatsReq> req);
    void EnqueueSessionMsg();
    void DispatchPendingSessionMsg();
    uint8_t GetSessionMsgIdx();

    AgentUveBase *agent_uve_;
    int task_id_;
    SessionEndpointMap session_endpoint_map_;
    Queue request_queue_;
    std::vector<SessionEndpoint> session_msg_list_;
    uint8_t session_msg_index_;
    uint32_t instance_id_;
    FlowStatsManager *flow_stats_manager_;
    SessionStatsCollectorObject *parent_;
    SessionTask *session_task_;
    // Number of timer fires needed to scan the flow-table once
    // This is based on ageing timer
    uint32_t timers_per_scan_;
    // Cached UTC Time stamp
    // The timestamp is taken once on SessionStatsCollector::RequestHandlerEntry()
    // and used for all requests in current run
    uint64_t current_time_;
    uint64_t session_task_starts_;
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
    };

    SessionStatsReq(Event ev, const FlowEntryPtr &flow, uint64_t time) :
        event_(ev), flow_(flow), time_(time){
    }
    ~SessionStatsReq() { }
    Event event() const { return event_; }
    FlowEntryPtr flow() const { return flow_; }
    uint64_t time() const { return time_; }

private:
    Event event_;
    FlowEntryPtr flow_;
    uint64_t time_;
    DISALLOW_COPY_AND_ASSIGN(SessionStatsReq);
};

#endif //vnsw_agent_session_stats_collector_h
