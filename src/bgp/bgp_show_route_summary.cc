/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/regex.hpp>

#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for a table.
//
static void FillRouteTableSummaryInfo(ShowRouteTableSummary *srts,
    const BgpSandeshContext *bsc, const BgpTable *table) {
    srts->set_name(table->name());
    srts->set_deleted(table->IsDeleted());
    srts->set_deleted_at(
        UTCUsecToString(table->deleter()->delete_time_stamp_usecs()));
    srts->set_prefixes(table->Size());
    srts->set_primary_paths(table->GetPrimaryPathCount());
    srts->set_secondary_paths(table->GetSecondaryPathCount());
    srts->set_infeasible_paths(table->GetInfeasiblePathCount());
    srts->set_stale_paths(table->GetStalePathCount());
    srts->set_llgr_stale_paths(table->GetLlgrStalePathCount());
    srts->set_paths(srts->get_primary_paths() + srts->get_secondary_paths());
    srts->set_walk_requests(table->walk_request_count());
    srts->set_walk_again_requests(table->walk_again_count());
    srts->set_actual_walks(table->walk_count());
    srts->set_walk_completes(table->walk_complete_count());
    srts->set_walk_cancels(table->walk_cancel_count());
    size_t markers = 0;
    srts->set_pending_updates(table->GetPendingRiboutsCount(&markers));
    srts->set_markers(markers);
    srts->set_listeners(table->GetListenerCount());
    srts->set_walkers(table->walker_count());
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
// Note that we check the page and iteration limits only after examining all
// tables for a given routing instance. This simplifies things and requires
// to only keep track of the next instance rather than the next instance and
// next table.
//
template <>
bool BgpShowHandler<ShowRouteSummaryReq, ShowRouteSummaryReqIterate,
    ShowRouteSummaryResp, ShowRouteTableSummary>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    regex search_expr(data->search_string);
    RoutingInstanceMgr::const_name_iterator it1 =
        rim->name_clower_bound(data->next_entry);
    for (uint32_t iter_count = 0; it1 != rim->name_cend(); ++it1) {
        const RoutingInstance *rtinstance = it1->second;
        for (RoutingInstance::RouteTableList::const_iterator it2 =
             rtinstance->GetTables().begin();
             it2 != rtinstance->GetTables().end(); ++it2, ++iter_count) {
            const BgpTable *table = it2->second;
            if ((!regex_search(table->name(), search_expr)) &&
                (data->search_string != "deleted" || !table->IsDeleted())) {
                continue;
            }
            ShowRouteTableSummary srts;
            FillRouteTableSummaryInfo(&srts, bsc, table);
            data->show_list.push_back(srts);
        }
        if (data->show_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it1 == rim->name_cend() || ++it1 == rim->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = data->show_list.size() >= page_limit;
    SaveContextToData(it1->second->name(), done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList.
//
template <>
void BgpShowHandler<ShowRouteSummaryReq, ShowRouteSummaryReqIterate,
    ShowRouteSummaryResp, ShowRouteTableSummary>::FillShowList(
    ShowRouteSummaryResp *resp,
    const vector<ShowRouteTableSummary> &show_list) {
    resp->set_tables(show_list);
}

//
// Handler for ShowRouteSummaryReq.
//
void ShowRouteSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteSummaryReq,
        ShowRouteSummaryReqIterate,
        ShowRouteSummaryResp,
        ShowRouteTableSummary>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteSummaryReq,
        ShowRouteSummaryReqIterate,
        ShowRouteSummaryResp,
        ShowRouteTableSummary>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRouteSummaryReqIterate.
//
void ShowRouteSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteSummaryReq,
        ShowRouteSummaryReqIterate,
        ShowRouteSummaryResp,
        ShowRouteTableSummary>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteSummaryReq,
        ShowRouteSummaryReqIterate,
        ShowRouteSummaryResp,
        ShowRouteTableSummary>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
