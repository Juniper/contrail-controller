/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_forwarding_class_kstate_h
#define vnsw_agent_forwarding_class_kstate_h

class ForwardingClassKState: public KState {
public:
    ForwardingClassKState(KForwardingClassResp *obj, const std::string &resp_ctx,
                          vr_fc_map_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};
#endif
