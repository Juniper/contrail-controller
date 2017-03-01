/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/regex.hpp>

#include "bgp/bgp_server.h"
#include "bgp/bgp_show_handler.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/route_aggregator.h"
#include "bgp/routing-instance/route_aggregate_internal_types.h"
#include "bgp/routing-instance/route_aggregate_types.h"

using boost::assign::list_of;
using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

static bool FillRouteAggregateInfoList(const BgpSandeshContext *bsc,
    bool summary, uint32_t page_limit, uint32_t iter_limit,
    const string &start_instance, const string &search_string,
    vector<AggregateRouteEntriesInfo> *are_list, string *next_instance) {
    regex search_expr(search_string);
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(start_instance);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        RoutingInstance *rtinstance = it->second;

        vector<Address::Family> families =
            list_of(Address::INET)(Address::INET6);
        BOOST_FOREACH(Address::Family family, families) {
            const BgpTable *table =
                static_cast<const BgpTable *>(rtinstance->GetTable(family));
            if (!table)
                continue;
            if ((!regex_search(table->name(), search_expr)) &&
                (search_string != "deleted" || !table->IsDeleted())) {
                continue;
            }

            IRouteAggregator *iroute_aggregator =
                rtinstance->route_aggregator(family);
            if (!iroute_aggregator)
                continue;
            AggregateRouteEntriesInfo info;
            if (!iroute_aggregator->FillAggregateRouteInfo(&info, summary))
                continue;
            are_list->push_back(info);
        }

        if (are_list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it == rim->name_cend() || ++it == rim->name_cend())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = are_list->size() >= page_limit;
    *next_instance = it->second->name();
    return done;
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for regular introspect.
//
template <>
bool BgpShowHandler<ShowRouteAggregateReq,
    ShowRouteAggregateReqIterate, ShowRouteAggregateResp,
    AggregateRouteEntriesInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillRouteAggregateInfoList(bsc, false, page_limit, iter_limit,
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
void BgpShowHandler<ShowRouteAggregateReq,
    ShowRouteAggregateReqIterate, ShowRouteAggregateResp,
    AggregateRouteEntriesInfo>::FillShowList(
    ShowRouteAggregateResp *resp,
    const vector<AggregateRouteEntriesInfo> &show_list) {
    resp->set_aggregate_route_entries(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for summary introspect.
//
template <>
bool BgpShowHandler<ShowRouteAggregateSummaryReq,
    ShowRouteAggregateSummaryReqIterate, ShowRouteAggregateSummaryResp,
    AggregateRouteEntriesInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillRouteAggregateInfoList(bsc, true, page_limit, iter_limit,
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
void BgpShowHandler<ShowRouteAggregateSummaryReq,
    ShowRouteAggregateSummaryReqIterate, ShowRouteAggregateSummaryResp,
    AggregateRouteEntriesInfo>::FillShowList(
    ShowRouteAggregateSummaryResp *resp,
    const vector<AggregateRouteEntriesInfo> &show_list) {
    resp->set_aggregate_route_entries(show_list);
}

//
// Handler for ShowRouteAggregateReq.
//
void ShowRouteAggregateReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RouteAggregate");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteAggregateReq,
        ShowRouteAggregateReqIterate,
        ShowRouteAggregateResp,
        AggregateRouteEntriesInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteAggregateReq,
        ShowRouteAggregateReqIterate,
        ShowRouteAggregateResp,
        AggregateRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRouteAggregateReqIterate.
//
void ShowRouteAggregateReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RouteAggregate");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteAggregateReq,
        ShowRouteAggregateReqIterate,
        ShowRouteAggregateResp,
        AggregateRouteEntriesInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteAggregateReq,
        ShowRouteAggregateReqIterate,
        ShowRouteAggregateResp,
        AggregateRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRouteAggregateSummaryReq.
//
void ShowRouteAggregateSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RouteAggregate");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteAggregateSummaryReq,
        ShowRouteAggregateSummaryReqIterate,
        ShowRouteAggregateSummaryResp,
        AggregateRouteEntriesInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteAggregateSummaryReq,
        ShowRouteAggregateSummaryReqIterate,
        ShowRouteAggregateSummaryResp,
        AggregateRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRouteAggregateSummaryReqIterate.
//
void ShowRouteAggregateSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RouteAggregate");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRouteAggregateSummaryReq,
        ShowRouteAggregateSummaryReqIterate,
        ShowRouteAggregateSummaryResp,
        AggregateRouteEntriesInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRouteAggregateSummaryReq,
        ShowRouteAggregateSummaryReqIterate,
        ShowRouteAggregateSummaryResp,
        AggregateRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
