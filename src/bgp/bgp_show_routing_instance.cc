/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/foreach.hpp>
#include <boost/regex.hpp>

#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-policy/routing_policy.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for a table.
//
static void FillRoutingInstanceTableInfo(ShowRoutingInstanceTable *srit,
    const BgpSandeshContext *bsc, const BgpTable *table) {
    srit->set_name(table->name());
    srit->set_deleted(table->IsDeleted());
    srit->set_walk_requests(table->walk_request_count());
    srit->set_walk_completes(table->walk_complete_count());
    srit->set_walk_cancels(table->walk_cancel_count());
    size_t markers = 0;
    srit->set_pending_updates(table->GetPendingRiboutsCount(&markers));
    srit->set_markers(markers);
    srit->set_listeners(table->GetListenerCount());
    srit->set_walkers(table->walker_count());
    srit->set_prefixes(table->Size());
    srit->set_primary_paths(table->GetPrimaryPathCount());
    srit->set_secondary_paths(table->GetSecondaryPathCount());
    srit->set_infeasible_paths(table->GetInfeasiblePathCount());
    srit->set_stale_paths(table->GetStalePathCount());
    srit->set_llgr_stale_paths(table->GetLlgrStalePathCount());
    srit->set_paths(srit->get_primary_paths() + srit->get_secondary_paths());
}

//
// Fill in information for an instance.
//
static void FillRoutingInstanceInfo(ShowRoutingInstance *sri,
    const BgpSandeshContext *bsc, const RoutingInstance *rtinstance,
    bool summary) {
    sri->set_name(rtinstance->name());
    sri->set_virtual_network(rtinstance->GetVirtualNetworkName());
    sri->set_vn_index(rtinstance->virtual_network_index());
    sri->set_vxlan_id(rtinstance->vxlan_id());
    sri->set_deleted(rtinstance->deleted());
    sri->set_deleted_at(
        UTCUsecToString(rtinstance->deleter()->delete_time_stamp_usecs()));
    vector<string> import_rt;
    BOOST_FOREACH(RouteTarget rt, rtinstance->GetImportList()) {
        import_rt.push_back(rt.ToString());
    }
    sri->set_import_target(import_rt);
    vector<string> export_rt;
    BOOST_FOREACH(RouteTarget rt, rtinstance->GetExportList()) {
        export_rt.push_back(rt.ToString());
    }
    sri->set_export_target(export_rt);
    sri->set_always_subscribe(rtinstance->always_subscribe());
    sri->set_allow_transit(rtinstance->virtual_network_allow_transit());
    sri->set_pbb_evpn_enable(rtinstance->virtual_network_pbb_evpn_enable());

    if (summary)
        return;

    const BgpMembershipManager *bmm = bsc->bgp_server->membership_mgr();
    vector<ShowRoutingInstanceTable> srit_list;
    const RoutingInstance::RouteTableList &tables = rtinstance->GetTables();
    for (RoutingInstance::RouteTableList::const_iterator it =
        tables.begin(); it != tables.end(); ++it) {
        ShowRoutingInstanceTable srit;
        FillRoutingInstanceTableInfo(&srit, bsc, it->second);
        bmm->FillRoutingInstanceTableInfo(&srit, it->second);
        srit_list.push_back(srit);
    }
    sri->set_tables(srit_list);

    vector<ShowInstanceRoutingPolicyInfo> policy_list;
    BOOST_FOREACH(RoutingPolicyInfo info, rtinstance->routing_policies()) {
        ShowInstanceRoutingPolicyInfo show_policy_info;
        RoutingPolicyPtr policy = info.first;
        show_policy_info.set_policy_name(policy->name());
        show_policy_info.set_generation(info.second);
        policy_list.push_back(show_policy_info);
    }
    sri->set_routing_policies(policy_list);

    const PeerManager *peer_manager = rtinstance->peer_manager();
    if (peer_manager) {
        vector<string> neighbors;
        for (const BgpPeer *peer = peer_manager->NextPeer(BgpPeerKey());
            peer != NULL; peer = peer_manager->NextPeer(peer->peer_key())) {
            neighbors.push_back(peer->peer_name());
        }
        sri->set_neighbors(neighbors);
    }
}

//
// Fill in information for list of instances.
//
// Allows regular and summary introspect to share code.
//
static bool FillRoutingInstanceInfoList(const BgpSandeshContext *bsc,
    bool summary, uint32_t page_limit, uint32_t iter_limit,
    const string &start_instance, const string &search_string,
    vector<ShowRoutingInstance> *sri_list, string *next_instance) {
    regex search_expr(search_string);
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(start_instance);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        const RoutingInstance *rtinstance = it->second;
        if (!search_string.empty() &&
            (!regex_search(rtinstance->name(), search_expr)) &&
            (search_string != "deleted" || !rtinstance->deleted())) {
            continue;
        }
        ShowRoutingInstance sri;
        FillRoutingInstanceInfo(&sri, bsc, rtinstance, summary);
        sri_list->push_back(sri);
        if (sri_list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it == rim->name_cend() || ++it == rim->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = sri_list->size() >= page_limit;
    *next_instance = it->second->name();
    return done;
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for regular introspect.
//
template <>
bool BgpShowHandler<ShowRoutingInstanceReq, ShowRoutingInstanceReqIterate,
    ShowRoutingInstanceResp, ShowRoutingInstance>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillRoutingInstanceInfoList(bsc, false, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_instance);
    if (!next_instance.empty())
        SaveContextToData(next_instance, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for regular introspect.
//
template <>
void BgpShowHandler<ShowRoutingInstanceReq, ShowRoutingInstanceReqIterate,
    ShowRoutingInstanceResp, ShowRoutingInstance>::FillShowList(
    ShowRoutingInstanceResp *resp,
    const vector<ShowRoutingInstance> &show_list) {
    resp->set_instances(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for summary introspect.
//
template <>
bool BgpShowHandler<ShowRoutingInstanceSummaryReq,
    ShowRoutingInstanceSummaryReqIterate,
    ShowRoutingInstanceSummaryResp, ShowRoutingInstance>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillRoutingInstanceInfoList(bsc, true, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_instance);
    if (!next_instance.empty())
        SaveContextToData(next_instance, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for summary introspect.
//
template <>
void BgpShowHandler<ShowRoutingInstanceSummaryReq,
    ShowRoutingInstanceSummaryReqIterate,
    ShowRoutingInstanceSummaryResp, ShowRoutingInstance>::FillShowList(
    ShowRoutingInstanceSummaryResp *resp,
    const vector<ShowRoutingInstance> &show_list) {
    resp->set_instances(show_list);
}

//
// Handler for ShowRoutingInstanceReq.
//
void ShowRoutingInstanceReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp,
        ShowRoutingInstance>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp,
        ShowRoutingInstance>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRoutingInstanceReqIterate.
//
void ShowRoutingInstanceReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp,
        ShowRoutingInstance>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp,
        ShowRoutingInstance>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRoutingInstanceSummaryReq.
//
void ShowRoutingInstanceSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp,
        ShowRoutingInstance>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp,
        ShowRoutingInstance>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRoutingInstanceSummaryReqIterate.
//
void ShowRoutingInstanceSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp,
        ShowRoutingInstance>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp,
        ShowRoutingInstance>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
