/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_assign_kstate_h
#define vnsw_agent_vrf_assign_kstate_h

class VrfAssignKState: public KState {
public:
    VrfAssignKState(KVrfAssignResp *obj, const std::string &resp_ctx,
                    vr_vrf_assign_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};

struct VrfAssignContext {
    uint16_t vif_index_;
    int      marker_;
};

#endif //vnsw_agent_vrf_assign_kstate_h
