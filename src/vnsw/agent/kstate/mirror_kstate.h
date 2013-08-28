/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mirror_kstate_h
#define vnsw_agent_mirro_kstate_h

class MirrorKState: public KState {
public:
    MirrorKState(KMirrorResp *obj, std::string resp_ctx, 
                 vr_mirror_req &req, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    static std::string FlagsToString(int flags);
};

#endif //vnsw_agent_mirror_kstate_h
