/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_table_test_h
#define vnsw_agent_flow_table_test_h

#include <vector>
#include <pkt/flow_table.h>

class FlowTableUnitTest : public FlowTable {
public:
    explicit FlowTableUnitTest(Agent *agent);
    virtual ~FlowTableUnitTest();
    void DispatchFlowMsg(SandeshLevel::type level, FlowDataIpv4 &flow);
    FlowDataIpv4 last_sent_flow_log() const;
    std::vector<FlowDataIpv4> ingress_flow_log_list() const {
        return ingress_flow_log_list_;
    }
    void ClearList();

private:
    FlowDataIpv4 flow_log_;
    std::vector<FlowDataIpv4> ingress_flow_log_list_;
};

#endif  //  vnsw_agent_flow_table_test_h
