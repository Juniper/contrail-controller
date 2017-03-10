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
                                               FlowStatsManager *aging_module,
                                               FlowStatsCollectorObject *obj)
    : FlowStatsCollector(io, intvl, flow_cache_timeout, uve,
                         instance_id, key, aging_module, obj),
      ingress_flow_log_list_(), dispatch_count_(0) {
}

FlowStatsCollectorTest::~FlowStatsCollectorTest() {
}

void FlowStatsCollectorTest::DispatchFlowMsg
    (const std::vector<FlowLogData> &msg_list) {
    std::vector<FlowLogData>::const_iterator it = msg_list.begin();
    while (it != msg_list.end()) {
        flow_log_ = *it;
        flow_log_valid_ = true;
        if (flow_log_.get_teardown_time()) {
            flow_del_log_ = flow_log_;
            flow_del_log_valid_ = true;
        }
        if (flow_log_.get_direction_ing()) {
            ingress_flow_log_list_.push_back(flow_log_);
        }
        dispatch_count_++;
        it++;
    }
}

void FlowStatsCollectorTest::ClearCount() {
    dispatch_count_ = 0;
}

FlowLogData FlowStatsCollectorTest::last_sent_flow_log() const {
    return flow_log_;
}

FlowLogData FlowStatsCollectorTest::last_sent_flow_del_log() const {
    return flow_del_log_;
}

void FlowStatsCollectorTest::ClearList() {
    if (ingress_flow_log_list_.size()) {
        ingress_flow_log_list_.clear();
    }
}

void FlowStatsCollectorTest::ResetLastSentLog() {
    flow_log_valid_ = false;
    flow_del_log_valid_ = false;
}
