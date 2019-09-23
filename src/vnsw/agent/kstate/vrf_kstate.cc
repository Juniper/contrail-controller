/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "vrf_kstate.h"

VrfKState::VrfKState(KVrfResp *obj,
                                 const std::string &resp_ctx,
                                 vr_vrf_req &req, int id) :
    KState(resp_ctx, obj) {

    req.set_h_op(sandesh_op::DUMP);
    req.set_vrf_idx(id);
    req.set_vrf_marker(-1);
}

void VrfKState::SendNextRequest() {
    VrfContext *ctx = boost::any_cast<VrfContext *>(more_context_);
    vr_vrf_req req;

    req.set_h_op(sandesh_op::DUMP);
    req.set_vrf_idx(ctx->vrf_idx_);
    req.set_vrf_hbfl_vif_idx(ctx->hbf_lintf_);
    req.set_vrf_hbfr_vif_idx(ctx->hbf_rintf_);
    req.set_vrf_marker(ctx->marker_);
    EncodeAndSend(req);
}

void VrfKState::Handler() {
    KVrfResp *resp = static_cast<KVrfResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more labels in Kernel. We need to query them from
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(response_context_);
            resp->Response();
            if (!more_context_.empty()) {
                VrfContext *ctx =
                    boost::any_cast<VrfContext *>(more_context_);
                if (ctx) {
                    delete ctx;
                    more_context_ = boost::any();
                }
            }
        }
    }
}

void VrfKState::SendResponse() {
    KVrfResp *resp = static_cast<KVrfResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KVrfResp();
}
