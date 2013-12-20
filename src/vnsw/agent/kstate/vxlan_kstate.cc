/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "vxlan_kstate.h"

VxLanKState::VxLanKState(KVxLanResp *obj, std::string resp_ctx, 
                       vr_vxlan_req &req, int id) : KState(resp_ctx, obj) {
    req.set_vxlanr_vnid(id);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        req.set_vxlanr_vnid(-1);
    }
}

void VxLanKState::SendNextRequest() {
    vr_vxlan_req req;
    req.set_h_op(sandesh_op::DUMP);
    req.set_vxlanr_vnid(0);
    int label = reinterpret_cast<long>(more_context_);
    req.set_vxlanr_vnid(label);
    EncodeAndSend(req);
}

void VxLanKState::Handler() {
    KVxLanResp *resp = static_cast<KVxLanResp *>(response_object_);
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
            more_context_ = NULL;
        }
    }
}

void VxLanKState::SendResponse() {

    KVxLanResp *resp = static_cast<KVxLanResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KVxLanResp();
}


