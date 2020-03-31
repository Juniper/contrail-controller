/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_show_handler.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/regex.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"

using boost::assign::list_of;
using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
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
    sbic->set_allow_transit(instance->virtual_network_allow_transit());
    sbic->set_pbb_evpn_enable(instance->virtual_network_pbb_evpn_enable());
    sbic->set_last_change_at(UTCUsecToString(instance->last_change_at()));

    vector<string> neighbors(
        instance->neighbor_list().begin(), instance->neighbor_list().end());
    sbic->set_neighbors(neighbors);

    vector<ShowBgpServiceChainConfig> sbscc_list;
    BOOST_FOREACH(const ServiceChainConfig &sc_config,
        instance->service_chain_list()) {
        ShowBgpServiceChainConfig sbscc;
        Address::Family family =
            SCAddress::SCFamilyToAddressFamily(sc_config.family);
        sbscc.set_family(Address::FamilyToString(family));
        sbscc.set_routing_instance(sc_config.routing_instance);
        sbscc.set_service_instance(sc_config.service_instance);
        sbscc.set_chain_address(sc_config.service_chain_address);
        sbscc.set_prefixes(sc_config.prefix);
        sbscc_list.push_back(sbscc);
    }
    sbic->set_service_chain_infos(sbscc_list);

    vector<ShowBgpStaticRouteConfig> static_route_list;
    vector<Address::Family> families = {Address::INET, Address::INET6};
    BOOST_FOREACH(Address::Family family, families) {
        BOOST_FOREACH(const StaticRouteConfig &static_rt_config,
            instance->static_routes(family)) {
            ShowBgpStaticRouteConfig sbsrc;
            string prefix = static_rt_config.address.to_string() + "/";
            prefix += integerToString(static_rt_config.prefix_length);
            sbsrc.set_prefix(prefix);
            sbsrc.set_nexthop(static_rt_config.nexthop.to_string());
            sbsrc.set_targets(static_rt_config.communities);
            sbsrc.set_targets(static_rt_config.route_targets);
            static_route_list.push_back(sbsrc);
        }
    }
    if (!static_route_list.empty())
        sbic->set_static_routes(static_route_list);

    vector<ShowBgpRouteAggregateConfig> aggregate_route_list;
    BOOST_FOREACH(Address::Family family, families) {
        BOOST_FOREACH(const AggregateRouteConfig &aggregate_rt_config,
            instance->aggregate_routes(family)) {
            ShowBgpRouteAggregateConfig sbarc;
            string prefix = aggregate_rt_config.aggregate.to_string() + "/";
            prefix += integerToString(aggregate_rt_config.prefix_length);
            sbarc.set_prefix(prefix);
            sbarc.set_nexthop(aggregate_rt_config.nexthop.to_string());
            aggregate_route_list.push_back(sbarc);
        }
    }
    if (!aggregate_route_list.empty())
        sbic->set_aggregate_routes(aggregate_route_list);

    vector<ShowBgpInstanceRoutingPolicyConfig> routing_policy_list;
    BOOST_FOREACH(const RoutingPolicyAttachInfo &policy_config,
                  instance->routing_policy_list()) {
        ShowBgpInstanceRoutingPolicyConfig sbirpc;
        sbirpc.set_policy_name(policy_config.routing_policy_);
        sbirpc.set_sequence(policy_config.sequence_);
        routing_policy_list.push_back(sbirpc);
    }
    if (!routing_policy_list.empty())
        sbic->set_routing_policies(routing_policy_list);
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

    regex search_expr(data->search_string);
    BgpConfigManager::InstanceMapRange range =
        bcm->InstanceMapItems(data->next_entry);
    BgpConfigManager::InstanceMap::const_iterator it = range.first;
    BgpConfigManager::InstanceMap::const_iterator it_end = range.second;
    for (uint32_t iter_count = 0; it != it_end; ++it, ++iter_count) {
        const BgpInstanceConfig *instance = it->second;
        if (!regex_search(instance->name(), search_expr))
            continue;
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

//
// Fill in information for an routing policy.
//
static void FillBgpRoutingPolicyInfo(ShowBgpRoutingPolicyConfig *sbrpc,
    const BgpSandeshContext *bsc, const BgpRoutingPolicyConfig *policy) {
    sbrpc->set_name(policy->name());
    std::vector<ShowBgpRoutingPolicyTermConfig> terms_list;
    BOOST_FOREACH(const RoutingPolicyTermConfig &term, policy->terms()) {
        ShowBgpRoutingPolicyTermConfig sbrptc;
        sbrptc.set_match(term.match.ToString());
        sbrptc.set_action(term.action.ToString());
        terms_list.push_back(sbrptc);
    }
    sbrpc->set_terms(terms_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowBgpRoutingPolicyConfigReq,
     ShowBgpRoutingPolicyConfigReqIterate, ShowBgpRoutingPolicyConfigResp,
     ShowBgpRoutingPolicyConfig>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    const BgpConfigManager *bcm = bsc->bgp_server->config_manager();

    regex search_expr(data->search_string);
    BgpConfigManager::RoutingPolicyMapRange range =
        bcm->RoutingPolicyMapItems(data->next_entry);
    BgpConfigManager::RoutingPolicyMap::const_iterator it = range.first;
    BgpConfigManager::RoutingPolicyMap::const_iterator it_end = range.second;
    for (uint32_t iter_count = 0; it != it_end; ++it, ++iter_count) {
        const BgpRoutingPolicyConfig *policy = it->second;
        if (!regex_search(policy->name(), search_expr))
            continue;
        ShowBgpRoutingPolicyConfig sbrpc;
        FillBgpRoutingPolicyInfo(&sbrpc, bsc, policy);
        data->show_list.push_back(sbrpc);
        if (data->show_list.size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all policies.
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
void BgpShowHandler<ShowBgpRoutingPolicyConfigReq,
     ShowBgpRoutingPolicyConfigReqIterate, ShowBgpRoutingPolicyConfigResp,
     ShowBgpRoutingPolicyConfig>::FillShowList(
        ShowBgpRoutingPolicyConfigResp *resp,
        const vector<ShowBgpRoutingPolicyConfig> &show_list) {
    resp->set_routing_policies(show_list);
}


//
// Handler for ShowBgpRoutingPolicyConfigReq.
//
void ShowBgpRoutingPolicyConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpRoutingPolicyConfigReq,
        ShowBgpRoutingPolicyConfigReqIterate,
        ShowBgpRoutingPolicyConfigResp,
        ShowBgpRoutingPolicyConfig>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpRoutingPolicyConfigReq,
        ShowBgpRoutingPolicyConfigReqIterate,
        ShowBgpRoutingPolicyConfigResp,
        ShowBgpRoutingPolicyConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpRoutingPolicyConfigReqIterate.
//
void ShowBgpRoutingPolicyConfigReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpRoutingPolicyConfigReq,
        ShowBgpRoutingPolicyConfigReqIterate,
        ShowBgpRoutingPolicyConfigResp,
        ShowBgpRoutingPolicyConfig>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpRoutingPolicyConfigReq,
        ShowBgpRoutingPolicyConfigReqIterate,
        ShowBgpRoutingPolicyConfigResp,
        ShowBgpRoutingPolicyConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Fill in information for a neighbor.
//
static void FillBgpNeighborConfigInfo(ShowBgpNeighborConfig *sbnc,
    const BgpSandeshContext *bsc, const BgpNeighborConfig *neighbor) {
    sbnc->set_instance_name(neighbor->instance_name());
    sbnc->set_name(neighbor->name());
    sbnc->set_admin_down(neighbor->admin_down());
    sbnc->set_passive(neighbor->passive());
    sbnc->set_as_override(neighbor->as_override());
    const BgpNeighborConfig::OriginOverrideConfig &route_origin =
                neighbor->origin_override();
    sbnc->set_origin_override(route_origin.origin_override);
    if (route_origin.origin_override) {
        sbnc->set_route_origin(route_origin.origin);
    } else {
        sbnc->set_route_origin("-");
    }
    sbnc->set_private_as_action(neighbor->private_as_action());
    sbnc->set_router_type(neighbor->router_type());
    sbnc->set_local_identifier(neighbor->local_identifier_string());
    sbnc->set_local_as(neighbor->local_as());
    sbnc->set_autonomous_system(neighbor->peer_as());
    sbnc->set_identifier(neighbor->peer_identifier_string());
    sbnc->set_address(neighbor->peer_address().to_string());
    sbnc->set_cluster_id(Ip4Address(neighbor->cluster_id()).to_string());
    sbnc->set_source_port(neighbor->source_port());
    sbnc->set_address_families(neighbor->GetAddressFamilies());
    sbnc->set_hold_time(neighbor->hold_time());
    sbnc->set_loop_count(neighbor->loop_count());
    sbnc->set_last_change_at(UTCUsecToString(neighbor->last_change_at()));
    sbnc->set_auth_type(neighbor->auth_data().KeyTypeToString());
    if (bsc->test_mode()) {
        sbnc->set_auth_keys(neighbor->auth_data().KeysToStringDetail());
    }

    vector<ShowBgpNeighborFamilyConfig> sbnfc_list;
    BOOST_FOREACH(const BgpFamilyAttributesConfig family_config,
        neighbor->family_attributes_list()) {
        ShowBgpNeighborFamilyConfig sbnfc;
        sbnfc.set_family(family_config.family);
        sbnfc.set_loop_count(family_config.loop_count);
        sbnfc.set_prefix_limit(family_config.prefix_limit);
        sbnfc.set_idle_timeout(family_config.idle_timeout);
        sbnfc.set_default_tunnel_encap_list(
            family_config.default_tunnel_encap_list);
        if (family_config.family == "inet") {
            IpAddress address = neighbor->gateway_address(Address::INET);
            if (!address.is_unspecified())
                sbnfc.set_gateway_address(address.to_string());
        } else if (family_config.family == "inet6") {
            IpAddress address = neighbor->gateway_address(Address::INET6);
            if (!address.is_unspecified())
                sbnfc.set_gateway_address(address.to_string());
        }
        sbnfc_list.push_back(sbnfc);
    }
    sbnc->set_family_attributes_list(sbnfc_list);
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowBgpNeighborConfigReq, ShowBgpNeighborConfigReqIterate,
    ShowBgpNeighborConfigResp, ShowBgpNeighborConfig>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    const BgpConfigManager *bcm = bsc->bgp_server->config_manager();

    regex search_expr(data->search_string);
    BgpConfigManager::InstanceMapRange range =
        bcm->InstanceMapItems(data->next_entry);
    BgpConfigManager::InstanceMap::const_iterator it = range.first;
    BgpConfigManager::InstanceMap::const_iterator it_end = range.second;
    for (uint32_t iter_count = 0; it != it_end; ++it, ++iter_count) {
        const BgpInstanceConfig *instance = it->second;
        BOOST_FOREACH(BgpConfigManager::NeighborMap::value_type value,
            bcm->NeighborMapItems(instance->name())) {
            const BgpNeighborConfig *neighbor = value.second;
            if (!regex_search(neighbor->name(), search_expr))
                continue;
            ShowBgpNeighborConfig sbnc;
            FillBgpNeighborConfigInfo(&sbnc, bsc, neighbor);
            data->show_list.push_back(sbnc);
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
void BgpShowHandler<ShowBgpNeighborConfigReq, ShowBgpNeighborConfigReqIterate,
    ShowBgpNeighborConfigResp, ShowBgpNeighborConfig>::FillShowList(
        ShowBgpNeighborConfigResp *resp,
        const vector<ShowBgpNeighborConfig> &show_list) {
    resp->set_neighbors(show_list);
}

//
// Handler for ShowBgpNeighborConfigReq.
//
void ShowBgpNeighborConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpNeighborConfigReq,
        ShowBgpNeighborConfigReqIterate,
        ShowBgpNeighborConfigResp,
        ShowBgpNeighborConfig>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpNeighborConfigReq,
        ShowBgpNeighborConfigReqIterate,
        ShowBgpNeighborConfigResp,
        ShowBgpNeighborConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpNeighborConfigReqIterate.
//
void ShowBgpNeighborConfigReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpNeighborConfigReq,
        ShowBgpNeighborConfigReqIterate,
        ShowBgpNeighborConfigResp,
        ShowBgpNeighborConfig>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowBgpNeighborConfigReq,
        ShowBgpNeighborConfigReqIterate,
        ShowBgpNeighborConfigResp,
        ShowBgpNeighborConfig>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpPeeringConfigReq.
//
void ShowBgpPeeringConfigReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());
    bsc->PeeringShowReqHandler(this);
}

//
// Handler for ShowBgpPeeringConfigReqIterate.
//
void ShowBgpPeeringConfigReqIterate::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());
    bsc->PeeringShowReqIterateHandler(this);
}

//
// Fill in information for an instance.
//
static void FillBgpGlobalSystemConfigInfo(ShowBgpGlobalSystemConfig *sbgc,
    const BgpSandeshContext *bsc, const BgpGlobalSystemConfig *bgsc) {

    sbgc->set_gr_time(bgsc->gr_time());
    sbgc->set_llgr_time(bgsc->llgr_time());
    sbgc->set_last_change_at(bgsc->last_change_at());
    sbgc->set_end_of_rib_timeout(bgsc->end_of_rib_timeout());
    sbgc->set_gr_bgp_helper(bgsc->gr_bgp_helper());
    sbgc->set_gr_xmpp_helper(bgsc->gr_xmpp_helper());
    sbgc->set_gr_enable(bgsc->gr_enable());
    sbgc->set_always_compare_med(bgsc->always_compare_med());
    sbgc->set_rd_cluster_seed(bgsc->rd_cluster_seed());
    sbgc->set_bgpaas_port_start(bgsc->bgpaas_port_start());
    sbgc->set_bgpaas_port_end(bgsc->bgpaas_port_end());
}

//
//Specialization of BgpShowHandler<>::ConvertReqToData
//
template <>
void BgpShowHandler<ShowBgpGlobalSystemConfigReq,
                    ShowBgpGlobalSystemConfigReqIterate,
                    ShowBgpGlobalSystemConfigResp,
                    ShowBgpGlobalSystemConfig>::ConvertReqToData(
                      const ShowBgpGlobalSystemConfigReq *req, Data *data) {

    data->initialized = true;
}

//
// Specialization of BgpShowHandler<>::CallbackCommon.
//
template <>
bool BgpShowHandler<ShowBgpGlobalSystemConfigReq,
                    ShowBgpGlobalSystemConfigReqIterate,
                    ShowBgpGlobalSystemConfigResp,
                    ShowBgpGlobalSystemConfig>::CallbackCommon(
                            const BgpSandeshContext *bsc, Data *data) {

   const  BgpGlobalSystemConfig *bgsc = bsc->bgp_server->global_config();

   ShowBgpGlobalSystemConfig sbgc;
   FillBgpGlobalSystemConfigInfo(&sbgc, bsc, bgsc);
   data->show_list.push_back(sbgc);

   return true;
}

//
// Specialization of BgpShowHandler<>::FillShowList.
//
template <>
void BgpShowHandler<ShowBgpGlobalSystemConfigReq,
                    ShowBgpGlobalSystemConfigReqIterate,
                    ShowBgpGlobalSystemConfigResp,
                    ShowBgpGlobalSystemConfig>::FillShowList(
                    ShowBgpGlobalSystemConfigResp *resp,
                    const vector<ShowBgpGlobalSystemConfig> &show_list) {
    resp->set_global_instances(show_list);
}

//
// Handler for ShowBgpGlobalSystemConfigReq.
//
void ShowBgpGlobalSystemConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpGlobalSystemConfigReq,
        ShowBgpGlobalSystemConfigReqIterate,
        ShowBgpGlobalSystemConfigResp,
        ShowBgpGlobalSystemConfig>::Callback, _1, _2, _3, _4, _5);

    s1.allocFn_ = BgpShowHandler<
        ShowBgpGlobalSystemConfigReq,
        ShowBgpGlobalSystemConfigReqIterate,
        ShowBgpGlobalSystemConfigResp,
        ShowBgpGlobalSystemConfig>::CreateData;

    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowBgpGlobalSystemConfigReqIterate.
//
void ShowBgpGlobalSystemConfigReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowBgpGlobalSystemConfigReq,
        ShowBgpGlobalSystemConfigReqIterate,
        ShowBgpGlobalSystemConfigResp,
        ShowBgpGlobalSystemConfig>::CallbackIterate, _1, _2, _3, _4, _5);

    s1.allocFn_ = BgpShowHandler<
        ShowBgpGlobalSystemConfigReq,
        ShowBgpGlobalSystemConfigReqIterate,
        ShowBgpGlobalSystemConfigResp,
        ShowBgpGlobalSystemConfig>::CreateData;

    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
