/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_kstate_h
#define vnsw_agent_vrf_kstate_h

class VrfKState: public KState {
public:
    VrfKState(KVrfResp *obj, const std::string &resp_ctx,
                    vr_vrf_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};

struct VrfContext {
    uint32_t vrf_idx_;
    uint32_t hbf_lintf_;
    uint32_t hbf_rintf_;
    int      marker_;
};

#endif //vnsw_agent_vrf_kstate_h
