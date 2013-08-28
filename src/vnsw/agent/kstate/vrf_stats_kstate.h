/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_stats_kstate_h
#define vnsw_agent_vrf_stats_kstate_h

class VrfStatsKState: public KState {
public:
    VrfStatsKState(KVrfStatsResp *obj, std::string resp_ctx, 
                   vr_vrf_stats_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    virtual void SendNextRequest();
    void InitDumpRequest(vr_vrf_stats_req &req);
    static std::string TypeToString(int type);
    static std::string FamilyToString(int family);
};
#endif //vnsw_agent_vrf_stats_kstate_h
