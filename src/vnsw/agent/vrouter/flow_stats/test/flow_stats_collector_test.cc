/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vrouter/flow_stats/test/flow_stats_collector_test.h>

FlowStatsCollectorTest::FlowStatsCollectorTest(boost::asio::io_service &io,
                                               int intvl,
                                               uint32_t flow_cache_timeout,
                                               AgentUveBase *uve,
                                               uint32_t instance_id,
                                               FlowAgingTableKey *key,
                                               FlowStatsManager *aging_module)
    : FlowStatsCollector(io, intvl, flow_cache_timeout, uve,
                         instance_id, key, aging_module) {
}

FlowStatsCollectorTest::~FlowStatsCollectorTest() {
}

void FlowStatsCollectorTest::DispatchFlowMsg(SandeshLevel::type level,
                                        FlowDataIpv4 &flow) {
    flow_log_ = flow;
    if (flow.get_direction_ing()) {
        ingress_flow_log_list_.push_back(flow);
    }
}

FlowDataIpv4 FlowStatsCollectorTest::last_sent_flow_log() const {
    return flow_log_;
}

void FlowStatsCollectorTest::ClearList() {
    if (ingress_flow_log_list_.size()) {
        ingress_flow_log_list_.clear();
    }
}

