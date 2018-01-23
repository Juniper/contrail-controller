/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/foreach.hpp>

#include <base/regex.h>
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/routing_instance.h"

using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using std::string;
using std::vector;

//
// Fill in information for an ermvpn table.
//
static void FillMvpnProjectManagerInfo(ShowMvpnProjectManager *smm,
    const BgpSandeshContext *bsc, const ErmVpnTable *table) {
    smm->set_name(table->name());
    const MvpnProjectManager *manager = table->mvpn_project_manager();
    if (!manager)
        return;

    size_t total_sg_states = 0;
    BOOST_FOREACH(MvpnProjectManagerPartition *pm_partition,
                  manager->partitions()) {
        total_sg_states += pm_partition->states().size();
    }
    smm->set_total_sg_states(total_sg_states);
    smm->set_total_managers(table->routing_instance()->manager()->
            GetMvpnProjectManagerCount(table->routing_instance()->name()));
    smm->set_deleted(manager->deleted());
    smm->set_deleted_at(
        UTCUsecToString(manager->deleter()->delete_time_stamp_usecs()));
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowMvpnProjectManagerReq, ShowMvpnProjectManagerReqIterate,
    ShowMvpnProjectManagerResp, ShowMvpnProjectManager>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    regex search_expr(data->search_string);
    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(data->next_entry);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        const RoutingInstance *rtinstance = it->second;
        const ErmVpnTable *table = static_cast<const ErmVpnTable *>(
            rtinstance->GetTable(Address::ERMVPN));
        if (!table)
            continue;
        if ((!regex_search(table->name(), search_expr)) &&
            (data->search_string != "deleted" || !table->IsDeleted())) {
            continue;
        }
        ShowMvpnProjectManager smm;
        FillMvpnProjectManagerInfo(&smm, bsc, table);
        data->show_list.push_back(smm);
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

//
// Specialization of BgpShowHandler<>::FillShowList.
//
template <>
void BgpShowHandler<ShowMvpnProjectManagerReq, ShowMvpnProjectManagerReqIterate,
    ShowMvpnProjectManagerResp, ShowMvpnProjectManager>::FillShowList(
    ShowMvpnProjectManagerResp *resp,
    const vector<ShowMvpnProjectManager> &show_list) {
    resp->set_managers(show_list);
}

//
// Handler for ShowMvpnProjectManagerReq.
//
void ShowMvpnProjectManagerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowMvpnProjectManagerReq,
        ShowMvpnProjectManagerReqIterate,
        ShowMvpnProjectManagerResp,
        ShowMvpnProjectManager>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowMvpnProjectManagerReq,
        ShowMvpnProjectManagerReqIterate,
        ShowMvpnProjectManagerResp,
        ShowMvpnProjectManager>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowMvpnProjectManagerReqIterate.
//
void ShowMvpnProjectManagerReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowMvpnProjectManagerReq,
        ShowMvpnProjectManagerReqIterate,
        ShowMvpnProjectManagerResp,
        ShowMvpnProjectManager>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowMvpnProjectManagerReq,
        ShowMvpnProjectManagerReqIterate,
        ShowMvpnProjectManagerResp,
        ShowMvpnProjectManager>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
