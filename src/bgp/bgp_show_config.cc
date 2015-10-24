/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_config.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"

using boost::assign::list_of;
using std::string;
using std::vector;

//
// Fill in information for an instance.
//
static void FillBgpInstanceConfigInfo(ShowBgpInstanceConfig *sbic,
    const BgpSandeshContext *bsc, const BgpInstanceConfig *instance) {
    sbic->set_name(instance->name());
    sbic->set_virtual_network(instance->virtual_network());
    sbic->set_virtual_network_index(instance->virtual_network_index());
    sbic->set_vxlan_id(instance->vxlan_id());

    vector<string> import_list;
    BOOST_FOREACH(std::string rt, instance->import_list()) {
        import_list.push_back(rt);
    }
    sbic->set_import_target(import_list);
    vector<string> export_list;
    BOOST_FOREACH(std::string rt, instance->export_list()) {
        export_list.push_back(rt);
    }
    sbic->set_export_target(export_list);
    sbic->set_has_pnf(instance->has_pnf());
    sbic->set_last_change_at(UTCUsecToString(instance->last_change_at()));

    vector<ShowBgpServiceChainConfig> sbscc_list;
    BOOST_FOREACH(const ServiceChainConfig &sc_config,
        instance->service_chain_list()) {
        ShowBgpServiceChainConfig sbscc;
        sbscc.set_family(Address::FamilyToString(sc_config.family));
        sbscc.set_routing_instance(sc_config.routing_instance);
        sbscc.set_service_instance(sc_config.service_instance);
        sbscc.set_chain_address(sc_config.service_chain_address);
        sbscc.set_prefixes(sc_config.prefix);
        sbscc_list.push_back(sbscc);
    }
    sbic->set_service_chain_infos(sbscc_list);

    vector<ShowBgpStaticRouteConfig> static_route_list;
    vector<Address::Family> families = list_of(Address::INET)(Address::INET6);
    BOOST_FOREACH(Address::Family family, families) {
        BOOST_FOREACH(const StaticRouteConfig &static_rt_config,
            instance->static_routes(family)) {
            ShowBgpStaticRouteConfig sbsrc;
            string prefix = static_rt_config.address.to_string() + "/";
            prefix += integerToString(static_rt_config.prefix_length);
            sbsrc.set_prefix(prefix);
            sbsrc.set_targets(static_rt_config.route_target);
            sbsrc.set_nexthop(static_rt_config.nexthop.to_string());
            static_route_list.push_back(sbsrc);
        }
    }
    if (!static_route_list.empty())
        sbic->set_static_routes(static_route_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowBgpInstanceConfigReq, ShowBgpInstanceConfigReqIterate,
    ShowBgpInstanceConfigResp, ShowBgpInstanceConfig>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    const BgpConfigManager *bcm = bsc->bgp_server->config_manager();

    BgpConfigManager::InstanceMapRange range =
        bcm->InstanceMapItems(data->next_entry);
    BgpConfigManager::InstanceMap::const_iterator it = range.first;
    BgpConfigManager::InstanceMap::const_iterator it_end = range.second;
    for (uint32_t iter_count = 0; it != it_end; ++it, ++iter_count) {
        const BgpInstanceConfig *instance = it->second;
        if (!data->search_string.empty() &&
            (instance->name().find(data->search_string) == string::npos)) {
            continue;
        }

        ShowBgpInstanceConfig sbic;
        FillBgpInstanceConfigInfo(&sbic, bsc, instance);
        data->show_list.push_back(sbic);
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
void BgpShowHandler<ShowBgpInstanceConfigReq, ShowBgpInstanceConfigReqIterate,
    ShowBgpInstanceConfigResp, ShowBgpInstanceConfig>::FillShowList(
        ShowBgpInstanceConfigResp *resp,
        const vector<ShowBgpInstanceConfig> &show_list) {
    resp->set_instances(show_list);
}

//
// Handler for ShowBgpInstanceConfigReq.
//
void ShowBgpInstanceConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpInstanceConfigReq,
        ShowBgpInstanceConfigReqIterate,
        ShowBgpInstanceConfigResp,
        ShowBgpInstanceConfig>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpInstanceConfigReq,
        ShowBgpInstanceConfigReqIterate,
        ShowBgpInstanceConfigResp,
        ShowBgpInstanceConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpInstanceConfigReqIterate.
//
void ShowBgpInstanceConfigReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpInstanceConfigReq,
        ShowBgpInstanceConfigReqIterate,
        ShowBgpInstanceConfigResp,
        ShowBgpInstanceConfig>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpInstanceConfigReq,
        ShowBgpInstanceConfigReqIterate,
        ShowBgpInstanceConfigResp,
        ShowBgpInstanceConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
