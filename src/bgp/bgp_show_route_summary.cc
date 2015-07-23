/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/time_util.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"

using std::string;
using std::vector;

static char kIterSeparator[] = "||";

//
// Class to handle ShowRouteSummaryReq
//
// Supports pagination of output as specified by page limit.
//
// Also supports an iteration limit, which is the maximum number of entries
// examined in one run. This is useful when the search string is non-empty
// and there are a large number of entries in table.  We don't want to look
// at potentially all entries in one shot in cases where most of them don't
// match the search string.
//
// Data is used to store partial pages of results as well as other context
// that needs to be maintained between successive runs of the callbacks in
// cases where we don't manage to fill a page of results in one run.
//
// Note that the infrastructure automatically reschedules and invokes the
// callback function again if it returns false.
//
class ShowRouteSummaryHandler {
public:
    static const uint32_t kPageLimit = 64;
    static const uint32_t kIterLimit = 1024;

    struct Data : public RequestPipeline::InstData {
        Data() : initialized(false) {
        }

        bool initialized;
        string search_string;
        string next_instance;
        string next_batch;
        vector<ShowRouteTableSummary> srts_list;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new Data);
    }

    static void FillRouteTableSummaryInfo(ShowRouteTableSummary *srts,
        const BgpSandeshContext *bsc, const BgpTable *table);

    static void ConvertReqToData(const ShowRouteSummaryReq *req, Data *data);
    static bool ConvertReqIterateToData(
        const ShowRouteSummaryReqIterate *req_iterate, Data *data);
    static void SaveContextToData(const string &next_instance, bool done,
        Data *data);

    static bool CallbackCommon(const BgpSandeshContext *bsc, Data *data);
    static bool Callback(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data);
    static bool CallbackIterate(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data);
};

//
// Fill in information for a table.
//
void ShowRouteSummaryHandler::FillRouteTableSummaryInfo(
    ShowRouteTableSummary *srts,
    const BgpSandeshContext *bsc, const BgpTable *table) {
    srts->set_name(table->name());
    srts->set_deleted(table->IsDeleted());
    srts->set_deleted_at(
        UTCUsecToString(table->deleter()->delete_time_stamp_usecs()));
    srts->prefixes = table->Size();
    srts->primary_paths = table->GetPrimaryPathCount();
    srts->secondary_paths = table->GetSecondaryPathCount();
    srts->infeasible_paths = table->GetInfeasiblePathCount();
    srts->paths = srts->primary_paths + srts->secondary_paths;
    srts->set_walk_requests(table->walk_request_count());
    srts->set_walk_completes(table->walk_complete_count());
    srts->set_walk_cancels(table->walk_cancel_count());
    size_t markers = 0;
    srts->set_pending_updates(table->GetPendingRiboutsCount(&markers));
    srts->set_markers(markers);
    srts->set_listeners(table->GetListenerCount());
    srts->set_walkers(table->walker_count());
}

//
// Initialize Data from ShowRouteSummaryReq.
//
void ShowRouteSummaryHandler::ConvertReqToData(const ShowRouteSummaryReq *req,
    Data *data) {
    if (data->initialized)
        return;
    data->initialized = true;
    data->search_string = req->get_search_string();
}

//
// Initialize Data from ShowRouteSummaryReqIterate.
//
// Return false if there's a problem parsing the iterate_info string.
//
bool ShowRouteSummaryHandler::ConvertReqIterateToData(
    const ShowRouteSummaryReqIterate *req_iterate, Data *data) {
    if (data->initialized)
        return true;
    data->initialized = true;

    // Format of iterate_info:
    // NextRI||search_string
    string iterate_info = req_iterate->get_iterate_info();
    size_t sep_size = strlen(kIterSeparator);

    size_t pos1 = iterate_info.find(kIterSeparator);
    if (pos1 == string::npos)
        return false;

    data->next_instance = iterate_info.substr(0, pos1);
    data->search_string = iterate_info.substr(pos1 + sep_size);
    return true;
}

//
// Save context into Data.
// The next_instance field gets used in the subsequent invocation of callback
// routine when the callback routine returns false.
// The next_batch string is used if the page is filled (i.e. done is true).
//
void ShowRouteSummaryHandler::SaveContextToData(
    const string &next_instance, bool done, Data *data) {
    data->next_instance = next_instance;
    if (done)
        data->next_batch = next_instance + kIterSeparator + data->search_string;
}

//
// Common routine for regular and iterate requests.
// Assumes that Data has been initialized properly by caller.
// Examine specified maximum tables starting at first table for next_instance.
//
// Note that we check the page and iteration limits only after examining all
// tables for a given routing instance. This simplifies things and requires
// to only keep track of the next instance rather than the next instance and
// next table.
//
// Return true if we're examined all instances or reached the page limit.
//
bool ShowRouteSummaryHandler::CallbackCommon(const BgpSandeshContext *bsc,
    Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    RoutingInstanceMgr::const_name_iterator it1 =
        rim->name_clower_bound(data->next_instance);
    for (uint32_t iter_count = 0; it1 != rim->name_cend(); ++it1) {
        const RoutingInstance *rtinstance = it1->second;
        for (RoutingInstance::RouteTableList::const_iterator it2 =
             rtinstance->GetTables().begin();
             it2 != rtinstance->GetTables().end(); ++it2, ++iter_count) {
            const BgpTable *table = it2->second;
            if (!data->search_string.empty() &&
                (table->name().find(data->search_string) == string::npos) &&
                (data->search_string != "deleted" || !table->IsDeleted())) {
                continue;
            }
            ShowRouteTableSummary srts;
            FillRouteTableSummaryInfo(&srts, bsc, table);
            data->srts_list.push_back(srts);
        }
        if (data->srts_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it1 == rim->name_cend() || ++it1 == rim->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = data->srts_list.size() >= page_limit;
    SaveContextToData(it1->second->name(), done, data);
    return done;
}

//
// Callback for ShowRouteSummaryReq. This gets called for initial request and
// subsequently in cases where the iteration count is reached.
//
// Return false if the iteration count is reached before page gets filled.
// Return true if the page gets filled or we've examined all instances.
//
bool ShowRouteSummaryHandler::Callback(const Sandesh *sr,
    const RequestPipeline::PipeSpec ps, int stage, int instNum,
    RequestPipeline::InstData *data) {
    Data *mydata = static_cast<Data *>(data);
    const ShowRouteSummaryReq *req =
        static_cast<const ShowRouteSummaryReq *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req->client_context());

    // Parse request and save state in Data.
    ConvertReqToData(req, mydata);

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of instances examined.
    if (!CallbackCommon(bsc, mydata))
        return false;

    // All done - ship the response.
    ShowRouteSummaryResp *resp = new ShowRouteSummaryResp;
    resp->set_context(req->context());
    if (!mydata->srts_list.empty())
        resp->set_tables(mydata->srts_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

//
// Callback for ShowRouteSummaryReqIterate. This is called for initial request
// and subsequently in cases where the iteration count is reached. Parse the
// iterate_info string to figure out the next instance to examine.
//
// Return false if the iteration limit is reached before page gets filled.
// Return true if the page gets filled or we've examined all instances.
//
bool ShowRouteSummaryHandler::CallbackIterate(const Sandesh *sr,
    const RequestPipeline::PipeSpec ps, int stage, int instNum,
    RequestPipeline::InstData *data) {
    Data *mydata = static_cast<Data *>(data);
    const ShowRouteSummaryReqIterate *req_iterate =
        static_cast<const ShowRouteSummaryReqIterate *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req_iterate->client_context());

    // Parse request and save state in Data.
    if (!ConvertReqIterateToData(req_iterate, mydata)) {
        ShowRouteSummaryResp *resp = new ShowRouteSummaryResp;
        resp->set_context(req_iterate->context());
        resp->Response();
        return true;
    }

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of instances examined.
    if (!CallbackCommon(bsc, mydata))
        return false;

    // All done - ship the response.
    ShowRouteSummaryResp *resp = new ShowRouteSummaryResp;
    resp->set_context(req_iterate->context());
    if (!mydata->srts_list.empty())
        resp->set_tables(mydata->srts_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

//
// Handler for ShowRouteSummaryReq.
//
void ShowRouteSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowRouteSummaryHandler::Callback;
    s1.allocFn_ = ShowRouteSummaryHandler::CreateData;
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
    s1.cbFn_ = ShowRouteSummaryHandler::CallbackIterate;
    s1.allocFn_ = ShowRouteSummaryHandler::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
