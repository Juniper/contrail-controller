/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
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
// Specialization of BgpShowHandler<>::CallbackCommon.
//
// Note that we check the page and iteration limits only after examining all
// ribouts in all tables for a given routing instance. This simplifies things
// and requires to only keep track of the next instance rather than the next
// instance and next table.
//
template <>
bool BgpShowHandler<ShowRibOutStatisticsReq, ShowRibOutStatisticsReqIterate,
    ShowRibOutStatisticsResp, ShowRibOutStatistics>::CallbackCommon(
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
            if (!regex_search(table->name(), search_expr))
                continue;
            table->FillRibOutStatisticsInfo(&data->show_list);
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
void BgpShowHandler<ShowRibOutStatisticsReq, ShowRibOutStatisticsReqIterate,
    ShowRibOutStatisticsResp, ShowRibOutStatistics>::FillShowList(
    ShowRibOutStatisticsResp *resp,
    const vector<ShowRibOutStatistics> &show_list) {
    resp->set_ribouts(show_list);
}

//
// Handler for ShowRibOutStatisticsReq.
//
void ShowRibOutStatisticsReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRibOutStatisticsReq,
        ShowRibOutStatisticsReqIterate,
        ShowRibOutStatisticsResp,
        ShowRibOutStatistics>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRibOutStatisticsReq,
        ShowRibOutStatisticsReqIterate,
        ShowRibOutStatisticsResp,
        ShowRibOutStatistics>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRibOutStatisticsReqIterate.
//
void ShowRibOutStatisticsReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRibOutStatisticsReq,
        ShowRibOutStatisticsReqIterate,
        ShowRibOutStatisticsResp,
        ShowRibOutStatistics>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRibOutStatisticsReq,
        ShowRibOutStatisticsReqIterate,
        ShowRibOutStatisticsResp,
        ShowRibOutStatistics>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
