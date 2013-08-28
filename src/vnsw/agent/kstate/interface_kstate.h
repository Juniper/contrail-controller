/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_kstate_h
#define vnsw_agent_interface_kstate_h

class InterfaceKState: public KState {
public:
    InterfaceKState(KInterfaceResp *obj, std::string resp_ctx, 
                    vr_interface_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    void InitDumpRequest(vr_interface_req &req);
    static std::string TypeToString(int type);
    static std::string FlagsToString(int flags);
    static std::string MacToString(const std::vector<signed char> &mac);
};
#endif //vnsw_agent_interface_kstate_h
