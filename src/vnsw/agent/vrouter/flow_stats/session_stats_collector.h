/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_session_stats_collector_h
#define vnsw_agent_session_stats_collector_h

#include <boost/static_assert.hpp>
#include <pkt/flow_table.h>
#include <pkt/flow_mgmt_request.h>
#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <uve/stats_collector.h>
#include <uve/interface_uve_stats_table.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <sandesh/common/flow_types.h>
#include <vrouter/flow_stats/flow_export_request.h>
#include <vrouter/flow_stats/flow_export_info.h>
#include <vrouter/flow_stats/flow_stats_manager.h>

// Forward declaration
class AgentUtXmlFlowThreshold;
class AgentUtXmlFlowThresholdValidate;
class FlowStatsRecordsReq;
class FetchFlowStatsRecord;
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

    bool IsLess (const SessionEndpointKey &rhs) const;
};

struct SessionAggKey {
public:
    IpAddress local_ip;
    uint16_t dst_port;
    uint16_t proto;
    bool IsLess (const SessionAggKey &rhs) const;
};

struct SessionKey {
public:
    IpAddress remote_ip;
    uint16_t src_port;
    bool IsLess (const SessionKey &rhs) const;
};

struct SessionKeyCmp {
    bool operator() (const SessionKey &lhs,
                     const SessionKey &rhs) const {
        const SessionKey &lhs_base = static_cast<const SessionKey&>(lhs);
        return lhs_base.IsLess(rhs);
    }
};

struct SessionPreAggInfo {
public:
    typedef std::map<const SessionKey, FlowEntry*, SessionKeyCmp> SessionMap;
    SessionMap session_map_;
};

struct SessionAggKeyCmp {
    bool operator() (const SessionAggKey &lhs,
                     const SessionAggKey &rhs) const {
        const SessionAggKey &lhs_base = static_cast<const SessionAggKey&>(lhs);
        return lhs_base.IsLess(rhs);
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
        const SessionEndpointKey &lhs_base = static_cast<const SessionEndpointKey&>(lhs);
        return lhs_base.IsLess(rhs);
    }
};

class SessionStatsCollector : public StatsCollector {
public:
    static const uint32_t kSessionStatsTimerInterval = 100;
    static const uint8_t  kMaxSessionMsgsPerSend = 16;

    typedef std::map<const SessionEndpointKey, SessionEndpointInfo, SessionEndpointKeyCmp> SessionEndpointMap;
    typedef WorkQueue<boost::shared_ptr<SessionStatsReq> > Queue;
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

    void set_deleted(bool val) {
        deleted_ = val;
    }
    bool deleted() const {
        return deleted_;
    }
    int task_id() const { return task_id_; }
    uint32_t instance_id() const { return instance_id_; }
    const Queue *queue() const { return &request_queue_; }
    size_t Size() const { return session_endpoint_map_.size(); }
    friend class AgentUtXmlFlowThreshold;
    friend class AgentUtXmlFlowThresholdValidate;
    friend class FlowStatsRecordsReq;
    friend class FetchFlowStatsRecord;
    friend class FlowStatsManager;
    friend class SessionStatsCollectorObject;
protected:
    virtual void DispatchSessionMsg(const std::vector<SessionEndpoint> &lst);
private:
    static uint64_t GetCurrentTime();
    void AddSession(FlowEntry* fe);
    void DeleteSession(FlowEntry* fe);
    void GetSessionKey(FlowEntry* fe, SessionAggKey &session_agg_key,
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
    tbb::atomic<bool> deleted_;
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
    void MarkDelete();
    void ClearDelete();
    bool IsDeleted() const;
    bool CanDelete() const;
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

    SessionStatsReq(Event ev, const FlowEntryPtr &flow) :
        event_(ev), flow_(flow) {
    }
    ~SessionStatsReq() { }
    Event event() const { return event_; }
    FlowEntryPtr flow() const { return flow_; }

private:
    Event event_;
    FlowEntryPtr flow_;
};

#endif //vnsw_agent_session_stats_collector_h
