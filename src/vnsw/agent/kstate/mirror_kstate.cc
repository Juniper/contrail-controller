/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "mirror_kstate.h"
#include "vr_mirror.h"

using namespace std;

MirrorKState::MirrorKState(KMirrorResp *obj, const std::string &resp_ctx,
    vr_mirror_req &req, int id): KState(resp_ctx, obj) {

    req.set_mirr_index(id);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        req.set_mirr_marker(-1);
    }
}

void MirrorKState::SendNextRequest() {
    vr_mirror_req req;

    req.set_mirr_index(0);
    req.set_h_op(sandesh_op::DUMP);
    int idx = reinterpret_cast<long>(more_context_);
    req.set_mirr_marker(idx);
    EncodeAndSend(req);
}

void MirrorKState::Handler() {
    KMirrorResp *resp = static_cast<KMirrorResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more nexthops in Kernel. We need to query them from 
             * Kernel and send it to Sandesh.
             */
            SendResponse();
            SendNextRequest();
        } else {
            resp->set_context(response_context_);
            resp->Response();
            more_context_ = NULL;
        }
    }
}

void MirrorKState::SendResponse() {

    KMirrorResp *resp = static_cast<KMirrorResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KMirrorResp();
}

const string MirrorKState::FlagsToString(int flags) const {
    if (flags == 0) {
        return "NIL";
    }
    if (flags & VR_MIRROR_FLAG_MARKED_DELETE) {
        return "MARKED_DELETE";
    }
    return "INVALID";
}

