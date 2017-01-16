/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_kstate_h
#define vnsw_agent_route_kstate_h

class RouteKState: public KState {
public:
    RouteKState(KRouteResp *obj, const std::string &resp_ctx,
                vr_route_req &encoder, int id, int family_id, sandesh_op::type op_code, int prefix_size);
    int family_id_;
    sandesh_op::type op_code_;
    // This is used to set route prefix in next vr_route_req
    std::vector<int8_t> prefix_;
    virtual void SendResponse();
    virtual void Handler();
    void InitEncoder(vr_route_req &req, int id, sandesh_op::type op_code) const;
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
