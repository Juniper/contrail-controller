/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/regex.hpp>

#include "bgp/bgp_config.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_show_handler.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/static_route.h"
#include "bgp/routing-instance/static_route_types.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

static bool FillStaticRouteInfo(Address::Family family,
                                const string &search_string,
                                const regex &search_expr,
                                StaticRouteEntriesInfo &info,
                                RoutingInstance *rtinstance) {
    const BgpTable *table =
        static_cast<const BgpTable *>(rtinstance->GetTable(family));
    if (!table)
        return false;
    if ((!regex_search(table->name(), search_expr)) &&
        (search_string != "deleted" || !table->IsDeleted())) {
        return false;
    }

    IStaticRouteMgr *imanager = rtinstance->static_route_mgr(family);
    if (!imanager)
        return false;
    return imanager->FillStaticRouteInfo(rtinstance, &info);
}

// Specialization of BgpShowHandler<>::CallbackCommon.
template <>
bool BgpShowHandler<ShowStaticRouteReq, ShowStaticRouteReqIterate,
    ShowStaticRouteResp, StaticRouteEntriesInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    regex search_expr(data->search_string);
    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(data->next_entry);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        RoutingInstance *rinstance = it->second;
        StaticRouteEntriesInfo info;
        if (FillStaticRouteInfo(Address::INET, data->search_string,
                                search_expr, info, rinstance)) {
            data->show_list.push_back(info);
        }
        if (FillStaticRouteInfo(Address::INET6, data->search_string,
                                search_expr, info, rinstance)) {
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
void BgpShowHandler<ShowStaticRouteReq, ShowStaticRouteReqIterate,
    ShowStaticRouteResp, StaticRouteEntriesInfo>::FillShowList(
        ShowStaticRouteResp *resp,
        const vector<StaticRouteEntriesInfo> &show_list) {
    resp->set_static_route_entries(show_list);
}

// Handler for ShowStaticRouteReq.
void ShowStaticRouteReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::StaticRoute");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowStaticRouteReq,
        ShowStaticRouteReqIterate,
        ShowStaticRouteResp,
        StaticRouteEntriesInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowStaticRouteReq,
        ShowStaticRouteReqIterate,
        ShowStaticRouteResp,
        StaticRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowStaticRouteReqIterate.
//
void ShowStaticRouteReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::StaticRoute");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowStaticRouteReq,
        ShowStaticRouteReqIterate,
        ShowStaticRouteResp,
        StaticRouteEntriesInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowStaticRouteReq,
        ShowStaticRouteReqIterate,
        ShowStaticRouteResp,
        StaticRouteEntriesInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
