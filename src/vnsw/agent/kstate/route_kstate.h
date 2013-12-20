/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_kstate_h
#define vnsw_agent_route_kstate_h

class RouteKState: public KState {
public:
    RouteKState(KRouteResp *obj, std::string resp_ctx, vr_route_req &encoder, 
                int id);
    virtual void SendResponse();
    virtual void Handler();
    void InitEncoder(vr_route_req &req, int id);
    virtual void SendNextRequest();
    static std::string FamilyToString(int family);
    static std::string LabelFlagsToString(int flags);
};

struct RouteContext {
    uint32_t vrf_id;
    int      marker;
    int      marker_plen;
};
#endif //vnsw_agent_route_kstate_h
