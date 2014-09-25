/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_kstate_h
#define vnsw_agent_route_kstate_h

class RouteKState: public KState {
public:
    RouteKState(KRouteResp *obj, const std::string &resp_ctx, 
                vr_route_req &encoder, int id);
    virtual void SendResponse();
    virtual void Handler();
    void InitEncoder(vr_route_req &req, int id) const;
    virtual void SendNextRequest();
    const std::string FamilyToString(int family) const;
    const std::string LabelFlagsToString(int flags) const;
};

struct RouteContext {
    uint32_t vrf_id;
    std::vector<int8_t> marker;
    int      marker_plen;
};
#endif //vnsw_agent_route_kstate_h
