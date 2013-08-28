/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_kstate_h
#define vnsw_agent_flow_kstate_h

#include "base/task.h"
#include "pkt/flowtable.h"
#include <vr_flow.h>
#include <vr_mirror.h>

class FlowKState : public Task {
 public:
    FlowKState(KFlowResp *obj, std::string resp_ctx, int idx) :
        Task((TaskScheduler::GetInstance()->GetTaskId("Agent::FlowResponder")),
              0), resp_obj_(obj), resp_data_(resp_ctx), 
        flow_iteration_key_(0), flow_idx_(idx) {}
    virtual void SendResponse();
    virtual void SendPartialResponse();
    
    virtual bool Run();
    void SetFlowData(std::vector<KFlowInfo> &list, const vr_flow_entry *k_flow, int index);
protected:
    KFlowResp *resp_obj_;
    std::string resp_data_;
    uint32_t flow_iteration_key_;
    int flow_idx_;
private:
    void UpdateFlagStr(std::string &str, bool &set, unsigned sflag, unsigned cflag);
    std::string FlagToStr(unsigned int flag);
};
#endif
