/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "route_kstate.h"
#include <net/address.h>
#include "vr_defs.h"

using namespace std;

RouteKState::RouteKState(KRouteResp *obj, const std::string &resp_ctx, 
                         vr_route_req &req, int id) :
                         KState(resp_ctx, obj) {
    InitEncoder(req, id);
}

void RouteKState::InitEncoder(vr_route_req &req, int id) const {
    req.set_rtr_family(AF_INET);
    req.set_rtr_vrf_id(id);
    req.set_rtr_rid(0);
    /* Only dump is supported */
    req.set_h_op(sandesh_op::DUMP);
}

void RouteKState::Handler() {
    KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more routes in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(response_context_);
            resp->Response();
            RouteContext *rctx = static_cast<RouteContext *>(more_context_);
            if (rctx) {
                delete rctx;
                more_context_ = NULL;
            }
        }
    }
}

void RouteKState::SendNextRequest() {
    vr_route_req req;
    RouteContext *rctx = static_cast<RouteContext *>(more_context_);

    InitEncoder(req, rctx->vrf_id);
    req.set_rtr_marker(rctx->marker);
    req.set_rtr_marker_plen(rctx->marker_plen);
    EncodeAndSend(req);
}

void RouteKState::SendResponse() {

    KRouteResp *resp = static_cast<KRouteResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KRouteResp();
}

const string RouteKState::FamilyToString(int nh_family) const {
    unsigned family = nh_family;
    switch(family) {
        case AF_INET:
            return "AF_INET";
        default:
            return "INVALID";
    }
}

const string RouteKState::LabelFlagsToString(int flags) const {
    if (flags == 0) {
        return "--NONE--";
    }

    string str = "";
    if (flags & VR_RT_LABEL_VALID_FLAG) {
        str += "MPLS ";
    }

    if (flags & VR_RT_ARP_PROXY_FLAG) {
        str += "PROXY-ARP";
    }
    return str;
}
