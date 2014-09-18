/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "kstate.h"
#include "interface_kstate.h"
#include "route_kstate.h"
#include "nh_kstate.h"
#include "mpls_kstate.h"
#include "flow_kstate.h"
#include "mirror_kstate.h"
#include "vxlan_kstate.h"
#include "vrf_assign_kstate.h"
#include "vrf_stats_kstate.h"
#include "drop_stats_kstate.h"

void KInterfaceReq::HandleRequest() const {
    vr_interface_req req;
    KInterfaceResp *resp = new KInterfaceResp();
    resp->set_context(context());

    InterfaceKState *kstate = new InterfaceKState(resp, context(), req, 
                                                  get_if_id());
    kstate->EncodeAndSend(req);
}

void KRouteReq::HandleRequest() const {
    vr_route_req req;
    std::vector<int8_t> marker;

    req.set_rtr_marker(marker);
    KRouteResp *resp = new KRouteResp();
    resp->set_context(context());

    RouteKState *kstate = new RouteKState(resp, context(), req, get_vrf_id());
    kstate->EncodeAndSend(req);
}

void KNHReq::HandleRequest() const {
    vr_nexthop_req req;
    KNHResp *resp = new KNHResp();
    resp->set_context(context());

    NHKState *kstate = new NHKState(resp, context(), req, get_nh_id());
    kstate->EncodeAndSend(req);
}

void KMplsReq::HandleRequest() const {
    vr_mpls_req req;
    KMplsResp *resp = new KMplsResp();
    resp->set_context(context());

    MplsKState *kstate = new MplsKState(resp, context(), req, get_mpls_label());
    kstate->EncodeAndSend(req);
}

void NextKFlowReq::HandleRequest() const {
    FlowKState *task = new FlowKState(Agent::GetInstance(), context(), 
                                      get_flow_handle());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void KFlowReq::HandleRequest() const {
    FlowKState *task = new FlowKState(Agent::GetInstance(), context(), 
                                      get_flow_idx());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void KMirrorReq::HandleRequest() const {
    vr_mirror_req req;
    KMirrorResp *resp = new KMirrorResp();
    resp->set_context(context());

    MirrorKState *kstate = new MirrorKState(resp, context(), req, 
                                            get_mirror_id());
    kstate->EncodeAndSend(req);
}

void KVrfAssignReq::HandleRequest() const {
    vr_vrf_assign_req req;
    KVrfAssignResp *resp = new KVrfAssignResp();
    resp->set_context(context());

    VrfAssignKState *kstate = new VrfAssignKState(resp, context(), req, 
                                                  get_vif_index());
    kstate->EncodeAndSend(req);
}

void KVrfStatsReq::HandleRequest() const {
    vr_vrf_stats_req req;
    KVrfStatsResp *resp = new KVrfStatsResp();
    resp->set_context(context());

    VrfStatsKState *kstate = new VrfStatsKState(resp, context(), req, 
                                                get_vrf_index());
    kstate->EncodeAndSend(req);
}

void KDropStatsReq::HandleRequest() const {
    vr_drop_stats_req req;
    KDropStatsResp *resp = new KDropStatsResp();
    resp->set_context(context());

    DropStatsKState *kstate = new DropStatsKState(resp, context(), req);
    kstate->EncodeAndSend(req);
}

void KVxLanReq::HandleRequest() const {
    vr_vxlan_req req;
    KVxLanResp *resp = new KVxLanResp();
    resp->set_context(context());

    VxLanKState *kstate = new VxLanKState(resp, context(), req, 
                                          get_vxlan_label());
    kstate->EncodeAndSend(req);
}


