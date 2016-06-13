/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "forwarding_class_kstate.h"

ForwardingClassKState::ForwardingClassKState(
        KForwardingClassResp *obj, const std::string &resp_ctx,
        vr_fc_map_req &req, int id) : KState(resp_ctx, obj) {
    std::vector<int16_t> l;
    l.push_back(id);
    req.set_fmr_id(l);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        l.clear(); l.push_back(-1);
        req.set_fmr_id(l);
        req.set_fmr_marker(-1);
    }
}

void ForwardingClassKState::SendNextRequest() {
    vr_fc_map_req req;
    req.set_h_op(sandesh_op::DUMP);
    int id = reinterpret_cast<long>(more_context_);
    req.set_fmr_marker(id);
    EncodeAndSend(req);
}

void ForwardingClassKState::Handler() {
    KForwardingClassResp *resp = static_cast<KForwardingClassResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(response_context_);
            resp->Response();
            more_context_ = NULL;
        }
    }
}

void ForwardingClassKState::SendResponse() {
    KForwardingClassResp *resp = static_cast<KForwardingClassResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();
    response_object_ = new KForwardingClassResp();
}
