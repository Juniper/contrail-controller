/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"


#include "bgp/bgp_peer_internal_types.h"
#include "bgp/routing-instance/peer_manager.h"

using std::string;
using std::vector;

//
// Specialization of BgpShowHandler<>::CallbackCommon for regular introspect.
//
// Note that we don't both paginating bgp neighbors since the list should be
// pretty small. We always add at least one xmpp neighbor to the list and then
// paginate the xmpp neighbors as needed.
//
template <>
bool BgpShowHandler<BgpNeighborReq, BgpNeighborReqIterate,
    BgpNeighborListResp, BgpNeighborResp>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;

    // Look only at the master instance since we don't have bgp neighbors in
    // other instances currently.
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    const RoutingInstance *rtinstance = rim->GetDefaultRoutingInstance();
    if (rtinstance && data->next_entry.empty()) {
        rtinstance->peer_manager()->FillBgpNeighborInfo(
            bsc, &data->show_list, data->search_string, false);
    }

    // Add xmpp neighbors.
    string next_neighbor;
    bool done = bsc->ShowNeighborExtension(bsc, false, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_neighbor);
    if (!next_neighbor.empty())
        SaveContextToData(next_neighbor, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for regular introspect.
//
template <>
void BgpShowHandler<BgpNeighborReq, BgpNeighborReqIterate,
    BgpNeighborListResp, BgpNeighborResp>::FillShowList(
    BgpNeighborListResp *resp, const vector<BgpNeighborResp> &show_list) {
    resp->set_neighbors(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for summary introspect.
//
// Note that we don't both paginating bgp neighbors since the list should be
// pretty small. We always add at least one xmpp neighbor to the list and then
// paginate the xmpp neighbors as needed.
//
template <>
bool BgpShowHandler<ShowBgpNeighborSummaryReq, ShowBgpNeighborSummaryReqIterate,
    ShowBgpNeighborSummaryResp, BgpNeighborResp>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;

    // Look only at the master instance since we don't have bgp neighbors in
    // other instances currently.
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    const RoutingInstance *rtinstance = rim->GetDefaultRoutingInstance();
    if (rtinstance && data->next_entry.empty()) {
        rtinstance->peer_manager()->FillBgpNeighborInfo(
            bsc, &data->show_list, data->search_string, true);
    }

    // Add xmpp neighbors.
    string next_neighbor;
    bool done = bsc->ShowNeighborExtension(bsc, true, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_neighbor);
    if (!next_neighbor.empty())
        SaveContextToData(next_neighbor, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for summary introspect.
//
template <>
void BgpShowHandler<ShowBgpNeighborSummaryReq, ShowBgpNeighborSummaryReqIterate,
    ShowBgpNeighborSummaryResp, BgpNeighborResp>::FillShowList(
    ShowBgpNeighborSummaryResp *resp,
    const vector<BgpNeighborResp> &show_list) {
    resp->set_neighbors(show_list);
}

//
// Handler for BgpNeighborReq.
//
void BgpNeighborReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::PeerMembership");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        BgpNeighborReq,
        BgpNeighborReqIterate,
        BgpNeighborListResp,
        BgpNeighborResp>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        BgpNeighborReq,
        BgpNeighborReqIterate,
        BgpNeighborListResp,
        BgpNeighborResp>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for BgpNeighborReqIterate.
//
void BgpNeighborReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::PeerMembership");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        BgpNeighborReq,
        BgpNeighborReqIterate,
        BgpNeighborListResp,
        BgpNeighborResp>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        BgpNeighborReq,
        BgpNeighborReqIterate,
        BgpNeighborListResp,
        BgpNeighborResp>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpNeighborSummaryReq.
//
void ShowBgpNeighborSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::PeerMembership");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpNeighborSummaryReq,
        ShowBgpNeighborSummaryReqIterate,
        ShowBgpNeighborSummaryResp,
        BgpNeighborResp>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpNeighborSummaryReq,
        ShowBgpNeighborSummaryReqIterate,
        ShowBgpNeighborSummaryResp,
        BgpNeighborResp>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpNeighborSummaryReqIterate.
//
void ShowBgpNeighborSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::PeerMembership");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpNeighborSummaryReq,
        ShowBgpNeighborSummaryReqIterate,
        ShowBgpNeighborSummaryResp,
        BgpNeighborResp>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpNeighborSummaryReq,
        ShowBgpNeighborSummaryReqIterate,
        ShowBgpNeighborSummaryResp,
        BgpNeighborResp>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
