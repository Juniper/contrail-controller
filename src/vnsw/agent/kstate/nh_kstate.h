/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_nh_kstate_h
#define vnsw_agent_nh_kstate_h

class NHKState: public KState {
public:
    NHKState(KNHResp *obj, const std::string &resp_ctx, 
             vr_nexthop_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    const std::string TypeToString(int type) const;
    const std::string FamilyToString(int family) const;
    const std::string FlagsToString(uint32_t flags) const;
    const std::string EncapFamilyToString(int family) const;
    const std::string EncapToString(const std::vector<signed char> &encap) const;
    void SetComponentNH(vr_nexthop_req *req, KNHInfo &info);
    const string NHKState::IPv6ToString(const vector<signed char> &ipv6) const;
};

#endif //vnsw_agent_nh_kstate_h
