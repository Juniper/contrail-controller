/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_nh_kstate_h
#define vnsw_agent_nh_kstate_h

class NHKState: public KState {
public:
    NHKState(KNHResp *obj, std::string resp_ctx, vr_nexthop_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    static std::string TypeToString(int type);
    static std::string FamilyToString(int family);
    static std::string FlagsToString(short flags);
    static std::string EncapFamilyToString(int family);
    static std::string EncapToString(const std::vector<signed char> &encap);
    void SetComponentNH(vr_nexthop_req *req, KNHInfo &info);
};

#endif //vnsw_agent_nh_kstate_h
