/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/regex.hpp>

#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_server.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/routing-instance/routing_instance.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for an ermvpn table.
//
static void FillMulticastManagerInfo(ShowMulticastManager *smm,
    const BgpSandeshContext *bsc, const ErmVpnTable *table) {
    smm->set_name(table->name());
    const McastTreeManager *manager = table->GetTreeManager();
    if (!manager)
        return;

    smm->set_deleted(manager->deleted());
    smm->set_deleted_at(
        UTCUsecToString(manager->deleter()->delete_time_stamp_usecs()));

    uint32_t total_trees = 0;
    for (McastTreeManager::const_iterator it = manager->begin();
         it != manager->end(); ++it) {
        const McastManagerPartition *partition = *it;
        total_trees += partition->size();
    }
    smm->set_total_trees(total_trees);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowMulticastManagerReq, ShowMulticastManagerReqIterate,
    ShowMulticastManagerResp, ShowMulticastManager>::CallbackCommon(
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
        ShowMulticastManager smm;
        FillMulticastManagerInfo(&smm, bsc, table);
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
void BgpShowHandler<ShowMulticastManagerReq, ShowMulticastManagerReqIterate,
    ShowMulticastManagerResp, ShowMulticastManager>::FillShowList(
    ShowMulticastManagerResp *resp,
    const vector<ShowMulticastManager> &show_list) {
    resp->set_managers(show_list);
}

//
// Handler for ShowMulticastManagerReq.
//
void ShowMulticastManagerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowMulticastManagerReq,
        ShowMulticastManagerReqIterate,
        ShowMulticastManagerResp,
        ShowMulticastManager>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowMulticastManagerReq,
        ShowMulticastManagerReqIterate,
        ShowMulticastManagerResp,
        ShowMulticastManager>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowMulticastManagerReqIterate.
//
void ShowMulticastManagerReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowMulticastManagerReq,
        ShowMulticastManagerReqIterate,
        ShowMulticastManagerResp,
        ShowMulticastManager>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowMulticastManagerReq,
        ShowMulticastManagerReqIterate,
        ShowMulticastManagerResp,
        ShowMulticastManager>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
