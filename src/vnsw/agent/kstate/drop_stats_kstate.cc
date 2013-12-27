/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "drop_stats_kstate.h"
#include <iomanip>
#include <sstream>
#include "vr_defs.h"

using namespace std;

DropStatsKState::DropStatsKState(KDropStatsResp *obj, 
                                 const std::string &resp_ctx,
                                 vr_drop_stats_req &req) 
    : KState(resp_ctx, obj) {
    req.set_h_op(sandesh_op::GET);
    req.set_vds_rid(0);    
}

void DropStatsKState::Handler() {
    KDropStatsResp *resp = static_cast<KDropStatsResp *>(response_object_);
    if (resp) {
        resp->set_context(response_context_);
        resp->Response();
    }
}

