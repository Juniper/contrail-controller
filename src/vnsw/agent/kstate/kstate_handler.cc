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
#include "forwarding_class_kstate.h"
#include "qos_config_kstate.h"
#include "vrf_kstate.h"

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
    int prefix_size, family_id;
    std::string family = get_family();

    if(family == "inet6") {
        prefix_size = 16;
        family_id = AF_INET6;
    } else if(family == "bridge") {
        prefix_size = 6;
        family_id = AF_BRIDGE;
    } else if(family == "inet") {
        prefix_size = 4;
        family_id = AF_INET;
    } else {
        std::string msg("Allowed options for family are inet, inet6, bridge");
        ErrResp *resp = new ErrResp();
        resp->set_resp(msg);
        resp->set_context(context());
        resp->Response();
        return;
    }

    std::vector<int8_t> marker(prefix_size, 0);
    if(family_id == AF_BRIDGE) {
        req.set_rtr_mac(marker);
    } else {
        // rtr_prefix needs to be initialized
        req.set_rtr_prefix(marker);
        req.set_rtr_marker_plen(0);
    }
    req.set_rtr_marker(marker);
    KRouteResp *resp = new KRouteResp();
    resp->set_context(context());

    RouteKState *kstate = new RouteKState(resp, context(), req, get_vrf_id(), family_id, sandesh_op::DUMP, prefix_size);
    kstate->EncodeAndSend(req);
}

void KRouteGetReq::HandleRequest() const {
    vr_route_req req;
    int family_id, prefix_size;
    boost::system::error_code ec;
    IpAddress addr(IpAddress::from_string(get_prefix(), ec));

    if(addr.is_v4()) {
        family_id = AF_INET;
        prefix_size = 4;
        Ip4Address::bytes_type bytes = addr.to_v4().to_bytes();
        std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
        req.set_rtr_prefix(rtr_prefix);
    } else if(addr.is_v6()) {
        family_id = AF_INET6;
        prefix_size = 16;
        Ip6Address::bytes_type bytes = addr.to_v6().to_bytes();
        std::vector<int8_t> rtr_prefix(bytes.begin(), bytes.end());
        req.set_rtr_prefix(rtr_prefix);
    } else {
        std::string msg("Allowed options for family are inet, inet6");
        ErrResp *resp = new ErrResp();
        resp->set_resp(msg);
        resp->set_context(context());
        resp->Response();
        return;
    }
    std::vector<int8_t> marker(prefix_size, 0);
    // rtr_prefix needs to be initialized
    req.set_rtr_marker(marker);
    req.set_rtr_marker_plen(0);
    req.set_rtr_prefix_len(get_prefix_len());
    KRouteResp *resp = new KRouteResp();
    resp->set_context(context());

    RouteKState *kstate = new RouteKState(resp, context(), req, get_vrf_id(), family_id, sandesh_op::GET, 0);
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
    vector<string> tokens;
    boost::split(tokens, get_flow_handle(), boost::is_any_of(" "));
    if (tokens.size() == 2) {
        task->set_evicted_flag(true);
    }
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void KFlowReq::HandleRequest() const {
    FlowKState *task = new FlowKState(Agent::GetInstance(), context(),
                                      get_flow_idx());
    task->set_evicted_flag(get_show_evicted());
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

void KVrfReq::HandleRequest() const {
    vr_vrf_req req;
    KVrfResp *resp = new KVrfResp();
    resp->set_context(context());

    VrfKState *kstate = new VrfKState(resp, context(), req,
                                          get_vrf_idx());
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

void KQosConfigReq::HandleRequest() const {

    vr_qos_map_req req;
    KQosConfigResp *resp = new KQosConfigResp();
    resp->set_context(context());

    QosConfigKState *kstate = new QosConfigKState(resp, context(), req,
                                                  get_index());
    kstate->EncodeAndSend(req);
}

void KForwardingClassReq::HandleRequest() const {
    vr_fc_map_req req;
    KForwardingClassResp *resp = new KForwardingClassResp();
    resp->set_context(context());

    ForwardingClassKState *kstate = new ForwardingClassKState(resp, context(),
                                                  req,
                                                  get_index());
    kstate->EncodeAndSend(req);
}
