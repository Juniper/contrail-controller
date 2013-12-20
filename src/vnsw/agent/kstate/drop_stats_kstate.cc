/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "drop_stats_kstate.h"
#include <iomanip>
#include <sstream>
#include "vr_defs.h"

using namespace std;

DropStatsKState::DropStatsKState(KDropStatsResp *obj, std::string resp_ctx, 
                                 vr_drop_stats_req &req) : 
                                 KState(resp_ctx, obj) {
    req.set_h_op(sandesh_op::GET);
    req.set_vds_rid(0);    
}

void DropStatsKState::Handler() {
    KDropStatsResp *resp = static_cast<KDropStatsResp *>(resp_obj_);
    if (resp) {
        resp->set_context(resp_ctx_);
        resp->Response();
    }
}

string DropStatsKState::TypeToString(int vrf_stats_type) {
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

string DropStatsKState::FamilyToString(int vrf_family) {
    unsigned family = vrf_family;
    switch(family) {
        case AF_INET:
            return "AF_INET";
        default:
            return "INVALID";
    }
}

