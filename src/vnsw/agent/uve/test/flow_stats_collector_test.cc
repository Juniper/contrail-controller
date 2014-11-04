/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <uve/agent_uve.h>
//#include <uve/flow_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <algorithm>
#include <pkt/flow_proto.h>
#include <ksync/ksync_init.h>
#include <uve/test/flow_stats_collector_test.h>

FlowStatsCollectorTest::FlowStatsCollectorTest(boost::asio::io_service &io, int intvl,
                                           uint32_t flow_cache_timeout,
                                           AgentUveBase *uve) :
    FlowStatsCollector(io, intvl, flow_cache_timeout, uve) {
}

FlowStatsCollectorTest::~FlowStatsCollectorTest() { 
}

void FlowStatsCollectorTest::DispatchFlowMsg(SandeshLevel::type level,
                                             FlowDataIpv4 &flow) {
    flow_log_ = flow;
}

FlowDataIpv4 FlowStatsCollectorTest::last_sent_flow_log() const {
    return flow_log_;
}
