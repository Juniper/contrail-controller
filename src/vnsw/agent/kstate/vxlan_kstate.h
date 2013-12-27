/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vxlan_kstate_h
#define vnsw_agent_vxlan_kstate_h

class VxLanKState: public KState {
public:
    VxLanKState(KVxLanResp *obj, const std::string &resp_ctx,
                vr_vxlan_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};

#endif //vnsw_agent_vxlan_kstate_h
