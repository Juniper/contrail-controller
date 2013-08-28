/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "route_kstate.h"
#include <net/address.h>
#include "vr_defs.h"

using namespace std;

RouteKState::RouteKState(KRouteResp *obj, std::string resp_ctx, 
                              vr_route_req &req, int id) :
                              KState(resp_ctx, obj) {
    InitEncoder(req, id);
}

void RouteKState::InitEncoder(vr_route_req &req, int id) {
    req.set_rtr_family(AF_INET);
    req.set_rtr_vrf_id(id);
    req.set_rtr_rid(0);
    /* Only dump is supported */
    req.set_h_op(sandesh_op::DUMP);
}

void RouteKState::Handler() {
    KRouteResp *resp = static_cast<KRouteResp *>(resp_obj_);
    if (resp) {
        if (MoreData()) {
            /* There are more routes in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(resp_data_);
            resp->Response();
            RouteContext *rctx = static_cast<RouteContext *>(more_ctx_);
            if (rctx) {
                delete rctx;
                more_ctx_ = NULL;
            }
        }
    }
}

void RouteKState::SendNextRequest() {
    vr_route_req req;
    RouteContext *rctx = static_cast<RouteContext *>(more_ctx_);

    InitEncoder(req, rctx->vrf_id);
    req.set_rtr_marker(rctx->marker);
    req.set_rtr_marker_plen(rctx->marker_plen);
    EncodeAndSend(req);
}

void RouteKState::SendResponse() {

    KRouteResp *resp = static_cast<KRouteResp *>(resp_obj_);
    resp->set_context(resp_data_);
    resp->set_more(true);
    resp->Response();
    ResetCount();

    resp_obj_ = new KRouteResp();
}

string RouteKState::FamilyToString(int nh_family) {
    unsigned family = nh_family;
    switch(family) {
        case AF_INET:
            return "AF_INET";
        default:
            return "INVALID";
    }
}

string RouteKState::LabelFlagsToString(int flags) {
    if (flags == 0) {
        return "--NONE--";
    }

    string str = "";
    if (flags & VR_RT_LABEL_VALID_FLAG) {
        str += "MPLS ";
    }

    if (flags & VR_RT_HOSTED_FLAG) {
        str += "PROXY-ARP";
    }
    return str;
}
