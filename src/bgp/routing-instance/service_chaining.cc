/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/service_chaining.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/queue_task.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "db/db_table_partition.h"
#include "net/address.h"

using boost::system::error_code;

int ServiceChainMgr::service_chain_task_id_ = -1;

ServiceChain::ServiceChain(RoutingInstance *src, RoutingInstance *dest, 
                           RoutingInstance *connected,
                           const std::vector<std::string> &subnets, 
                           IpAddress addr) 
    : src_(src), dest_(dest), connected_(connected), connected_route_(NULL),
    service_chain_addr_(addr), connected_table_unregistered_(false),
    dest_table_unregistered_(false), 
    aggregate_(false), src_table_delete_ref_(this, src_table()->deleter()) {
    for(std::vector<std::string>::const_iterator it = subnets.begin();
        it != subnets.end(); it++) {
        error_code ec;
        Ip4Prefix ipam_subnet = Ip4Prefix::FromString(*it, &ec);
        assert(ec == 0);
        prefix_to_routelist_map_[ipam_subnet] = RouteList();
    }
}

// Compare config and return whether cfg has updated
bool 
ServiceChain::CompareServiceChainCfg(const autogen::ServiceChainInfo &cfg) {
    if (cfg.routing_instance != dest_->name()) {
        return false;
    }
    if (cfg.source_routing_instance != connected_->name()) {
        return false;
    }
    if (service_chain_addr_.to_string() != cfg.service_chain_address) {
        return false;
    }
    if (prefix_to_routelist_map_.size() != cfg.prefix.size()) {
        return false;
    }
    for (std::vector<std::string>::const_iterator it = cfg.prefix.begin();
         it != cfg.prefix.end(); it++) {
        error_code ec;
        Ip4Prefix ipam_subnet = Ip4Prefix::FromString(*it, &ec);
        if (prefix_to_routelist_map_.find(ipam_subnet) 
            == prefix_to_routelist_map_.end()) {
            return false;
        }
    }
    return true;
}

static int GetOriginVnIndex(const BgpRoute *route) {
    const BgpPath *path = route->BestPath();
    if (!path)
        return 0;

    const BgpAttr *attr = path->GetAttr();
    const ExtCommunity *ext_community = attr->ext_community();
    if (!ext_community)
        return 0;

    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (!ExtCommunity::is_origin_vn(comm))
            continue;
        OriginVn origin_vn(comm);
        return origin_vn.vn_index();
    }

    return 0;
}

// Match function called from BgpConditionListener
// Concurrency : db::DBTable
// For the purpose of route aggregation, two condition needs to be matched
//      1. More specific route present in any of the Dest BgpTable partition
//      2. Connected route(for nexthop) present in Src BgpTable
bool ServiceChain::Match(BgpServer *server, BgpTable *table, 
                              BgpRoute *route, bool deleted) {
    CHECK_CONCURRENCY("db::DBTable");
    ServiceChainRequest::RequestType type;
    Ip4Prefix aggregate_match;

    if (table == dest_table() && !dest_table_unregistered()) {
        if (is_connected_route(route)) {
            return false;
        }
        if (is_aggregate(route)) {
            return false;
        }
        if (aggregate_enable() && is_more_specific(route, &aggregate_match)) {
            // More specific
            if (deleted) {
                type = ServiceChainRequest::MORE_SPECIFIC_DELETE;
            } else {
                type = ServiceChainRequest::MORE_SPECIFIC_ADD_CHG;
            }
        } else {
            // External connecting routes
            if (!deleted) {
                if (!route->BestPath() || !route->BestPath()->IsFeasible()) {
                    deleted = true;
                } else {
                    const BgpAttr *attr = route->BestPath()->GetAttr();
                    const Community *comm = attr ? attr->community() : NULL;
                    if (comm && comm->ContainsValue(Community::NoAdvertise))
                        deleted = true;

                    int vn_index = GetOriginVnIndex(route);
                    int dest_vn_index = dest_->virtual_network_index();
                    if (!vn_index || dest_vn_index != vn_index) {
                        if (!dest_->virtual_network_allow_transit())
                            deleted = true;
                        if (!dest_vn_index)
                            deleted = true;
                    }
                }
            }

            if (deleted) {
                type = ServiceChainRequest::EXT_CONNECT_ROUTE_DELETE;
            } else {
                type = ServiceChainRequest::EXT_CONNECT_ROUTE_ADD_CHG;
            }
        }
    } else if ((table == connected_table()) && 
               !connected_table_unregistered() &&
               is_connected_route(route)) {
        if (!deleted) {
            if (!route->IsValid() ||
                route->BestPath()->GetSource() != BgpPath::BGP_XMPP) {
                deleted = true;
            }
        }

        // Connected route for service chain
        if (deleted) {
            type = ServiceChainRequest::CONNECTED_ROUTE_DELETE;
        } else {
            type = ServiceChainRequest::CONNECTED_ROUTE_ADD_CHG;
        }
    } else {
        return false;
    }

    BgpConditionListener *listener = server->condition_listener();
    ServiceChainState *state = 
        static_cast<ServiceChainState *>(listener->GetMatchState(table, route,
                                                                this));
    if (!deleted) {
        // MatchState is added to the Route to ensure that DBEntry is not
        // deleted before the ServiceChain module processes the WorkQueue
        // request.
        if (!state) {
            state = new ServiceChainState(ServiceChainPtr(this));
            listener->SetMatchState(table, route, this, state);
        }
    } else {
        // MatchState is set on all the Routes that matches the conditions 
        // Retrieve to check and ignore delete of unseen Add Match
        if (state == NULL) {
            // Not seen ADD ignore DELETE
            return false;
        }
    }

    // The MatchState reference is taken to ensure that the route is not 
    // deleted when request is still present in the queue
    // This is to handle the case where MatchState already exists and 
    // deleted entry gets reused or reused entry gets deleted.
    state->IncrementRefCnt();

    // Post the Match result to ServiceChain task to take Action
    // More_Specific_Present + Connected_Route_exists ==> Add Aggregate Route
    // and stitch the nexthop from connected route
    ServiceChainRequest *req = 
        new ServiceChainRequest(type, table, route, 
                                aggregate_match, ServiceChainPtr(this));
    server->service_chain_mgr()->Enqueue(req);

    return true;
}

bool ServiceChain::is_more_specific(BgpRoute *route, 
                                    Ip4Prefix *aggregate_match) {
    unsigned long broadcast = 0xFFFFFFFF;
    InetRoute *inet_route = dynamic_cast<InetRoute *>(route);
    Ip4Address address = inet_route->GetPrefix().ip4_addr();
    for(PrefixToRouteListMap::iterator it = prefix_to_route_list_map()->begin();
        it != prefix_to_route_list_map()->end(); it++) {
        unsigned long shift_mask = (32 - it->first.prefixlen());
        if ((it->first.prefixlen() != inet_route->GetPrefix().prefixlen()) &&
            ((address.to_ulong() & (broadcast << shift_mask)) == 
             (it->first.ip4_addr().to_ulong() & (broadcast << shift_mask)))) {
            *aggregate_match = it->first;
            return true;
        }
    }
    return false;
}

bool ServiceChain::is_aggregate(BgpRoute *route) {
    InetRoute *inet_route = dynamic_cast<InetRoute *>(route);
    for (PrefixToRouteListMap::iterator it = prefix_to_route_list_map()->begin();
        it != prefix_to_route_list_map()->end(); it++) {
        if (it->first == inet_route->GetPrefix())
            return true;
    }
    return false;
}

// RemoveServiceChainRoute
void ServiceChain::RemoveServiceChainRoute(Ip4Prefix prefix, bool aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    BgpTable *bgptable = src_table();
    InetRoute rt_key(prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(bgptable->GetTablePartition(&rt_key));
    BgpRoute *service_chain_route = 
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (!service_chain_route || service_chain_route->IsDeleted())
        return;

    for (ConnectedPathIdList::iterator it = ConnectedPathIds()->begin();
         it != ConnectedPathIds()->end(); it++) {
        service_chain_route->RemovePath(BgpPath::ServiceChain, NULL, *it);
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed " << (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(*it) <<
            " in table " << bgptable->name());
    }

    if (!service_chain_route->BestPath()) {
        partition->Delete(service_chain_route);
    } else {
        partition->Notify(service_chain_route);
    }
}

// AddServiceChainRoute
void ServiceChain::AddServiceChainRoute(Ip4Prefix prefix, InetRoute *orig_route,
        ConnectedPathIdList *old_path_ids, bool aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    BgpTable *bgptable = src_table();
    InetRoute rt_key(prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(bgptable->GetTablePartition(&rt_key));
    BgpRoute *service_chain_route = 
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (service_chain_route == NULL) {
        service_chain_route = new InetRoute(prefix);
        partition->Add(service_chain_route);
    } else {
        service_chain_route->ClearDelete();
    }

    int vn_index = dest_routing_instance()->virtual_network_index();
    BgpServer *server = dest_routing_instance()->server();
    OriginVn origin_vn(server->autonomous_system(), vn_index);

    ExtCommunity::ExtCommunityList sgid_list;
    if (orig_route) {
        const BgpPath *orig_path = orig_route->BestPath();
        const BgpAttr *orig_attr = NULL;
        const ExtCommunity *ext_community = NULL;
        if (orig_path)
            orig_attr = orig_path->GetAttr();
        if (orig_attr)
            ext_community = orig_attr->ext_community();
        if (ext_community) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_community->communities()) {
                if (ExtCommunity::is_security_group(comm))
                    sgid_list.push_back(comm);
            }
        }
    }

    BgpAttrDB *attr_db = src_->server()->attr_db();
    ExtCommunityDB *extcomm_db = src_->server()->extcomm_db();
    for (Route::PathList::iterator it = 
         connected_route()->GetPathList().begin();
         it != connected_route()->GetPathList().end(); it++) {
        BgpPath *connected_path = static_cast<BgpPath *>(it.operator->());

        // Infeasible paths are not considered
        if (!connected_path->IsFeasible()) break;

        // take snapshot of all ECMP paths
        if (connected_route()->BestPath()->PathCompare(*connected_path, true)) 
            break;

        // Skip paths with duplicate forwarding information.  This ensures
        // that we generate only one path with any given next hop and label
        // when there are multiple connected paths from the original source
        // received via different peers e.g. directly via XMPP and via BGP.
        if (connected_route()->DuplicateForwardingPath(connected_path))
            continue;

        const BgpAttr *attr = connected_path->GetAttr();
        ExtCommunityPtr new_ext_community;

        // Strip any RouteTargets from the connected attributes.
        new_ext_community = extcomm_db->ReplaceRTargetAndLocate(
            attr->ext_community(), ExtCommunity::ExtCommunityList());

        // Replace the SGID list with the list from the original route.
        new_ext_community = extcomm_db->ReplaceSGIDListAndLocate(
            new_ext_community.get(), sgid_list);

        // Replace the OriginVn with the value from the original route
        // or the value associated with the dest routing instance.
        new_ext_community = extcomm_db->ReplaceOriginVnAndLocate(
            new_ext_community.get(), origin_vn.GetExtCommunity());

        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            attr, new_ext_community);

        // Replace the source rd if the connected path is a secondary path
        // of a primary path in the l3vpn table. Use the RD of the primary.
        if (connected_path->IsReplicated()) {
            const BgpSecondaryPath *spath =
                static_cast<const BgpSecondaryPath *>(connected_path);
            const RoutingInstance *ri = spath->src_table()->routing_instance();
            if (ri->IsDefaultRoutingInstance()) {
                const InetVpnRoute *vpn_route =
                    static_cast<const InetVpnRoute *>(spath->src_rt());
                new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(),
                    vpn_route->GetPrefix().route_distinguisher());
            }
        }

        // Check whether we already have a path with the associated path id.
        uint32_t path_id =
            connected_path->GetAttr()->nexthop().to_v4().to_ulong();
        BgpPath *existing_path = 
            service_chain_route->FindPath(BgpPath::ServiceChain, NULL,
                                          path_id);
        bool is_stale = false;
        if (existing_path != NULL) {
            if ((new_attr.get() != existing_path->GetAttr()) || 
                (connected_path->GetLabel() != existing_path->GetLabel())) {
                // Update Attributes and notify (if needed)
                is_stale = existing_path->IsStale();
                service_chain_route->RemovePath(BgpPath::ServiceChain, NULL,
                                                path_id);
            } else 
                continue;
        }

        BgpPath *new_path = 
            new BgpPath(path_id, BgpPath::ServiceChain, new_attr.get(),
                        connected_path->GetFlags(), connected_path->GetLabel());
        if (is_stale) 
            new_path->SetStale();

        service_chain_route->InsertPath(new_path);
        partition->Notify(service_chain_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Added " << (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(path_id) <<
            " in table " << bgptable->name());
    }

    if (!old_path_ids) return;

    for (ConnectedPathIdList::iterator it = old_path_ids->begin();
         it != old_path_ids->end(); it++) {
        if (ConnectedPathIds()->find(*it) != ConnectedPathIds()->end())
            continue;
        service_chain_route->RemovePath(BgpPath::ServiceChain, NULL, *it);
        partition->Notify(service_chain_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed " << (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(*it) <<
            " in table " << bgptable->name());
    }
}

static void RemoveMatchState(BgpConditionListener *listener, ServiceChain *info,
                             BgpTable *table, BgpRoute *route, 
                             ServiceChainState *state) {
    if (info->deleted() || route->IsDeleted()) {
        // At this point we are ready to release the MatchState on the DBEntry
        // So mark it as deleted.. Actual removal of the state is done when 
        // ref count is 0
        state->set_deleted();
    }   
}

static void FillServiceChainConfigInfo(ShowServicechainInfo &info, 
                                       const autogen::ServiceChainInfo &sci) {
    info.set_dest_virtual_network(GetVNFromRoutingInstance(sci.routing_instance));
    info.set_service_instance(sci.service_instance);
}

struct ServiceChainInfoComp {
    bool operator()(const ShowServicechainInfo &lhs, const ShowServicechainInfo &rhs) {
        std::string lnetwork;
        std::string rnetwork;

        if (lhs.src_virtual_network < lhs.dest_virtual_network) {
            lnetwork = lhs.src_virtual_network + lhs.dest_virtual_network;
        } else {
            lnetwork = lhs.dest_virtual_network + lhs.src_virtual_network;
        }

        if (rhs.src_virtual_network < rhs.dest_virtual_network) {
            rnetwork = rhs.src_virtual_network + rhs.dest_virtual_network;
        } else {
            rnetwork = rhs.dest_virtual_network + rhs.src_virtual_network;
        }

        if (lnetwork != rnetwork) {
            return (lnetwork < rnetwork);
        }

        if (lhs.src_virtual_network != rhs.src_virtual_network) {
            return (lhs.src_virtual_network < rhs.src_virtual_network);
        }

        if (lhs.service_instance != rhs.service_instance) {
            return (lhs.service_instance < rhs.service_instance);
        }

        return false;
    }
};

bool ServiceChainMgr::RequestHandler(ServiceChainRequest *req) {
    CHECK_CONCURRENCY("bgp::ServiceChain");
    BgpTable *table = NULL;
    BgpRoute *route = NULL;
    Ip4Prefix aggregate_match = req->aggregate_match_;
    ServiceChain *info = NULL;

    if (req->type_ != ServiceChainRequest::SHOW_SERVICE_CHAIN &&
        req->type_ != ServiceChainRequest::SHOW_PENDING_CHAIN) {
        table = req->table_;
        route = req->rt_;
        info = static_cast<ServiceChain *>(req->info_.get());
        // Table where the aggregate route needs to be added
        aggregate_match = req->aggregate_match_;
    }

    BgpConditionListener *listener = server()->condition_listener();

    ServiceChainState *state = NULL;
    if (route) {
        state = static_cast<ServiceChainState *>
            (listener->GetMatchState(table, route, info));
    }

    switch (req->type_) {
        case ServiceChainRequest::MORE_SPECIFIC_ADD_CHG: {
            assert(state);
            if (state->deleted()) {
                state->reset_deleted();
            }
            if (info->add_more_specific(aggregate_match, route) && 
                info->connected_route_valid()) {
                // Add the aggregate route
                info->AddServiceChainRoute(
                    aggregate_match, NULL, NULL, true);
            }
            break;
        }
        case ServiceChainRequest::MORE_SPECIFIC_DELETE: {
            assert(state);
            if (info->delete_more_specific(aggregate_match, route)) {
                // Delete the aggregate route
                info->RemoveServiceChainRoute(aggregate_match, true);
            }
            RemoveMatchState(listener, info, table, route, state);
            break;
        }
        case ServiceChainRequest::CONNECTED_ROUTE_ADD_CHG: {
            assert(state);
            if (route->IsDeleted() || !route->BestPath() || 
                !route->BestPath()->IsFeasible())  {
                break;
            }

            if (state->deleted()) {
                state->reset_deleted();
            }
            // Store the old path list
            ServiceChain::ConnectedPathIdList path_ids;
            path_ids.swap(*(info->ConnectedPathIds()));

            // Populate the ConnectedPathId
            info->set_connected_route(route);

            ServiceChain::PrefixToRouteListMap *vnprefix_list = 
                info->prefix_to_route_list_map();
            for (ServiceChain::PrefixToRouteListMap::iterator it = 
                 vnprefix_list->begin(); it != vnprefix_list->end(); it++) {
                // Add aggregate route.. Or if the route exists
                // sync the path and purge old paths
                if (!it->second.empty())
                    info->AddServiceChainRoute(
                        it->first, NULL, &path_ids, true);
            }

            for (ServiceChain::ExtConnectRouteList::iterator it = 
                 info->ext_connecting_routes()->begin(); 
                 it != info->ext_connecting_routes()->end(); it++) {
                // Add ServiceChain route for external connecting route
                InetRoute *ext_route = static_cast<InetRoute *>(*it);
                info->AddServiceChainRoute(
                    ext_route->GetPrefix(), ext_route, &path_ids, false);
            }
            break;
        }
        case ServiceChainRequest::CONNECTED_ROUTE_DELETE: {
            assert(state);
            ServiceChain::PrefixToRouteListMap *vnprefix_list 
                = info->prefix_to_route_list_map();
            for (ServiceChain::PrefixToRouteListMap::iterator it 
                 = vnprefix_list->begin(); it != vnprefix_list->end(); it++) {
                // Delete the aggregate route
                info->RemoveServiceChainRoute(it->first, true);
            }

            for (ServiceChain::ExtConnectRouteList::iterator it = 
                 info->ext_connecting_routes()->begin(); 
                 it != info->ext_connecting_routes()->end(); it++) {
                // Delete ServiceChain route for external connecting route
                InetRoute *ext_route = static_cast<InetRoute *>(*it);
                info->RemoveServiceChainRoute(ext_route->GetPrefix(), false);
            }
            RemoveMatchState(listener, info, table, route, state);
            info->set_connected_route(NULL);
            break;
        }
        case ServiceChainRequest::EXT_CONNECT_ROUTE_ADD_CHG: {
            assert(state);
            if (state->deleted()) {
                state->reset_deleted();
            }
            info->ext_connecting_routes()->insert(route);
            if (info->connected_route_valid()) { 
                InetRoute *ext_route = dynamic_cast<InetRoute *>(route);
                info->AddServiceChainRoute(
                    ext_route->GetPrefix(), ext_route, NULL, false);
            }
            break;
        }
        case ServiceChainRequest::EXT_CONNECT_ROUTE_DELETE: {
            assert(state);
            if (info->ext_connecting_routes()->erase(route)) {
                InetRoute *inet_route = dynamic_cast<InetRoute *>(route);
                info->RemoveServiceChainRoute(inet_route->GetPrefix(), false);
            }
            RemoveMatchState(listener, info, table, route, state);
            break;
         }
        case ServiceChainRequest::STOP_CHAIN_DONE: {
            if (table == info->connected_table()) {
                info->set_connected_table_unregistered();
                if (!info->num_matchstate()) {
                    listener->UnregisterCondition(table, info);
                }
            }
            if (table == info->dest_table()) {
                info->set_dest_table_unregistered();
                if (!info->num_matchstate()) {
                    listener->UnregisterCondition(table, info);
                }
            }
            if (info->unregistered()) {
                chain_set_.erase(info->src_routing_instance());
                StartResolve();
            }
            break;
        }
        case ServiceChainRequest::SHOW_SERVICE_CHAIN: {
            ShowServiceChainResp *resp = 
                static_cast<ShowServiceChainResp *>(req->snh_resp_);
            std::vector<ShowServicechainInfo> list;
            RoutingInstanceMgr::RoutingInstanceIterator rit = 
                server()->routing_instance_mgr()->begin();
            for (;rit != server()->routing_instance_mgr()->end(); rit++) {
                if (rit->deleted()) continue;
                ShowServicechainInfo info;
                const autogen::RoutingInstance *rti = rit->config()->instance_config();
                if (rti && rti->IsPropertySet(autogen::RoutingInstance::SERVICE_CHAIN_INFORMATION)) {
                    info.set_src_virtual_network(rit->virtual_network());
                    const autogen::ServiceChainInfo &sci = rti->service_chain_information();
                    FillServiceChainConfigInfo(info, sci);
                    ServiceChain *chain = FindServiceChain(rit->name());
                    if (chain) {
                        if (chain->deleted())
                            info.set_state("Deleted");
                        else 
                            info.set_state("Active");
                        chain->FillServiceChainInfo(info);
                    } else {
                        info.set_state("Pending");
                    }
                    list.push_back(info);
                }
            }
            ServiceChainInfoComp comp;
            std::sort(list.begin(), list.end(), comp);
            resp->set_service_chain_list(list);
            resp->Response();
            break;
        }
        case ServiceChainRequest::SHOW_PENDING_CHAIN: {
            ShowPendingServiceChainResp *resp = 
                static_cast<ShowPendingServiceChainResp *>(req->snh_resp_);
            std::vector<std::string> pending_list;
            for(UnresolvedServiceChainList::const_iterator it 
                = pending_chains().begin(); it != pending_chains().end(); it++)
                pending_list.push_back((*it)->name());

            resp->set_pending_chains(pending_list);
            resp->Response();
            break;
        }
        default: {
            assert(0);
            break;
        }
    }

    if (state) {
        state->DecrementRefCnt();
        if (state->refcnt() == 0 && state->deleted()) {
            listener->RemoveMatchState(table, route, info);
            delete state;
            if (!info->num_matchstate()) {
                if (info->dest_table_unregistered()) {
                    listener->UnregisterCondition(info->dest_table(), info);
                }
                if (info->connected_table_unregistered()) {
                    listener->UnregisterCondition(info->connected_table(), info);
                }
                if (info->unregistered()) {
                    chain_set_.erase(info->src_routing_instance());
                    StartResolve();
                }
            }
        }
    }
    delete req;
    return true;
}

ServiceChainMgr::ServiceChainMgr(BgpServer *server) : server_(server), 
    resolve_trigger_(new TaskTrigger(
             boost::bind(&ServiceChainMgr::ResolvePendingServiceChain, this), 
             TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
    aggregate_host_route_(false) {
    if (service_chain_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        service_chain_task_id_ = scheduler->GetTaskId("bgp::ServiceChain");
    }

    process_queue_ = 
        new WorkQueue<ServiceChainRequest *>(service_chain_task_id_, 0, 
                     boost::bind(&ServiceChainMgr::RequestHandler, this, _1));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        boost::bind(&ServiceChainMgr::RoutingInstanceCallback, this, _1, _2));
}

ServiceChainMgr::~ServiceChainMgr() {
    delete process_queue_;
    server()->routing_instance_mgr()->UnregisterInstanceOpCallback(id_);
}

void ServiceChainMgr::Enqueue(ServiceChainRequest *req) { 
    process_queue_->Enqueue(req); 
}

bool ServiceChainMgr::LocateServiceChain(RoutingInstance *rtinstance, 
                                         const autogen::ServiceChainInfo &cfg) {
    CHECK_CONCURRENCY("bgp::Config");
    // Verify whether the entry already exists
    ServiceChainMap::iterator it = chain_set_.find(rtinstance);
    if (it != chain_set_.end()) {
        ServiceChain *chain = static_cast<ServiceChain *>(it->second.get());
        if (chain->CompareServiceChainCfg(cfg)) {
            BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                        "No update in ServiceChain config : " << rtinstance->name());
            return true;
        }

        // Entry already exists. Update of match condition
        // The routing instance to pending resolve such that
        // service chain is created after stop done cb
        AddPendingServiceChain(rtinstance);

        if (it->second->deleted()) {
            // Wait for the delete complete cb
            return false;
        }

        BgpConditionListener::RequestDoneCb callback = 
            boost::bind(&ServiceChainMgr::StopServiceChainDone, this, _1, _2);

        server()->condition_listener()->RemoveMatchCondition(
                         chain->dest_table(), it->second.get(), callback);

        server()->condition_listener()->RemoveMatchCondition(
                         chain->connected_table(), it->second.get(), callback);
        return true;
    }

    RoutingInstanceMgr *mgr = server()->routing_instance_mgr();
    RoutingInstance *dest = mgr->GetRoutingInstance(cfg.routing_instance);
    // Destination routing instance is not yet created.
    if (dest == NULL || dest->deleted()) {
        // Wait for the creation of RoutingInstance
        AddPendingServiceChain(rtinstance);
        return false;
    }

    RoutingInstance *connected_ri = NULL;
    if (cfg.source_routing_instance == "") {
        connected_ri = rtinstance;
        assert(!rtinstance->deleted());
    } else {
        connected_ri = mgr->GetRoutingInstance(cfg.source_routing_instance);
    }
    // routing instance to search for connected route is not yet created.
    if (connected_ri == NULL || connected_ri->deleted()) {
        // Wait for the creation of RoutingInstance where connected route 
        // will be published
        AddPendingServiceChain(rtinstance);
        return false;
    }

    // Get the service chain address
    error_code ec;
    IpAddress chain_addr = 
        Ip4Address::from_string(cfg.service_chain_address, ec);
    assert(ec == 0);

    // Get the BGP Tables to add condition
    BgpTable *connected_table = connected_ri->GetTable(Address::INET);
    assert(connected_table);
    BgpTable *dest_table = dest->GetTable(Address::INET);
    assert(dest_table);

    // Allocate the new service chain and verify whether one already exists
    ServiceChainPtr chain 
        = ServiceChainPtr(new ServiceChain(rtinstance, dest, connected_ri,
                                           cfg.prefix, chain_addr));

    if (aggregate_host_route()) {
        ServiceChain *obj = static_cast<ServiceChain *>(chain.get());
        obj->set_aggregate_enable();
    }

    // Add the new service chain request
    chain_set_.insert(std::make_pair(rtinstance, chain));

    server()->condition_listener()->AddMatchCondition(connected_table, chain.get(), 
                                      BgpConditionListener::RequestDoneCb());
    server()->condition_listener()->AddMatchCondition(dest_table, chain.get(),
                                      BgpConditionListener::RequestDoneCb());
    return true;
}

bool ServiceChainMgr::ResolvePendingServiceChain() {
    for (UnresolvedServiceChainList::iterator it = pending_chain_.begin(), next;
         it != pending_chain_.end(); it = next) {
        next = it;
        ++next;
        RoutingInstance *rtinst = *it;
        pending_chain_.erase(it);
        LocateServiceChain(rtinst,
           rtinst->config()->instance_config()->service_chain_information());
    }
    return true;
}

void ServiceChainMgr::RoutingInstanceCallback(std::string name, int op) {
    if (op == RoutingInstanceMgr::INSTANCE_ADD) StartResolve();
}

void ServiceChainMgr::StartResolve() {
    if (pending_chain_.empty() == false) {
        resolve_trigger_->Set();
    }
}

void ServiceChainMgr::StopServiceChainDone(BgpTable *table, 
                                           ConditionMatch *info) {
    // Post the RequestDone event to ServiceChain task to take Action
    ServiceChainRequest *req = 
        new ServiceChainRequest(ServiceChainRequest::STOP_CHAIN_DONE, table,
                                NULL, Ip4Prefix(), ServiceChainPtr(info));

    server()->service_chain_mgr()->Enqueue(req);
    return;
}

void ServiceChainMgr::StopServiceChain(RoutingInstance *src) {
    // Remove the src routing instance from the pending_chain_
    pending_chain_.erase(src);

    ServiceChainMap::iterator it = chain_set_.find(src);
    if (it == chain_set_.end()) {
        return;
    }

    if (it->second->deleted()) {
        return;
    }
    BgpConditionListener::RequestDoneCb callback = 
        boost::bind(&ServiceChainMgr::StopServiceChainDone, this, _1, _2);

    ServiceChain *obj = static_cast<ServiceChain *>(it->second.get());

    server()->condition_listener()->RemoveMatchCondition(obj->dest_table(), obj,
                                                         callback);

    server()->condition_listener()->RemoveMatchCondition(obj->connected_table(), obj, 
                                                         callback);
}

void ServiceChain::FillServiceChainInfo(ShowServicechainInfo &info) const {
    info.set_src_rt_instance(src_routing_instance()->name());
    info.set_connected_rt_instance(connected_routing_instance()->name());
    info.set_dest_rt_instance(dest_routing_instance()->name());

    ConnectedRouteInfo connected_rt_info;
    connected_rt_info.set_service_chain_addr(service_chain_addr().to_string());
    if (connected_route()) {
        ShowRoute show_route;
        connected_route()->FillRouteInfo(connected_table(), &show_route);
        connected_rt_info.set_connected_rt(show_route);
    }
    info.set_connected_route(connected_rt_info);

    std::vector<PrefixToRouteListInfo> more_vec;
    for(PrefixToRouteListMap::const_iterator it = 
        prefix_to_route_list_map().begin(); 
        it != prefix_to_route_list_map().end(); it++) {
        PrefixToRouteListInfo prefix_list_info;
        prefix_list_info.set_prefix(it->first.ToString());

        BgpTable *bgptable = src_table();
        InetRoute rt_key(it->first);
        BgpRoute *aggregate = static_cast<BgpRoute *>(bgptable->Find(&rt_key));
        if (aggregate) {
            prefix_list_info.set_aggregate(true);
            ShowRoute show_route;
            aggregate->FillRouteInfo(bgptable, &show_route);
            prefix_list_info.set_aggregate_rt(show_route);
        } else {
            prefix_list_info.set_aggregate(false);
        }

        std::vector<std::string> rt_list;
        for (RouteList::iterator rt_it = it->second.begin();
             rt_it != it->second.end(); rt_it++) {
            rt_list.push_back((*rt_it)->ToString());
        }
        prefix_list_info.set_more_specific_list(rt_list);
        more_vec.push_back(prefix_list_info);
    }
    info.set_more_specifics(more_vec);

    std::vector<ExtConnectRouteInfo> ext_connecting_rt_info_list;
    for(ExtConnectRouteList::const_iterator it = ext_connecting_routes().begin();
        it != ext_connecting_routes().end(); it++) {
        ExtConnectRouteInfo ext_rt_info;
        ext_rt_info.set_ext_rt_prefix((*it)->ToString());
        BgpTable *bgptable = src_table();
        InetRoute *ext_route = static_cast<InetRoute *>(*it);
        InetRoute rt_key(ext_route->GetPrefix());
        BgpRoute *ext_connecting = 
            static_cast<BgpRoute *>(bgptable->Find(&rt_key));
        if (ext_connecting) {
            ShowRoute show_route;
            ext_connecting->FillRouteInfo(bgptable, &show_route);
            ext_rt_info.set_ext_rt_svc_rt(show_route);
        }
        ext_connecting_rt_info_list.push_back(ext_rt_info);
    }
    info.set_ext_connecting_rt_info_list(ext_connecting_rt_info_list);
    info.set_aggregate_enable(aggregate_enable());
}

void ShowServiceChainReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());
    ServiceChainMgr *mgr =  bsc->bgp_server->service_chain_mgr();
    ShowServiceChainResp *resp = new ShowServiceChainResp;
    resp->set_context(context());
    ServiceChainRequest  *req = 
        new ServiceChainRequest(ServiceChainRequest::SHOW_SERVICE_CHAIN, resp);
    mgr->Enqueue(req);
}

void ShowPendingServiceChainReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());
    ServiceChainMgr *mgr =  bsc->bgp_server->service_chain_mgr();
    ShowPendingServiceChainResp *resp = new ShowPendingServiceChainResp;
    resp->set_context(context());
    ServiceChainRequest  *req = 
        new ServiceChainRequest(ServiceChainRequest::SHOW_PENDING_CHAIN, resp);
    mgr->Enqueue(req);
}

BgpTable *ServiceChain::src_table() const {
    return src_->GetTable(Address::INET);
}

BgpTable *ServiceChain::connected_table() const {
    return connected_->GetTable(Address::INET);
}

BgpTable *ServiceChain::dest_table() const {
    return dest_->GetTable(Address::INET);
}

ServiceChain *ServiceChainMgr::FindServiceChain(const std::string &src) {
    RoutingInstance *rtinstance = 
        server()->routing_instance_mgr()->GetRoutingInstance(src);
    if (!rtinstance) return NULL;
    ServiceChainMap::iterator it = chain_set_.find(rtinstance);
    if (it == chain_set_.end()) return NULL;
    ServiceChain *chain = static_cast<ServiceChain *>(it->second.get());
    return chain;
}
