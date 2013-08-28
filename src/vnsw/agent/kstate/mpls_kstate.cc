/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "mpls_kstate.h"

MplsKState::MplsKState(KMplsResp *obj, std::string resp_ctx, 
                       vr_mpls_req &req, int id) : KState(resp_ctx, obj) {
    req.set_mr_label(id);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        req.set_mr_marker(-1);
    }
}

void MplsKState::SendNextRequest() {
    vr_mpls_req req;
    req.set_h_op(sandesh_op::DUMP);
    req.set_mr_label(0);
    int label = reinterpret_cast<long>(more_ctx_);
    req.set_mr_marker(label);
    EncodeAndSend(req);
}

void MplsKState::Handler() {
    KMplsResp *resp = static_cast<KMplsResp *>(resp_obj_);
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
            more_ctx_ = NULL;
        }
    }
}

void MplsKState::SendResponse() {

    KMplsResp *resp = static_cast<KMplsResp *>(resp_obj_);
    resp->set_context(resp_data_);
    resp->set_more(true);
    resp->Response();
    ResetCount();

    resp_obj_ = new KMplsResp();
}


