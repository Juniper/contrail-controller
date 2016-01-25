/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/foreach.hpp>

#include "bgp/bgp_server.h"
#include "bgp/bgp_show_handler.h"
#include "bgp/routing-policy/routing_policy.h"
#include "bgp/routing-policy/routing_policy_action.h"
#include "bgp/routing-policy/routing_policy_match.h"
#include "bgp/routing-policy/routing_policy_types.h"

using std::string;
using std::vector;

//
// Fill in information for a policy.
//
static void FillRoutingPolicyInfo(ShowRoutingPolicyInfo *srpi,
    const BgpSandeshContext *bsc, const RoutingPolicy *policy,
    bool summary) {
    srpi->set_name(policy->name());
    srpi->set_generation(policy->generation());
    srpi->set_ref_count(policy->refcount());
    srpi->set_deleted(policy->deleted());
    vector<PolicyTermInfo> term_list;
    BOOST_FOREACH(RoutingPolicy::PolicyTermPtr term, policy->terms()) {
        PolicyTermInfo show_term;
        show_term.set_terminal(term->terminal());
        vector<string> match_list;
        BOOST_FOREACH(RoutingPolicyMatch *match, term->matches()) {
            match_list.push_back(match->ToString());
        }
        show_term.set_matches(match_list);
        vector<string> action_list;
        BOOST_FOREACH(RoutingPolicyAction *action, term->actions()) {
            action_list.push_back(action->ToString());
        }
        show_term.set_actions(action_list);
        term_list.push_back(show_term);
    }
    srpi->set_terms(term_list);
}

//
// Fill in information for list of policies.
//
static bool FillRoutingPolicyInfoList(const BgpSandeshContext *bsc,
    bool summary, uint32_t page_limit, uint32_t iter_limit,
    const string &start_policy, const string &search_string,
    vector<ShowRoutingPolicyInfo> *srpi_list, string *next_policy) {
    RoutingPolicyMgr *rpm = bsc->bgp_server->routing_policy_mgr();
    RoutingPolicyMgr::const_name_iterator it =
        rpm->name_clower_bound(start_policy);
    for (uint32_t iter_count = 0; it != rpm->name_cend(); ++it, ++iter_count) {
        const RoutingPolicy *policy = it->second;
        if (!search_string.empty() &&
            (policy->name().find(search_string) == string::npos) &&
            (search_string != "deleted" || !policy->deleted())) {
            continue;
        }
        ShowRoutingPolicyInfo srpi;
        FillRoutingPolicyInfo(&srpi, bsc, policy, summary);
        srpi_list->push_back(srpi);
        if (srpi_list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all policies.
    if (it == rpm->name_cend() || ++it == rpm->name_end())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = srpi_list->size() >= page_limit;
    *next_policy = it->second->name();
    return done;
}

// Specialization of BgpShowHandler<>::CallbackCommon.
template <>
bool BgpShowHandler<ShowRoutingPolicyReq, ShowRoutingPolicyReqIterate,
    ShowRoutingPolicyResp, ShowRoutingPolicyInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    string next_policy;
    bool done = FillRoutingPolicyInfoList(bsc, false, page_limit, iter_limit,
        data->next_entry, data->search_string, &data->show_list,
        &next_policy);
    if (!next_policy.empty())
        SaveContextToData(next_policy, done, data);
    return done;
}

// Specialization of BgpShowHandler<>::FillShowList.
template <>
void BgpShowHandler<ShowRoutingPolicyReq, ShowRoutingPolicyReqIterate,
    ShowRoutingPolicyResp, ShowRoutingPolicyInfo>::FillShowList(
        ShowRoutingPolicyResp *resp,
        const vector<ShowRoutingPolicyInfo> &show_list) {
    resp->set_routing_policies(show_list);
}

// Handler for ShowRoutingPolicyReq.
void ShowRoutingPolicyReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingPolicyReq,
        ShowRoutingPolicyReqIterate,
        ShowRoutingPolicyResp,
        ShowRoutingPolicyInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingPolicyReq,
        ShowRoutingPolicyReqIterate,
        ShowRoutingPolicyResp,
        ShowRoutingPolicyInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowRoutingPolicyReqIterate.
//
void ShowRoutingPolicyReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowRoutingPolicyReq,
        ShowRoutingPolicyReqIterate,
        ShowRoutingPolicyResp,
        ShowRoutingPolicyInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowRoutingPolicyReq,
        ShowRoutingPolicyReqIterate,
        ShowRoutingPolicyResp,
        ShowRoutingPolicyInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
