/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_kstate_h
#define vnsw_agent_flow_kstate_h

class FlowKState : public Task {
 public:
    FlowKState(Agent *agent, const std::string &resp_ctx, int idx);
    FlowKState(Agent *agent, const std::string &resp_ctx, 
               const std::string &iter_idx);
    virtual void SendResponse(KFlowResp *resp) const;
    
    virtual bool Run();
    std::string Description() const { return "FlowKState"; }
    void SetFlowData(std::vector<KFlowInfo> &list, const vr_flow_entry *k_flow,
                     int index) const;
    void set_evicted_flag(bool show_evicted) { evicted_ = show_evicted;}
protected:
    std::string response_context_;
    int flow_idx_;
    bool evicted_;
    uint32_t flow_iteration_key_;
private:
    Agent *agent_;
    void UpdateFlagStr(std::string &str, bool &set, bool tcp, unsigned sflag,
                       unsigned cflag) const;
    const std::string FlagToStr(unsigned int flag) const;
    const std::string TcpFlagToStr(unsigned int flag) const;
    const std::string DropCodeToStr(uint8_t drop_code) const;
};
#endif
