/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/regex.hpp>

#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for list of path resolvers.
//
// Allows regular and summary introspect to share code.
//
static bool FillPathResolverInfoList(const BgpSandeshContext *bsc,
    bool summary, uint32_t page_limit, uint32_t iter_limit,
    const string &start_instance, const string &search_string,
    vector<ShowPathResolver> *spr_list, string *next_instance) {
    regex search_expr(search_string);
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    RoutingInstanceMgr::const_name_iterator it1 =
        rim->name_clower_bound(start_instance);
    for (uint32_t iter_count = 0; it1 != rim->name_cend();
         ++it1, ++iter_count) {
        const RoutingInstance *rtinstance = it1->second;
        for (RoutingInstance::RouteTableList::const_iterator it2 =
             rtinstance->GetTables().begin();
             it2 != rtinstance->GetTables().end(); ++it2, ++iter_count) {
            const BgpTable *table = it2->second;
            if ((!regex_search(table->name(), search_expr)) &&
                (search_string != "deleted" || !table->IsDeleted())) {
                continue;
            }
            const PathResolver *path_resolver = table->path_resolver();
            if (!path_resolver)
                continue;
            ShowPathResolver spr;
            path_resolver->FillShowInfo(&spr, summary);
            if (spr.get_path_count() == 0 &&
                spr.get_modified_path_count() == 0 &&
                spr.get_nexthop_count() == 0 &&
                spr.get_modified_nexthop_count() == 0)
                continue;
            spr_list->push_back(spr);
        }
        if (spr_list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it1 == rim->name_cend() || ++it1 == rim->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = spr_list->size() >= page_limit;
    *next_instance = it1->second->name();
    return done;
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for regular introspect.
//
template <>
bool BgpShowHandler<ShowPathResolverReq, ShowPathResolverReqIterate,
    ShowPathResolverResp, ShowPathResolver>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillPathResolverInfoList(bsc, false, page_limit, iter_limit,
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
void BgpShowHandler<ShowPathResolverReq,
    ShowPathResolverReqIterate, ShowPathResolverResp,
    ShowPathResolver>::FillShowList(ShowPathResolverResp *resp,
    const vector<ShowPathResolver> &show_list) {
    resp->set_resolvers(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for summary introspect.
//
template <>
bool BgpShowHandler<ShowPathResolverSummaryReq,
    ShowPathResolverSummaryReqIterate, ShowPathResolverSummaryResp,
    ShowPathResolver>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_instance;
    bool done = FillPathResolverInfoList(bsc, true, page_limit, iter_limit,
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
void BgpShowHandler<ShowPathResolverSummaryReq,
    ShowPathResolverSummaryReqIterate, ShowPathResolverSummaryResp,
    ShowPathResolver>::FillShowList(ShowPathResolverSummaryResp *resp,
    const vector<ShowPathResolver> &show_list) {
    resp->set_resolvers(show_list);
}

//
// Handler for ShowPathResolverReq.
//
void ShowPathResolverReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ResolverNexthop");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowPathResolverReq,
        ShowPathResolverReqIterate,
        ShowPathResolverResp,
        ShowPathResolver>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowPathResolverReq,
        ShowPathResolverReqIterate,
        ShowPathResolverResp,
        ShowPathResolver>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowPathResolverReqIterate.
//
void ShowPathResolverReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ResolverNexthop");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowPathResolverReq,
        ShowPathResolverReqIterate,
        ShowPathResolverResp,
        ShowPathResolver>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowPathResolverReq,
        ShowPathResolverReqIterate,
        ShowPathResolverResp,
        ShowPathResolver>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowPathResolverSummaryReq.
//
void ShowPathResolverSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ResolverNexthop");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowPathResolverSummaryReq,
        ShowPathResolverSummaryReqIterate,
        ShowPathResolverSummaryResp,
        ShowPathResolver>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowPathResolverSummaryReq,
        ShowPathResolverSummaryReqIterate,
        ShowPathResolverSummaryResp,
        ShowPathResolver>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowPathResolverSummaryReqIterate.
//
void ShowPathResolverSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ResolverNexthop");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowPathResolverSummaryReq,
        ShowPathResolverSummaryReqIterate,
        ShowPathResolverSummaryResp,
        ShowPathResolver>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowPathResolverSummaryReq,
        ShowPathResolverSummaryReqIterate,
        ShowPathResolverSummaryResp,
        ShowPathResolver>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
