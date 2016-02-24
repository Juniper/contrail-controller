/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __PKT_SANDESH_FLOW_H__
#define __PKT_SANDESH_FLOW_H__

#include "base/task.h"
#include "pkt/flow_table.h"
#include "pkt/pkt_types.h"
#include "vrouter/flow_stats/flow_stats_collector.h"

class PktSandeshFlow : public Task {
public:
    static const int kMaxFlowResponse = 100;
    static const char kDelimiter = '-';
    static const std::string start_key;
    PktSandeshFlow(Agent *agent, FlowRecordsResp *obj, std::string resp_ctx,
                   std::string key);
    virtual ~PktSandeshFlow();

    void SendResponse(SandeshResponse *resp);
    bool SetFlowKey(std::string key);
    static std::string GetFlowKey(const FlowKey &key, uint16_t partition_id);
    
    virtual bool Run();
    std::string Description() const { return "PktSandeshFlow"; }
    void SetSandeshFlowData(std::vector<SandeshFlowData> &list, FlowEntry *fe,
                            const FlowExportInfo *info);
    void set_delete_op(bool delete_op) {delete_op_ = delete_op;}

protected:
    FlowRecordsResp *resp_obj_;
    std::string resp_data_;
    FlowKey flow_iteration_key_;
    bool key_valid_;
    bool delete_op_;
    Agent *agent_;
    uint16_t partition_id_;

private:
    DISALLOW_COPY_AND_ASSIGN(PktSandeshFlow);
};

class PktSandeshFlowStats : public PktSandeshFlow {
public:
    PktSandeshFlowStats(Agent *agent, FlowStatsCollectorRecordsResp *obj, std::string resp_ctx,
                        std::string key);
    virtual ~PktSandeshFlowStats() {}
    bool SetProtoKey(std::string key);
    virtual bool Run();
    bool SetProto(std::string &key);
private:
    uint32_t proto_;
    uint32_t port_;
    FlowStatsCollectorRecordsResp *resp_;
    DISALLOW_COPY_AND_ASSIGN(PktSandeshFlowStats);
};
#endif
