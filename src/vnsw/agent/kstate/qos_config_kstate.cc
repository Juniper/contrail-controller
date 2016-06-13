/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "qos_config_kstate.h"

QosConfigKState::QosConfigKState(KQosConfigResp *obj, const std::string &resp_ctx,
                         vr_qos_map_req &req, int id) : KState(resp_ctx, obj) {
    req.set_qmr_id(id);
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
    } else {
        req.set_h_op(sandesh_op::DUMP);
        req.set_qmr_marker(-1);
    }
}

void QosConfigKState::SendNextRequest() {
    vr_qos_map_req req;
    req.set_h_op(sandesh_op::DUMP);
    req.set_qmr_id(0);
    int id = reinterpret_cast<long>(more_context_);
    req.set_qmr_marker(id);
    EncodeAndSend(req);
}

void QosConfigKState::Handler() {
    KQosConfigResp *resp = static_cast<KQosConfigResp *>(response_object_);
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

void QosConfigKState::SendResponse() {
    KQosConfigResp *resp = static_cast<KQosConfigResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();
    response_object_ = new KQosConfigResp();
}
