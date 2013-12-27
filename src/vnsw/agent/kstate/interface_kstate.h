/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_kstate_h
#define vnsw_agent_interface_kstate_h

class InterfaceKState: public KState {
public:
    InterfaceKState(KInterfaceResp *obj, const std::string &resp_ctx, 
                    vr_interface_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    void InitDumpRequest(vr_interface_req &req) const;
    const std::string TypeToString(int type) const;
    const std::string FlagsToString(int flags) const;
    const std::string MacToString(const std::vector<signed char> &mac) const;
};
#endif //vnsw_agent_interface_kstate_h
