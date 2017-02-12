/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/regex.hpp>

#include "bgp/bgp_config.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_show_handler.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/service_chaining.h"
#include "bgp/routing-instance/service_chaining_types.h"
#include "net/address_util.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

static bool FillServiceChainInfo(Address::Family family,
                                 const string &search_string,
                                 const regex &search_expr,
                                 ShowServicechainInfo &info,
                                 RoutingInstance *rtinstance) {
    const BgpTable *table =
        static_cast<const BgpTable *>(rtinstance->GetTable(family));
    if (!table)
        return false;
    IServiceChainMgr *service_chain_mgr =
        rtinstance->server()->service_chain_mgr(family);
    if (!service_chain_mgr)
        return false;

    if ((!regex_search(table->name(), search_expr)) &&
        (search_string != "pending" ||
            !service_chain_mgr->IsPending(rtinstance)) &&
        (search_string != "deleted" || !table->IsDeleted())) {
        return false;
    }
    const BgpInstanceConfig *rtconfig = rtinstance->config();
    const ServiceChainConfig *sc_config = rtconfig->service_chain_info(family);
    if (!sc_config)
        return false;

    info.set_family(Address::FamilyToString(family));
    info.set_src_virtual_network(rtinstance->GetVirtualNetworkName());
    info.set_dest_virtual_network(GetVNFromRoutingInstance(
                                     sc_config->routing_instance));
    info.set_service_instance(sc_config->service_instance);
    info.set_src_rt_instance(rtinstance->name());
    info.set_dest_rt_instance(sc_config->routing_instance);
    if (sc_config->source_routing_instance.empty()) {
        info.set_connected_rt_instance(rtinstance->name());
    } else {
        info.set_connected_rt_instance(sc_config->source_routing_instance);
    }
    info.set_service_chain_addr(sc_config->service_chain_address);

    return service_chain_mgr->FillServiceChainInfo(rtinstance, &info);
}

// Specialization of BgpShowHandler<>::CallbackCommon.
template <>
bool BgpShowHandler<ShowServiceChainReq, ShowServiceChainReqIterate,
    ShowServiceChainResp, ShowServicechainInfo>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    uint32_t page_limit = bsc->page_limit() ? bsc->page_limit() : kPageLimit;
    uint32_t iter_limit = bsc->iter_limit() ? bsc->iter_limit() : kIterLimit;
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    regex search_expr(data->search_string);
    RoutingInstanceMgr::const_name_iterator it =
        rim->name_clower_bound(data->next_entry);
    for (uint32_t iter_count = 0; it != rim->name_cend(); ++it, ++iter_count) {
        RoutingInstance *rinstance = it->second;
        ShowServicechainInfo inet_info;
        if (FillServiceChainInfo(Address::INET, data->search_string,
                                 search_expr, inet_info, rinstance)) {
            data->show_list.push_back(inet_info);
        }
        ShowServicechainInfo inet6_info;
        if (FillServiceChainInfo(Address::INET6, data->search_string,
                                 search_expr, inet6_info, rinstance)) {
            data->show_list.push_back(inet6_info);
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
void BgpShowHandler<ShowServiceChainReq, ShowServiceChainReqIterate,
    ShowServiceChainResp, ShowServicechainInfo>::FillShowList(
        ShowServiceChainResp *resp,
        const vector<ShowServicechainInfo> &show_list) {
    resp->set_service_chain_list(show_list);
}

// Handler for ShowServiceChainReq.
void ShowServiceChainReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ServiceChain");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowServiceChainReq,
        ShowServiceChainReqIterate,
        ShowServiceChainResp,
        ShowServicechainInfo>::Callback, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowServiceChainReq,
        ShowServiceChainReqIterate,
        ShowServiceChainResp,
        ShowServicechainInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}

//
// Handler for ShowServiceChainReqIterate.
//
void ShowServiceChainReqIterate::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ServiceChain");
    s1.cbFn_ = boost::bind(&BgpShowHandler<
        ShowServiceChainReq,
        ShowServiceChainReqIterate,
        ShowServiceChainResp,
        ShowServicechainInfo>::CallbackIterate, _1, _2, _3, _4, _5);
    s1.allocFn_ = BgpShowHandler<
        ShowServiceChainReq,
        ShowServiceChainReqIterate,
        ShowServiceChainResp,
        ShowServicechainInfo>::CreateData;
    s1.instances_.push_back(0);
    ps.stages_.push_back(s1);
    RequestPipeline rp(ps);
}
