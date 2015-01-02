/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/test/flow_table_test.h>

FlowTableUnitTest::FlowTableUnitTest(Agent *agent)
    : FlowTable(agent) {
}

FlowTableUnitTest::~FlowTableUnitTest() {
}

void FlowTableUnitTest::DispatchFlowMsg(SandeshLevel::type level,
                                    FlowDataIpv4 &flow) {
    flow_log_ = flow;
}

FlowDataIpv4 FlowTableUnitTest::last_sent_flow_log() const {
    return flow_log_;
}

