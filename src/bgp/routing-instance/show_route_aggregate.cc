/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include "bgp/bgp_server.h"
#include "bgp/bgp_show_handler.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/route_aggregator.h"
#include "bgp/routing-instance/route_aggregate_types.h"

using std::string;
using std::vector;

static bool FillRouteAggregateInfo(Address::Family family,
                                const string search_string,
                                AggregateRouteEntriesInfo &info,
                                RoutingInstance *rtinstance) {
    const BgpTable *table =
        static_cast<const BgpTable *>(rtinstance->GetTable(family));
    if (!table)
        return false;
    if (!search_string.empty() &&
        (table->name().find(search_string) == string::npos) &&
        (search_string != "deleted" || !table->IsDeleted())) {
        return false;
    }

    IRouteAggregator *iroute_aggregator = rtinstance->route_aggregator(family);
    if (!iroute_aggregator)
        return false;
    return iroute_aggregator->FillAggregateRouteInfo(rtinstance, &info);
}

// Specialization of BgpShowHandler<>::CallbackCommon.
template <>
bool BgpShowHandler<ShowRouteAggregateReq, ShowRouteAggregateReqIterate,
    ShowRouteAggregateResp, AggregateRouteEntriesInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(data->next_entry);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        RoutingInstance *rinstance = it->second;
        AggregateRouteEntriesInfo info;
        if (FillRouteAggregateInfo(Address::INET, data->search_string, info,
                                rinstance)) {
            data->show_list.push_back(info);
        }
        if (FillRouteAggregateInfo(Address::INET6, data->search_string, info,
                                rinstance)) {
            data->show_list.push_back(info);
        }

        if (data->show_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it == rim->name_cend() || ++it == rim->name_cend())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = data->show_list.size() >= page_limit;
    SaveContextToData(it->second->name(), done, data);
    return done;
}

// Specialization of BgpShowHandler<>::FillShowList.
template <>
void BgpShowHandler<ShowRouteAggregateReq, ShowRouteAggregateReqIterate,
    ShowRouteAggregateResp, AggregateRouteEntriesInfo>::FillShowList(
        ShowRouteAggregateResp *resp,
        const vector<AggregateRouteEntriesInfo> &show_list) {
    resp->set_aggregate_route_entries(show_list);
}

// Handler for ShowRouteAggregateReq.
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
