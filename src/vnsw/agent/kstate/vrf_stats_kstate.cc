/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "vrf_stats_kstate.h"
#include <iomanip>
#include <sstream>
#include "vr_defs.h"

using namespace std;

VrfStatsKState::VrfStatsKState(KVrfStatsResp *obj, const std::string &resp_ctx,
                               vr_vrf_stats_req &req, int id) : 
                               KState(resp_ctx, obj) {
    if (id >= 0) {
        req.set_h_op(sandesh_op::GET);
        req.set_vsr_vrf(id);    
        req.set_vsr_family(AF_INET);
        req.set_vsr_type(RT_UCAST);
    } else {
        InitDumpRequest(req);
        req.set_vsr_marker(-1);
    }
}

void VrfStatsKState::InitDumpRequest(vr_vrf_stats_req &req) const {
    req.set_h_op(sandesh_op::DUMP);
    req.set_vsr_rid(0);
    req.set_vsr_family(AF_INET);
    req.set_vsr_type(RT_UCAST);
}

void VrfStatsKState::Handler() {
    KVrfStatsResp *resp = static_cast<KVrfStatsResp *>(response_object_);
    if (resp) {
        if (MoreData()) {
            /* There are more interfaces in Kernel. We need to query them from 
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

void VrfStatsKState::SendNextRequest() {
    vr_vrf_stats_req req;
    InitDumpRequest(req);
    int idx = reinterpret_cast<long>(more_context_);
    req.set_vsr_marker(idx);
    EncodeAndSend(req);
}

void VrfStatsKState::SendResponse() {

    KVrfStatsResp *resp = static_cast<KVrfStatsResp *>(response_object_);
    resp->set_context(response_context_);
    resp->set_more(true);
    resp->Response();

    response_object_ = new KVrfStatsResp();
}

const string VrfStatsKState::TypeToString(int vrf_stats_type) const {
    unsigned short type = vrf_stats_type;
    switch(type) {
        case RT_UCAST:
            return "RT_UCAST";
        case RT_MCAST:
            return "RT_MCAST";
        default:
            return "INVALID";
    }
}

const string VrfStatsKState::FamilyToString(int vrf_family) const {
    unsigned family = vrf_family;
    switch(family) {
        case AF_INET:
            return "AF_INET";
        default:
            return "INVALID";
    }
}

