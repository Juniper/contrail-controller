/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_test_h
#define vnsw_agent_flow_stats_collector_test_h

#include <vector>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <sandesh/common/flow_types.h>

class FlowStatsCollectorTest : public FlowStatsCollector {
public:
    FlowStatsCollectorTest(boost::asio::io_service &io, int intvl,
                           uint32_t flow_cache_timeout,
                           AgentUveBase *uve,
                           uint32_t instance_id,
                           FlowAgingTableKey *key,
                           FlowStatsManager *aging_module);
    virtual ~FlowStatsCollectorTest();
    void DispatchFlowMsg(const std::vector<FlowLogData> &msg_list);
    FlowLogData last_sent_flow_log() const;
    std::vector<FlowLogData> ingress_flow_log_list() const {
        return ingress_flow_log_list_;
    }
    void ClearList();

private:
    FlowLogData flow_log_;
    std::vector<FlowLogData> ingress_flow_log_list_;
};

#endif  //  vnsw_agent_flow_stats_collector_test_h
