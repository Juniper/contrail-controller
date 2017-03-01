/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/regex.hpp>

#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for list of rtarget groups.
//
// Allows regular, summary and peer introspect to share code.
// Assumes that search_string is the peer name if match_peer is true.
//
static bool FillRtGroupInfoList(const BgpSandeshContext *bsc,
    bool summary, bool match_peer, uint32_t page_limit, uint32_t iter_limit,
    const string &start_rtarget_str, const string &search_string,
    vector<ShowRtGroupInfo> *srtg_list, string *next_rtarget_str) {
    RouteTarget rtarget;

    // Bail if start_rtarget_str is bad.
    if (!start_rtarget_str.empty()) {
        rtarget = RouteTarget::FromString(start_rtarget_str);
        if (rtarget.IsNull())
            return true;
    }

    // Bail if there's no peer specified when doing a peer introspect.
    if (match_peer && search_string.empty())
        return true;

    regex search_expr(search_string);
    const RTargetGroupMgr *rtgroup_mgr = bsc->bgp_server->rtarget_group_mgr();
    RTargetGroupMgr::const_iterator it = rtgroup_mgr->lower_bound(rtarget);
    for (uint32_t iter_count = 0; it != rtgroup_mgr->end();
         ++it, ++iter_count) {
        const RtGroup *rtgroup = it->second;
        if (!match_peer && !regex_search(rtgroup->ToString(), search_expr))
            continue;
        ShowRtGroupInfo srtg;
        if (match_peer) {
            if (!rtgroup->HasInterestedPeer(search_string))
                continue;
            rtgroup->FillShowPeerInfo(&srtg);
        } else if (summary) {
            rtgroup->FillShowSummaryInfo(&srtg);
        } else {
            rtgroup->FillShowInfo(&srtg);
        }
        srtg_list->push_back(srtg);
        if (srtg_list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all rtarget groups.
    if (it == rtgroup_mgr->end() || ++it == rtgroup_mgr->end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = srtg_list->size() >= page_limit;
    *next_rtarget_str = it->second->ToString();
    return done;
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for regular introspect.
//
template <>
bool BgpShowHandler<ShowRtGroupReq, ShowRtGroupReqIterate,
    ShowRtGroupResp, ShowRtGroupInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_rtarget_str;
    bool done = FillRtGroupInfoList(bsc, false, false, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_rtarget_str);
    if (!next_rtarget_str.empty())
        SaveContextToData(next_rtarget_str, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for regular introspect.
//
template <>
void BgpShowHandler<ShowRtGroupReq, ShowRtGroupReqIterate,
    ShowRtGroupResp, ShowRtGroupInfo>::FillShowList(
    ShowRtGroupResp *resp,
    const vector<ShowRtGroupInfo> &show_list) {
    resp->set_rtgroup_list(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for summary introspect.
//
template <>
bool BgpShowHandler<ShowRtGroupSummaryReq, ShowRtGroupSummaryReqIterate,
    ShowRtGroupSummaryResp, ShowRtGroupInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_rtarget_str;
    bool done = FillRtGroupInfoList(bsc, true, false, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_rtarget_str);
    if (!next_rtarget_str.empty())
        SaveContextToData(next_rtarget_str, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for summary introspect.
//
template <>
void BgpShowHandler<ShowRtGroupSummaryReq, ShowRtGroupSummaryReqIterate,
    ShowRtGroupSummaryResp, ShowRtGroupInfo>::FillShowList(
    ShowRtGroupSummaryResp *resp,
    const vector<ShowRtGroupInfo> &show_list) {
    resp->set_rtgroup_list(show_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon for peer introspect.
//
template <>
bool BgpShowHandler<ShowRtGroupPeerReq, ShowRtGroupPeerReqIterate,
    ShowRtGroupPeerResp, ShowRtGroupInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_rtarget_str;
    bool done = FillRtGroupInfoList(bsc, false, true, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_rtarget_str);
    if (!next_rtarget_str.empty())
        SaveContextToData(next_rtarget_str, done, data);
    return done;
}

//
// Specialization of BgpShowHandler<>::FillShowList for peer introspect.
//
template <>
void BgpShowHandler<ShowRtGroupPeerReq, ShowRtGroupPeerReqIterate,
    ShowRtGroupPeerResp, ShowRtGroupInfo>::FillShowList(
    ShowRtGroupPeerResp *resp,
    const vector<ShowRtGroupInfo> &show_list) {
    resp->set_rtgroup_list(show_list);
}

//
// Handler for ShowRtGroupReq.
//
void ShowRtGroupReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupReq,
        ShowRtGroupReqIterate,
        ShowRtGroupResp,
        ShowRtGroupInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupReq,
        ShowRtGroupReqIterate,
        ShowRtGroupResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRtGroupReqIterate.
//
void ShowRtGroupReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupReq,
        ShowRtGroupReqIterate,
        ShowRtGroupResp,
        ShowRtGroupInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupReq,
        ShowRtGroupReqIterate,
        ShowRtGroupResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRtGroupSummaryReq.
//
void ShowRtGroupSummaryReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupSummaryReq,
        ShowRtGroupSummaryReqIterate,
        ShowRtGroupSummaryResp,
        ShowRtGroupInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupSummaryReq,
        ShowRtGroupSummaryReqIterate,
        ShowRtGroupSummaryResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRtGroupSummaryReqIterate.
//
void ShowRtGroupSummaryReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupSummaryReq,
        ShowRtGroupSummaryReqIterate,
        ShowRtGroupSummaryResp,
        ShowRtGroupInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupSummaryReq,
        ShowRtGroupSummaryReqIterate,
        ShowRtGroupSummaryResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRtGroupPeerReq.
//
void ShowRtGroupPeerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupPeerReq,
        ShowRtGroupPeerReqIterate,
        ShowRtGroupPeerResp,
        ShowRtGroupInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupPeerReq,
        ShowRtGroupPeerReqIterate,
        ShowRtGroupPeerResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRtGroupPeerReqIterate.
//
void ShowRtGroupPeerReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::RTFilter");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRtGroupPeerReq,
        ShowRtGroupPeerReqIterate,
        ShowRtGroupPeerResp,
        ShowRtGroupInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRtGroupPeerReq,
        ShowRtGroupPeerReqIterate,
        ShowRtGroupPeerResp,
        ShowRtGroupInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
