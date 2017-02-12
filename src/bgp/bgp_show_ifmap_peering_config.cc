/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/foreach.hpp>
#include <boost/regex.hpp>

#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "schema/bgp_schema_types.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

//
// Fill in information for a peering.
//
static void FillBgpPeeringConfigInfo(ShowBgpPeeringConfig *sbpc,
    const BgpSandeshContext *bsc, const BgpIfmapPeeringConfig *peering) {
    sbpc->set_instance_name(peering->instance()->name());
    sbpc->set_name(peering->name());
    sbpc->set_neighbor_count(peering->size());
    if (peering->bgp_peering()) {
        vector<ShowBgpSessionConfig> sbsc_list;
        for (vector<autogen::BgpSession>::const_iterator sit =
             peering->bgp_peering()->data().begin();
             sit != peering->bgp_peering()->data().end(); ++sit) {
            ShowBgpSessionConfig sbsc;
            sbsc.set_uuid(sit->uuid);
            vector<ShowBgpSessionAttributesConfig> sbsac_list;
            for (vector<autogen::BgpSessionAttributes>::const_iterator ait =
                 sit->attributes.begin(); ait != sit->attributes.end(); ++ait) {
                ShowBgpSessionAttributesConfig sbsac;
                sbsac.set_bgp_router(ait->bgp_router);
                sbsac.set_address_families(ait->address_families.family);
                sbsac_list.push_back(sbsac);
            }
            sbsc.set_attributes(sbsac_list);
            sbsc_list.push_back(sbsc);
        }
        sbpc->set_sessions(sbsc_list);
    }
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowBgpPeeringConfigReq, ShowBgpPeeringConfigReqIterate,
    ShowBgpPeeringConfigResp, ShowBgpPeeringConfig>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    const BgpIfmapConfigManager *bcm = dynamic_cast<BgpIfmapConfigManager *>(
        bsc->bgp_server->config_manager());
    if (!bcm)
        return true;

    regex search_expr(data->search_string);
    BgpConfigManager::InstanceMapRange range =
        bcm->InstanceMapItems(data->next_entry);
    BgpConfigManager::InstanceMap::const_iterator it = range.first;
    BgpConfigManager::InstanceMap::const_iterator it_end = range.second;
    for (uint32_t iter_count = 0; it != it_end; ++it, ++iter_count) {
        const BgpIfmapInstanceConfig *instance =
            bcm->config()->FindInstance(it->first);
        if (!instance)
            continue;
        BOOST_FOREACH(BgpIfmapInstanceConfig::PeeringMap::value_type value,
            instance->peerings()) {
            const BgpIfmapPeeringConfig *peering = value.second;
            if (!regex_search(peering->name(), search_expr))
                continue;
            ShowBgpPeeringConfig sbpc;
            FillBgpPeeringConfigInfo(&sbpc, bsc, peering);
            data->show_list.push_back(sbpc);
        }
        if (data->show_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all instances.
    if (it == it_end || ++it == it_end)
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
void BgpShowHandler<ShowBgpPeeringConfigReq, ShowBgpPeeringConfigReqIterate,
    ShowBgpPeeringConfigResp, ShowBgpPeeringConfig>::FillShowList(
        ShowBgpPeeringConfigResp *resp,
        const vector<ShowBgpPeeringConfig> &show_list) {
    resp->set_peerings(show_list);
}

//
// Handler for ShowBgpPeeringConfigReq.
// Called via BgpSandeshContext::PeeringShowReqHandler.
//
void ShowBgpIfmapPeeringConfigReqHandler(const BgpSandeshContext *bsc,
    const ShowBgpPeeringConfigReq *req) {
    RequestPipeline::PipeSpec ps(req);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpPeeringConfigReq,
        ShowBgpPeeringConfigReqIterate,
        ShowBgpPeeringConfigResp,
        ShowBgpPeeringConfig>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpPeeringConfigReq,
        ShowBgpPeeringConfigReqIterate,
        ShowBgpPeeringConfigResp,
        ShowBgpPeeringConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpPeeringConfigReqIterate.
// Called via BgpSandeshContext::PeeringShowReqIterateHandler.
//
void ShowBgpIfmapPeeringConfigReqIterateHandler(const BgpSandeshContext *bsc,
    const ShowBgpPeeringConfigReqIterate *req_iterate) {
    RequestPipeline::PipeSpec ps(req_iterate);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpPeeringConfigReq,
        ShowBgpPeeringConfigReqIterate,
        ShowBgpPeeringConfigResp,
        ShowBgpPeeringConfig>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpPeeringConfigReq,
        ShowBgpPeeringConfigReqIterate,
        ShowBgpPeeringConfigResp,
        ShowBgpPeeringConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
