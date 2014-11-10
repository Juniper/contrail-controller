/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_stats_collector_test_h
#define vnsw_agent_flow_stats_collector_test_h

#include <uve/agent_uve.h>
#include <uve/flow_stats_collector.h>

class FlowStatsCollectorTest : public FlowStatsCollector {
public:
    FlowStatsCollectorTest(boost::asio::io_service &io, int intvl,
                           uint32_t flow_cache_timeout,
                           AgentUveBase *uve);
    virtual ~FlowStatsCollectorTest();
    void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    FlowDataIpv4 last_sent_flow_log() const;
private:
    FlowDataIpv4 flow_log_;
};
#endif //vnsw_agent_flow_stats_collector_test_h
