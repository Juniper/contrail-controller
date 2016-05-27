/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_qos_config_kstate_h
#define vnsw_agent_qos_config_kstate_h

class QosConfigKState: public KState {
public:
    QosConfigKState(KQosConfigResp *obj, const std::string &resp_ctx,
                    vr_qos_map_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
};

#endif
