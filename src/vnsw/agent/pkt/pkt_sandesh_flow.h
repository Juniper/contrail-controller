/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __PKT_SANDESH_FLOW_H__
#define __PKT_SANDESH_FLOW_H__

#include "base/task.h"
#include "pkt/flowtable.h"
#include "pkt/pkt_types.h"

class PktSandeshFlow : public Task {
public:
    static const int max_flow_response = 100;
    static const std::string start_key;
    PktSandeshFlow(FlowRecordsResp *obj, std::string resp_ctx, std::string key) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::PktFlowResponder")),
              0), resp_obj_(obj), resp_data_(resp_ctx), 
        flow_iteration_key_(), key_valid_(false) {
        if (key != Agent::GetInstance()->NullString()) {
            if (SetFlowKey(key)) {
                key_valid_ = true;
            }
        }
    }
    void SendResponse(SandeshResponse *resp);
    bool SetFlowKey(std::string key);
    static std::string GetFlowKey(const FlowKey &key);
    
    virtual bool Run();
    void SetSandeshFlowData(std::vector<SandeshFlowData> &list, FlowEntry *fe);
protected:
    FlowRecordsResp *resp_obj_;
    std::string resp_data_;
    FlowKey flow_iteration_key_;
    bool key_valid_;
};

#endif
