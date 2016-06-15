/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_OPER_PROFILE_H_
#define SRC_VNSW_AGENT_OPER_PROFILE_H_
#include "db/db.h"
class Agent;
class Timer;

class ProfileData {
public:
    ProfileData();
    ~ProfileData(){}
    struct WorkQueueStats {
        std::string name_;
        uint64_t queue_count_;
        uint64_t enqueue_count_;
        uint64_t dequeue_count_;
        uint64_t max_queue_count_;
        uint64_t start_count_;
        uint64_t busy_time_;
        void Reset();
        void Get();
    };

    struct FlowTokenStats {
        uint32_t add_tokens_;
        uint64_t add_failures_;
        uint64_t add_restarts_;
        uint32_t ksync_tokens_;
        uint64_t ksync_failures_;
        uint64_t ksync_restarts_;
        uint32_t update_tokens_;
        uint64_t update_failures_;
        uint64_t update_restarts_;
        uint32_t del_tokens_;
        uint64_t del_failures_;
        uint64_t del_restarts_;
        void Reset();
    };

    struct DBTableStats {
        uint64_t db_entry_count_;
        uint64_t walker_count_;
        uint64_t enqueue_count_;
        uint64_t input_count_;
        uint64_t notify_count_;
        void Get(const DBTable *table);
        void Accumulate(const DBTableBase *table);
        void Reset();
    };

    struct FlowStats {
        uint64_t flow_count_;
        uint64_t add_count_;
        uint64_t del_count_;
        uint64_t audit_count_;
        uint64_t reval_count_;
        uint64_t recompute_count_;
        uint64_t vrouter_responses_;
        uint64_t vrouter_error_;
        uint64_t evict_count_;
        FlowTokenStats token_stats_;
        WorkQueueStats pkt_handler_queue_;
        WorkQueueStats flow_mgmt_queue_;
        WorkQueueStats flow_update_queue_;
        std::vector<WorkQueueStats> flow_event_queue_;
        std::vector<WorkQueueStats> flow_tokenless_queue_;
        std::vector<WorkQueueStats> flow_delete_queue_;
        std::vector<WorkQueueStats> flow_ksync_queue_;
        std::vector<WorkQueueStats> flow_stats_queue_;
        void Get();
        void Reset();
    };

    struct PktStats {
        uint64_t arp_count_;
        uint64_t dhcp_count_;
        uint64_t dns_count_;
        uint64_t icmp_count_;
        void Get();
        void Reset();
    };

    struct XmppStats {
        uint64_t inet4_add_count_;
        uint64_t inet4_del_count_;
        uint64_t inet6_add_count_;
        uint64_t inet6_del_count_;
        uint64_t mcast_add_count_;
        uint64_t mcast_del_count_;
        uint64_t bridge_add_count_;
        uint64_t bridge_del_count_;
        void Get();
        void Reset();
    };

    struct NovaIpcStats {
        uint64_t add_count_;
        uint64_t del_count_;
        void Get();
    };

    void Get(Agent *agent);
public:
    std::string  time_;
    FlowStats    flow_;
    PktStats     pkt_;
    DBTableStats inet4_routes_;
    DBTableStats inet6_routes_;
    DBTableStats bridge_routes_;
    DBTableStats multicast_routes_;
    DBTableStats evpn_routes_;
    XmppStats    rx_stats_;
    XmppStats    tx_stats_;
    WorkQueueStats ksync_tx_queue_count_;
    WorkQueueStats ksync_rx_queue_count_;
    TaskStats   task_stats_[24];
    std::map<std::string, DBTableStats > profile_stats_table_;
};

class AgentProfile {
public:
    static const uint32_t kProfileTimeout = 1000;
    static const uint16_t kSecondsHistoryCount = 300;
    static const uint16_t kMinutesHistoryCount = 60;
    static const uint16_t kHoursHistoryCount = 24;
    typedef boost::function<void(ProfileData *data)> PktFlowStatsCb;
    typedef boost::function<void(ProfileData *data)> KSyncStatsCb;
    typedef boost::function<void(ProfileData *data)> ProfileCb;

    AgentProfile(Agent *agent, bool enable);
    ~AgentProfile();
    bool Init();
    void Shutdown();
    void InitDone();

    bool TimerRun();
    void Log();

    void RegisterPktFlowStatsCb(ProfileCb cb) { pkt_flow_stats_cb_ = cb; }
    void RegisterKSyncStatsCb(ProfileCb cb) { ksync_stats_cb_ = cb; }
    void RegisterFlowStatsCb(ProfileCb cb) { flow_stats_cb_ = cb; }
    void AddProfileData(ProfileData *data);
    ProfileData *GetProfileData(uint16_t index);
    uint16_t seconds_history_index() const { return seconds_history_index_; }
 private:
    ProfileData *GetLastProfileData();

    Agent *agent_;
    Timer *timer_;
    time_t start_time_;
    bool enable_;

    ProfileData one_min_data_;
    ProfileData five_min_data_;
    ProfileData fifteen_min_data_;
    ProfileData thirty_min_data_;
    ProfileData one_hr_data_;
    ProfileData four_hr_data_;
    ProfileData eight_hr_data_;
    ProfileData sixteen_hr_data_;
    ProfileData twentyfour_hr_data_;

    uint16_t seconds_history_index_;
    ProfileData seconds_history_data_[kSecondsHistoryCount];
    uint16_t minutes_history_index_;
    ProfileData minutes_history_data_[kMinutesHistoryCount];
    uint16_t hours_history_index_;
    ProfileData hours_history_data_[kHoursHistoryCount];

    ProfileCb pkt_flow_stats_cb_;
    ProfileCb ksync_stats_cb_;
    ProfileCb flow_stats_cb_;
    DISALLOW_COPY_AND_ASSIGN(AgentProfile);
};

#endif  // SRC_VNSW_AGENT_OPER_PROFILE_H_
