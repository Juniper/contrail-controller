/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_table_test_h
#define vnsw_agent_flow_table_test_h

#include <pkt/flow_table.h>

class FlowTableUnitTest : public FlowTable {
public:
    explicit FlowTableUnitTest(Agent *agent);
    virtual ~FlowTableUnitTest();
    void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    FlowDataIpv4 last_sent_flow_log() const;
private:
    FlowDataIpv4 flow_log_;
};

#endif  //  vnsw_agent_flow_table_test_h
