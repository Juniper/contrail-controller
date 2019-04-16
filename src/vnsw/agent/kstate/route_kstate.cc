/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "route_kstate.h"
#include <base/address.h>
#include "vr_defs.h"

using namespace std;

RouteKState::RouteKState(KRouteResp *obj, const std::string &resp_ctx,
                         vr_route_req &req, int id, int family_id, sandesh_op::type op_code, int prefix_size) :
                         KState(resp_ctx, obj), family_id_(family_id), op_code_(op_code), prefix_(prefix_size, 0) {
    InitEncoder(req, id, op_code_);
}

void RouteKState::InitEncoder(vr_route_req &req, int id, sandesh_op::type op_code_) const {
    req.set_rtr_family(family_id_);
    req.set_rtr_vrf_id(id);
    req.set_rtr_rid(0);
    if(op_code_ == sandesh_op::DUMP)
        req.set_rtr_prefix_len(0);
    req.set_h_op(op_code_);
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
            if (!more_context_.empty()) {
                RouteContext *rctx =
                    boost::any_cast<RouteContext *>(more_context_);
                if (rctx) {
                    delete rctx;
                    more_context_ = boost::any();
                }
            }
        }
    }
}

void RouteKState::SendNextRequest() {
    vr_route_req req;
    RouteContext *rctx = boost::any_cast<RouteContext *>(more_context_);

    InitEncoder(req, rctx->vrf_id, op_code_);
    if (family_id_ == AF_BRIDGE) {
        req.set_rtr_mac(rctx->marker);
    } else {
        // rtr_prefix needs to be initialized
        req.set_rtr_prefix(prefix_);
        req.set_rtr_marker(rctx->marker);
        req.set_rtr_marker_plen(rctx->marker_plen);
    }
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
        case AF_INET6:
            return "AF_INET6";
        case AF_BRIDGE:
            return "AF_BRIDGE";
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
