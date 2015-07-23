/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/time_util.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"

using std::string;
using std::vector;

static char kIterSeparator[] = "||";

//
// Class template to handle the following combinations:

// ShowRoutingInstanceReq + ShowRoutingInstanceReqIterate
// ShowRoutingInstanceSummaryReq + ShowRoutingInstanceSummaryReqIterate
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
template <typename ReqT, typename ReqIterateT, typename RespT>
class ShowRoutingInstanceCommonHandler {
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
        vector<ShowRoutingInstance> sri_list;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new Data);
    }

    static void FillRoutingInstanceTableInfo(ShowRoutingInstanceTable *srit,
        const BgpSandeshContext *bsc, const BgpTable *table);
    static void FillRoutingInstanceInfo(ShowRoutingInstance *sri_list,
        const BgpSandeshContext *bsc, const RoutingInstance *rtinstance,
        bool summary);

    static void ConvertReqToData(const ReqT *req, Data *data);
    static bool ConvertReqIterateToData(const ReqIterateT *req_iterate,
        Data *data);
    static void SaveContextToData(const string &next_instance, bool done,
        Data *data);

    static bool CallbackCommon(const BgpSandeshContext *bsc, bool summary,
        Data *data);
    static bool Callback(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data, bool summary);
    static bool CallbackIterate(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data, bool summary);
};

//
// Fill in information for a table.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
void ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::FillRoutingInstanceTableInfo(
    ShowRoutingInstanceTable *srit,
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
    srit->prefixes = table->Size();
    srit->primary_paths = table->GetPrimaryPathCount();
    srit->secondary_paths = table->GetSecondaryPathCount();
    srit->infeasible_paths = table->GetInfeasiblePathCount();
    srit->paths = srit->primary_paths + srit->secondary_paths;
}

//
// Fill in information for an instance.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
void ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::FillRoutingInstanceInfo(
    ShowRoutingInstance *sri, const BgpSandeshContext *bsc,
    const RoutingInstance *rtinstance, bool summary) {
    sri->set_name(rtinstance->name());
    sri->set_virtual_network(rtinstance->virtual_network());
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

    if (!summary) {
        const PeerRibMembershipManager *pmm = bsc->bgp_server->membership_mgr();
        vector<ShowRoutingInstanceTable> srit_list;
        const RoutingInstance::RouteTableList &tables = rtinstance->GetTables();
        for (RoutingInstance::RouteTableList::const_iterator it =
             tables.begin(); it != tables.end(); ++it) {
            ShowRoutingInstanceTable srit;
            FillRoutingInstanceTableInfo(&srit, bsc, it->second);
            pmm->FillRoutingInstanceTableInfo(&srit, it->second);
            srit_list.push_back(srit);
        }
        sri->set_tables(srit_list);
    }
}

//
// Initialize Data from ReqT.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
void ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::ConvertReqToData(
    const ReqT *req, Data *data) {
    if (data->initialized)
        return;
    data->initialized = true;
    data->search_string = req->get_search_string();
}

//
// Initialize Data from ReqInterateT.
//
// Return false if there's a problem parsing the iterate_info string.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
bool ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::ConvertReqIterateToData(
    const ReqIterateT *req_iterate, Data *data) {
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
template <typename ReqT, typename ReqIterateT, typename RespT>
void ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::SaveContextToData(
    const string &next_instance, bool done, Data *data) {
    data->next_instance = next_instance;
    if (done)
        data->next_batch = next_instance + kIterSeparator + data->search_string;
}

//
// Common routine for regular and iterate requests.
// Assumes that Data has been initialized properly by caller.
// Examine specified maximum instances starting at next_instance.
//
// Return true if we're examined all instances or reached the page limit.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
bool ShowRoutingInstanceCommonHandler<ReqT, ReqIterateT, RespT>::CallbackCommon(
    const BgpSandeshContext *bsc, bool summary, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(data->next_instance);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        const RoutingInstance *rtinstance = it->second;
        if (!data->search_string.empty() &&
            (rtinstance->name().find(data->search_string) == string::npos) &&
            (data->search_string != "deleted" || !rtinstance->deleted())) {
            continue;
        }
        ShowRoutingInstance sri;
        FillRoutingInstanceInfo(&sri, bsc, rtinstance, summary);
        data->sri_list.push_back(sri);
        if (data->sri_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it == rim->name_cend() || ++it == rim->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = data->sri_list.size() >= page_limit;
    SaveContextToData(it->second->name(), done, data);
    return done;
}

//
// Callback for ReqT. This gets called for initial request and subsequently in
// cases where the iteration count is reached.
//
// Return false if the iteration count is reached before page gets filled.
// Return true if the page gets filled or we've examined all instances.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
bool ShowRoutingInstanceCommonHandler<ReqT, ReqIterateT, RespT>::Callback(
    const Sandesh *sr, const RequestPipeline::PipeSpec ps,
    int stage, int instNum, RequestPipeline::InstData *data, bool summary) {
    Data *mydata = static_cast<Data *>(data);
    const ReqT *req = static_cast<const ReqT *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req->client_context());

    // Parse request and save state in Data.
    ConvertReqToData(req, mydata);

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of instances examined.
    if (!CallbackCommon(bsc, summary, mydata))
        return false;

    // All done - ship the response.
    RespT *resp = new RespT;
    resp->set_context(req->context());
    if (!mydata->sri_list.empty())
        resp->set_instances(mydata->sri_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

//
// Callback for ReqIterate. This is called for initial request and subsequently
// in cases where the iteration count is reached. Parse the iterate_info string
// to figure out the next instance to examine.
//
// Return false if the iteration limit is reached before page gets filled.
// Return true if the page gets filled or we've examined all instances.
//
template <typename ReqT, typename ReqIterateT, typename RespT>
bool ShowRoutingInstanceCommonHandler<
    ReqT, ReqIterateT, RespT>::CallbackIterate(const Sandesh *sr,
    const RequestPipeline::PipeSpec ps, int stage, int instNum,
    RequestPipeline::InstData *data, bool summary) {
    Data *mydata = static_cast<Data *>(data);
    const ReqIterateT *req_iterate =
        static_cast<const ReqIterateT *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req_iterate->client_context());

    // Parse request and save state in Data.
    if (!ConvertReqIterateToData(req_iterate, mydata)) {
        RespT *resp = new RespT;
        resp->set_context(req_iterate->context());
        resp->Response();
        return true;
    }

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of instances examined.
    if (!CallbackCommon(bsc, summary, mydata))
        return false;

    // All done - ship the response.
    RespT *resp = new RespT;
    resp->set_context(req_iterate->context());
    if (!mydata->sri_list.empty())
        resp->set_instances(mydata->sri_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

//
// Handler for ShowRoutingInstanceReq.
//
void ShowRoutingInstanceReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp>::Callback,
        _1, _2, _3, _4, _5, false);
    s1.allocFn_ = ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp>::CreateData;
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
    s1.cbFn_ = boost::bind(&ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp>::CallbackIterate,
        _1, _2, _3, _4, _5, false);
    s1.allocFn_ = ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceReq,
        ShowRoutingInstanceReqIterate,
        ShowRoutingInstanceResp>::CreateData;
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
    s1.cbFn_ = boost::bind(&ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp>::Callback,
        _1, _2, _3, _4, _5, true);
    s1.allocFn_ = ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp>::CreateData;
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
    s1.cbFn_ = boost::bind(&ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp>::CallbackIterate,
        _1, _2, _3, _4, _5, true);
    s1.allocFn_ = ShowRoutingInstanceCommonHandler<
        ShowRoutingInstanceSummaryReq,
        ShowRoutingInstanceSummaryReqIterate,
        ShowRoutingInstanceSummaryResp>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
