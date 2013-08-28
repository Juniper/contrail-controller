/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_kstate_h
#define vnsw_agent_mpls_kstate_h

class MplsKState: public KState {
public:
    MplsKState(KMplsResp *obj, std::string resp_ctx, vr_mpls_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};

#endif //vnsw_agent_mpls_kstate_h
