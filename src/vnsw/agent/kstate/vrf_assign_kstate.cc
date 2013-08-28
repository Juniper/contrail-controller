/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "vrf_assign_kstate.h"

VrfAssignKState::VrfAssignKState(KVrfAssignResp *obj, std::string resp_ctx,
                                 vr_vrf_assign_req &req, int id) : 
    KState(resp_ctx, obj) {

    req.set_h_op(sandesh_op::DUMP);
    req.set_var_vif_index(id);
    req.set_var_marker(-1);
}

void VrfAssignKState::SendNextRequest() {
    VrfAssignContext *ctx = static_cast<VrfAssignContext *>(more_ctx_);
    vr_vrf_assign_req req;

    req.set_h_op(sandesh_op::DUMP);
    req.set_var_vif_index(ctx->vif_index_);
    req.set_var_vlan_id(ctx->marker_);
    req.set_var_marker(ctx->marker_);
    EncodeAndSend(req);
}

void VrfAssignKState::Handler() {
    KVrfAssignResp *resp = static_cast<KVrfAssignResp *>(resp_obj_);
    if (resp) {
        if (MoreData()) {
            /* There are more labels in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(resp_data_);
            resp->Response();
            VrfAssignContext *ctx = static_cast<VrfAssignContext *>(more_ctx_);
            if (ctx) {
                delete ctx;
                more_ctx_ = NULL;
            }
        }
    }
}

void VrfAssignKState::SendResponse() {
    KVrfAssignResp *resp = static_cast<KVrfAssignResp *>(resp_obj_);
    resp->set_context(resp_data_);
    resp->set_more(true);
    resp->Response();
    ResetCount();

    resp_obj_ = new KVrfAssignResp();
}
