/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/service_chaining.h"

#include <boost/foreach.hpp>

#include <algorithm>

#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_server.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/inet6vpn/inet6vpn_route.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/service_chaining_types.h"
#include "net/community_type.h"

using boost::bind;
using boost::system::error_code;
using std::make_pair;
using std::sort;
using std::string;
using std::vector;

template<>
int ServiceChainMgr<ServiceChainInet>::service_chain_task_id_ = -1;
template<>
int ServiceChainMgr<ServiceChainInet6>::service_chain_task_id_ = -1;

static int GetOriginVnIndex(const BgpTable *table, const BgpRoute *route) {
    const BgpPath *path = route->BestPath();
    if (!path)
        return 0;

    const BgpAttr *attr = path->GetAttr();
    const ExtCommunity *ext_community = attr->ext_community();
    if (ext_community) {
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            return origin_vn.vn_index();
        }
    }
    if (path->IsVrfOriginated())
        return table->routing_instance()->virtual_network_index();
    return 0;
}

template <typename T>
class ServiceChainMgr<T>::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(ServiceChainMgr<T> *manager)
        : LifetimeActor(manager->server_->lifetime_manager()),
          manager_(manager) {
    }
    virtual bool MayDelete() const {
        return manager_->MayDelete();
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        manager_->Terminate();
    }

private:
    ServiceChainMgr<T> *manager_;
};

template <typename T>
ServiceChain<T>::ServiceChain(ServiceChainMgrT *manager,
    ServiceChainGroup *group, RoutingInstance *src, RoutingInstance *dest,
    RoutingInstance *connected, const vector<string> &subnets, AddressT addr)
    : manager_(manager),
      group_(group),
      src_(src),
      dest_(dest),
      connected_(connected),
      connected_route_(NULL),
      service_chain_addr_(addr),
      group_oper_state_up_(group ? false : true),
      connected_table_unregistered_(false),
      dest_table_unregistered_(false),
      aggregate_(false),
      src_table_delete_ref_(this, src_table()->deleter()),
      dest_table_delete_ref_(this, dest_table()->deleter()),
      connected_table_delete_ref_(this, connected_table()->deleter()) {
    for (vector<string>::const_iterator it = subnets.begin();
         it != subnets.end(); ++it) {
        error_code ec;
        PrefixT ipam_subnet = PrefixT::FromString(*it, &ec);
        if (ec != 0)
            continue;
        prefix_to_routelist_map_[ipam_subnet] = RouteList();
    }
}

template <typename T>
BgpTable *ServiceChain<T>::src_table() const {
    return src_->GetTable(GetFamily());
}

template <typename T>
BgpTable *ServiceChain<T>::connected_table() const {
    return connected_->GetTable(GetFamily());
}

template <typename T>
BgpTable *ServiceChain<T>::dest_table() const {
    return dest_->GetTable(GetFamily());
}

//
// Compare this ServiceChain against the ServiceChainConfig.
// Return true if the configuration has not changed, false otherwise.
//
template <typename T>
bool ServiceChain<T>::CompareServiceChainConfig(
    const ServiceChainConfig &config) {
    if (deleted())
        return false;
    if (dest_->name() != config.routing_instance)
        return false;
    if (connected_->name() != config.source_routing_instance)
        return false;
    if (service_chain_addr_.to_string() != config.service_chain_address)
        return false;
    if (!group_ && !config.service_chain_id.empty())
        return false;
    if (group_ && config.service_chain_id.empty())
        return false;
    if (group_ && group_->name() != config.service_chain_id)
        return false;

    if (prefix_to_routelist_map_.size() != config.prefix.size())
        return false;
    for (vector<string>::const_iterator it = config.prefix.begin();
         it != config.prefix.end(); ++it) {
        error_code ec;
        PrefixT ipam_subnet = PrefixT::FromString(*it, &ec);
        if (prefix_to_routelist_map_.find(ipam_subnet) ==
            prefix_to_routelist_map_.end()) {
            return false;
        }
    }
    return true;
}

//
// Match function called from BgpConditionListener
// Concurrency : db::DBTable
// For the purpose of route aggregation, two condition needs to be matched
//      1. More specific route present in any of the Dest BgpTable partition
//      2. Connected route(for nexthop) present in Src BgpTable
//
template <typename T>
bool ServiceChain<T>::Match(BgpServer *server, BgpTable *table, BgpRoute *route,
    bool deleted) {
    CHECK_CONCURRENCY("db::DBTable");

    typename ServiceChainRequestT::RequestType type;
    PrefixT aggregate_match;

    if (table == dest_table() && !dest_table_unregistered()) {
        if (IsConnectedRoute(route))
            return false;
        if (aggregate_enable() && IsAggregate(route))
            return false;

        if (aggregate_enable() && IsMoreSpecific(route, &aggregate_match)) {
            // More specific
            if (deleted) {
                type = ServiceChainRequestT::MORE_SPECIFIC_DELETE;
            } else {
                type = ServiceChainRequestT::MORE_SPECIFIC_ADD_CHG;
            }
        } else {
            // External connecting routes
            if (!deleted) {
                if (!route->BestPath() || !route->BestPath()->IsFeasible()) {
                    deleted = true;
                } else {
                    const BgpAttr *attr = route->BestPath()->GetAttr();
                    const Community *comm = attr ? attr->community() : NULL;
                    if (comm) {
                        if ((comm->ContainsValue(CommunityType::NoAdvertise)) ||
                           (comm->ContainsValue(CommunityType::NoReOriginate)))
                        deleted = true;
                    }

                    int vn_index = GetOriginVnIndex(table, route);
                    int src_vn_index = src_->virtual_network_index();
                    int dest_vn_index = dest_->virtual_network_index();
                    if (!vn_index || dest_vn_index != vn_index) {
                        if (src_vn_index == vn_index)
                            deleted = true;
                        if (!dest_->virtual_network_allow_transit())
                            deleted = true;
                        if (!dest_vn_index)
                            deleted = true;
                    }

                    OriginVn src_origin_vn(
                        server->autonomous_system(), src_vn_index);
                    const OriginVnPath *ovnpath =
                        attr ? attr->origin_vn_path() : NULL;
                    if (ovnpath && ovnpath->Contains(
                                server->autonomous_system(), src_vn_index)) {
                        //ovnpath->Contains(src_origin_vn.GetExtCommunity())) {
                        deleted = true;
                    }
                }
            }

            if (deleted) {
                type = ServiceChainRequestT::EXT_CONNECT_ROUTE_DELETE;
            } else {
                type = ServiceChainRequestT::EXT_CONNECT_ROUTE_ADD_CHG;
            }
        }
    } else if ((table == connected_table()) &&
               !connected_table_unregistered() &&
               IsConnectedRoute(route)) {
        if (!deleted) {
            if (!route->IsValid() ||
                route->BestPath()->GetSource() != BgpPath::BGP_XMPP) {
                deleted = true;
            }
        }

        // Connected route for service chain
        if (deleted) {
            type = ServiceChainRequestT::CONNECTED_ROUTE_DELETE;
        } else {
            type = ServiceChainRequestT::CONNECTED_ROUTE_ADD_CHG;
        }
    } else {
        return false;
    }

    BgpConditionListener *listener = server->condition_listener(GetFamily());
    ServiceChainState *state = static_cast<ServiceChainState *>(
        listener->GetMatchState(table, route, this));
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
    ServiceChainRequestT *req = new ServiceChainRequestT(
        type, table, route, aggregate_match, ServiceChainPtr(this));
    manager_->Enqueue(req);
    return true;
}

template <typename T>
string ServiceChain<T>::ToString() const {
    return (string("ServiceChain " ) + service_chain_addr_.to_string());
}

template <typename T>
void ServiceChain<T>::SetConnectedRoute(BgpRoute *connected) {
    connected_route_ = connected;
    connected_path_ids_.clear();
    if (!connected_route_)
        return;

    for (Route::PathList::iterator it = connected->GetPathList().begin();
        it != connected->GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());

        // Infeasible paths are not considered.
        if (!path->IsFeasible())
            break;

        // Bail if it's not ECMP with the best path.
        if (connected_route_->BestPath()->PathCompare(*path, true))
            break;

        // Use nexthop attribute of connected path as path id.
        uint32_t path_id = path->GetAttr()->nexthop().to_v4().to_ulong();
        connected_path_ids_.insert(path_id);
    }
}

template <typename T>
bool ServiceChain<T>::IsConnectedRouteValid() const {
    return (connected_route_ && connected_route_->IsValid());
}

template <typename T>
bool ServiceChain<T>::IsMoreSpecific(BgpRoute *route,
    PrefixT *aggregate_match) const {
    const RouteT *ip_route = static_cast<RouteT *>(route);
    const PrefixT &ip_prefix = ip_route->GetPrefix();
    for (typename PrefixToRouteListMap::const_iterator it =
         prefix_to_route_list_map()->begin();
         it != prefix_to_route_list_map()->end(); ++it) {
        if (ip_prefix.IsMoreSpecific(it->first)) {
            *aggregate_match = it->first;
            return true;
        }
    }
    return false;
}

template <typename T>
bool ServiceChain<T>::IsAggregate(BgpRoute *route) const {
    RouteT *ip_route = dynamic_cast<RouteT *>(route);
    for (typename PrefixToRouteListMap::const_iterator it =
         prefix_to_route_list_map()->begin();
         it != prefix_to_route_list_map()->end(); ++it) {
        if (it->first == ip_route->GetPrefix())
            return true;
    }
    return false;
}

template <typename T>
bool ServiceChain<T>::IsConnectedRoute(BgpRoute *route) const {
    RouteT *ip_route = dynamic_cast<RouteT *>(route);
    return (service_chain_addr() == ip_route->GetPrefix().addr());
}

template <typename T>
void ServiceChain<T>::RemoveMatchState(BgpRoute *route,
    ServiceChainState *state) {
    if (deleted() || route->IsDeleted()) {
        // At this point we are ready to release the MatchState on the DBEntry
        // So mark it as deleted.. Actual removal of the state is done when
        // ref count is 0
        state->set_deleted();
    }
}

template <typename T>
void ServiceChain<T>::DeleteServiceChainRoute(PrefixT prefix, bool aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    BgpTable *bgptable = src_table();
    RouteT rt_key(prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(bgptable->GetTablePartition(&rt_key));
    BgpRoute *service_chain_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (!service_chain_route || service_chain_route->IsDeleted())
        return;

    for (ConnectedPathIdList::const_iterator it = GetConnectedPathIds().begin();
         it != GetConnectedPathIds().end(); ++it) {
        uint32_t path_id = *it;
        service_chain_route->RemovePath(BgpPath::ServiceChain, NULL, path_id);
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed " << (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(path_id) <<
            " in table " << bgptable->name());
    }

    if (!service_chain_route->HasPaths()) {
        partition->Delete(service_chain_route);
    } else {
        partition->Notify(service_chain_route);
    }
}

template <typename T>
void ServiceChain<T>::UpdateServiceChainRoute(PrefixT prefix,
    const RouteT *orig_route, const ConnectedPathIdList &old_path_ids,
    bool aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    BgpTable *bgptable = src_table();
    RouteT rt_key(prefix);
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(bgptable->GetTablePartition(&rt_key));
    BgpRoute *service_chain_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (service_chain_route == NULL) {
        service_chain_route = new RouteT(prefix);
        partition->Add(service_chain_route);
    } else {
        service_chain_route->ClearDelete();
    }

    int vn_index = dest_routing_instance()->virtual_network_index();
    BgpServer *server = dest_routing_instance()->server();
    OriginVn origin_vn(server->autonomous_system(), vn_index);

    SiteOfOrigin soo;
    ExtCommunity::ExtCommunityList sgid_list;
    ExtCommunity::ExtCommunityList tag_list;
    LoadBalance load_balance;
    bool load_balance_present = false;
    const Community *orig_community = NULL;
    const OriginVnPath *orig_ovnpath = NULL;
    RouteDistinguisher orig_rd;
    if (orig_route) {
        const BgpPath *orig_path = orig_route->BestPath();
        const BgpAttr *orig_attr = NULL;
        const ExtCommunity *ext_community = NULL;
        if (orig_path)
            orig_attr = orig_path->GetAttr();
        if (orig_attr) {
            orig_community = orig_attr->community();
            ext_community = orig_attr->ext_community();
            orig_ovnpath = orig_attr->origin_vn_path();
            orig_rd = orig_attr->source_rd();
        }
        if (ext_community) {
            BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                          ext_community->communities()) {
                if (ExtCommunity::is_security_group(comm))
                    sgid_list.push_back(comm);
                if (ExtCommunity::is_tag(comm))
                    tag_list.push_back(comm);
                if (ExtCommunity::is_site_of_origin(comm) && soo.IsNull())
                    soo = SiteOfOrigin(comm);
                if (ExtCommunity::is_load_balance(comm)) {
                    load_balance = LoadBalance(comm);
                    load_balance_present = true;
                }
            }
        }
    }

    BgpAttrDB *attr_db = server->attr_db();
    CommunityDB *comm_db = server->comm_db();
    CommunityPtr new_community = comm_db->AppendAndLocate(
        orig_community, CommunityType::AcceptOwnNexthop);
    ExtCommunityDB *extcomm_db = server->extcomm_db();
    BgpMembershipManager *membership_mgr = server->membership_mgr();
    OriginVnPathDB *ovnpath_db = server->ovnpath_db();
    OriginVnPathPtr new_ovnpath =
        ovnpath_db->PrependAndLocate(orig_ovnpath, origin_vn.GetExtCommunity());

    ConnectedPathIdList new_path_ids;
    for (Route::PathList::iterator it =
         connected_route()->GetPathList().begin();
         it != connected_route()->GetPathList().end(); ++it) {
        BgpPath *connected_path = static_cast<BgpPath *>(it.operator->());

        // Infeasible paths are not considered
        if (!connected_path->IsFeasible())
            break;

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

        // Replace the Tag list with the list from the original route.
        new_ext_community = extcomm_db->ReplaceTagListAndLocate(
            new_ext_community.get(), tag_list);

        // Replace SiteOfOrigin with value from original route if any.
        if (soo.IsNull()) {
            new_ext_community = extcomm_db->RemoveSiteOfOriginAndLocate(
                new_ext_community.get());
        } else {
            new_ext_community = extcomm_db->ReplaceSiteOfOriginAndLocate(
                new_ext_community.get(), soo.GetExtCommunity());
        }

        // Inherit load balance attribute of orig_route if connected path
        // does not have one already.
        if (!LoadBalance::IsPresent(connected_path) && load_balance_present) {
            new_ext_community = extcomm_db->AppendAndLocate(
                    new_ext_community.get(), load_balance.GetExtCommunity());
        }

        // Replace the OriginVn with the value from the original route
        // or the value associated with the dest routing instance.
        new_ext_community = extcomm_db->ReplaceOriginVnAndLocate(
            new_ext_community.get(), origin_vn.GetExtCommunity());

        // Replace extended community, community and origin vn path.
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            attr, new_ext_community);
        new_attr =
            attr_db->ReplaceCommunityAndLocate(new_attr.get(), new_community);
        new_attr = attr_db->ReplaceOriginVnPathAndLocate(new_attr.get(),
            new_ovnpath);

        // Strip aspath. This is required when the connected route is
        // learnt via BGP.
        new_attr = attr_db->ReplaceAsPathAndLocate(new_attr.get(), AsPathPtr());

        // If the connected path is learnt via XMPP, construct RD based on
        // the id registered with source table instead of connected table.
        // This allows chaining of multiple in-network service instances
        // that are on the same compute node.
        const IPeer *peer = connected_path->GetPeer();
        if (src_ != connected_ && peer && peer->IsXmppPeer()) {
            int instance_id = -1;
            bool is_registered = membership_mgr->GetRegistrationInfo(peer,
                                                       bgptable, &instance_id);
            if (!is_registered)
                continue;
            RouteDistinguisher connected_rd = attr->source_rd();
            if (connected_rd.Type() != RouteDistinguisher::TypeIpAddressBased)
                continue;

            RouteDistinguisher rd(connected_rd.GetAddress(), instance_id);
            new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(), rd);
        }

        // Replace the source rd if the connected path is a secondary path
        // of a primary path in the l3vpn table. Use the RD of the primary.
        if (connected_path->IsReplicated()) {
            const BgpSecondaryPath *spath =
                static_cast<const BgpSecondaryPath *>(connected_path);
            const RoutingInstance *ri = spath->src_table()->routing_instance();
            if (ri->IsMasterRoutingInstance()) {
                const VpnRouteT *vpn_route =
                    static_cast<const VpnRouteT *>(spath->src_rt());
                new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(),
                    vpn_route->GetPrefix().route_distinguisher());
            }
        }

        // Skip paths with Source RD same as source RD of the connected path
        if (!orig_rd.IsZero() && new_attr->source_rd() == orig_rd)
            continue;

        // Check whether we already have a path with the associated path id.
        uint32_t path_id =
            connected_path->GetAttr()->nexthop().to_v4().to_ulong();
        BgpPath *existing_path =
            service_chain_route->FindPath(BgpPath::ServiceChain, NULL,
                                          path_id);
        bool path_updated = false;
        if (existing_path != NULL) {
            // Existing path can be reused.
            if ((new_attr.get() == existing_path->GetAttr()) &&
                (connected_path->GetLabel() == existing_path->GetLabel()) &&
                (connected_path->GetFlags() == existing_path->GetFlags())) {
                new_path_ids.insert(path_id);
                continue;
            }

            // Remove existing path, new path will be added below.
            path_updated = true;
            service_chain_route->RemovePath(
                BgpPath::ServiceChain, NULL, path_id);
        }

        BgpPath *new_path =
            new BgpPath(path_id, BgpPath::ServiceChain, new_attr.get(),
                        connected_path->GetFlags(), connected_path->GetLabel());
        new_path_ids.insert(path_id);
        service_chain_route->InsertPath(new_path);
        partition->Notify(service_chain_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            (path_updated ? "Updated " : "Added ") <<
            (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(path_id) <<
            " in table " << bgptable->name());
    }

    // Remove stale paths.
    for (ConnectedPathIdList::const_iterator it = old_path_ids.begin();
         it != old_path_ids.end(); ++it) {
        uint32_t path_id = *it;
        if (new_path_ids.find(path_id) != new_path_ids.end())
            continue;
        service_chain_route->RemovePath(BgpPath::ServiceChain, NULL, path_id);
        partition->Notify(service_chain_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed " << (aggregate ? "Aggregate" : "ExtConnected") <<
            " ServiceChain path " << service_chain_route->ToString() <<
            " path_id " << BgpPath::PathIdString(path_id) <<
            " in table " << bgptable->name());
    }

    // Delete the route if there's no paths.
    if (!service_chain_route->HasPaths())
        partition->Delete(service_chain_route);
}

template <typename T>
bool ServiceChain<T>::AddMoreSpecific(PrefixT aggregate,
    BgpRoute *more_specific) {
    typename PrefixToRouteListMap::iterator it =
        prefix_to_routelist_map_.find(aggregate);
    assert(it != prefix_to_routelist_map_.end());
    bool ret = false;
    if (it->second.empty()) {
        // Add the aggregate for the first time
        ret = true;
    }
    it->second.insert(more_specific);
    return ret;
}

template <typename T>
bool ServiceChain<T>::DeleteMoreSpecific(PrefixT aggregate,
    BgpRoute *more_specific) {
    typename PrefixToRouteListMap::iterator it =
        prefix_to_routelist_map_.find(aggregate);
    assert(it != prefix_to_routelist_map_.end());
    it->second.erase(more_specific);
    return it->second.empty();
}

template <typename T>
void ServiceChain<T>::FillServiceChainInfo(ShowServicechainInfo *info) const {
    if (deleted()) {
        info->set_state("deleted");
    } else if (!IsConnectedRouteValid()) {
        info->set_state("down");
    } else if (!group_oper_state_up()) {
        info->set_state("group down");
    } else {
        info->set_state("active");
    }

    ConnectedRouteInfo connected_rt_info;
    connected_rt_info.set_service_chain_addr(
        service_chain_addr().to_string());
    if (connected_route()) {
        ShowRoute show_route;
        connected_route()->FillRouteInfo(connected_table(), &show_route);
        connected_rt_info.set_connected_rt(show_route);
    }
    info->set_connected_route(connected_rt_info);

    vector<PrefixToRouteListInfo> more_vec;
    for (typename PrefixToRouteListMap::const_iterator it =
         prefix_to_route_list_map()->begin();
        it != prefix_to_route_list_map()->end(); ++it) {
        PrefixToRouteListInfo prefix_list_info;
        prefix_list_info.set_prefix(it->first.ToString());

        BgpTable *bgptable = src_table();
        RouteT rt_key(it->first);
        BgpRoute *aggregate = static_cast<BgpRoute *>(bgptable->Find(&rt_key));
        if (aggregate) {
            prefix_list_info.set_aggregate(true);
            ShowRoute show_route;
            aggregate->FillRouteInfo(bgptable, &show_route);
            prefix_list_info.set_aggregate_rt(show_route);
        } else {
            prefix_list_info.set_aggregate(false);
        }

        vector<string> rt_list;
        for (RouteList::iterator rt_it = it->second.begin();
             rt_it != it->second.end(); ++rt_it) {
            rt_list.push_back((*rt_it)->ToString());
        }
        prefix_list_info.set_more_specific_list(rt_list);
        more_vec.push_back(prefix_list_info);
    }
    info->set_more_specifics(more_vec);

    vector<ExtConnectRouteInfo> ext_connecting_rt_info_list;
    for (ExtConnectRouteList::const_iterator it =
         ext_connecting_routes().begin();
         it != ext_connecting_routes().end(); ++it) {
        ExtConnectRouteInfo ext_rt_info;
        ext_rt_info.set_ext_rt_prefix((*it)->ToString());
        BgpTable *bgptable = src_table();
        RouteT *ext_route = static_cast<RouteT *>(*it);
        RouteT rt_key(ext_route->GetPrefix());
        BgpRoute *ext_connecting =
            static_cast<BgpRoute *>(bgptable->Find(&rt_key));
        if (ext_connecting) {
            ShowRoute show_route;
            ext_connecting->FillRouteInfo(bgptable, &show_route);
            ext_rt_info.set_ext_rt_svc_rt(show_route);
        }
        ext_connecting_rt_info_list.push_back(ext_rt_info);
    }
    info->set_ext_connecting_rt_info_list(ext_connecting_rt_info_list);
    info->set_aggregate_enable(aggregate_enable());
}

ServiceChainGroup::ServiceChainGroup(IServiceChainMgr *manager,
    const string &name)
    : manager_(manager),
      name_(name),
      oper_state_up_(false) {
}

ServiceChainGroup::~ServiceChainGroup() {
    assert(chain_set_.empty());
}

//
// Add a RoutingInstance to this ServiceChainGroup.
// The caller must ensure that multiple bgp::ConfigHelper tasks do not
// invoke this method in parallel.
//
void ServiceChainGroup::AddRoutingInstance(RoutingInstance *rtinstance) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    chain_set_.insert(rtinstance);
    manager_->UpdateServiceChainGroup(this);
}

//
// Delete a RoutingInstance from this ServiceChainGroup.
// The caller must ensure that multiple bgp::ConfigHelper tasks do not
// invoke this method in parallel.
//
void ServiceChainGroup::DeleteRoutingInstance(RoutingInstance *rtinstance) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");
    chain_set_.erase(rtinstance);
    manager_->UpdateServiceChainGroup(this);
}

//
// Update the operational state of this ServiceChainGroup.
// It's considered to be up if all ServiceChains in the ServiceChainGroup
// are up.
// Trigger an update on the operational state of each ServiceChain if the
// operational state of the ServiceChainGroup changed.
//
void ServiceChainGroup::UpdateOperState() {
    bool old_oper_state_up = oper_state_up_;
    oper_state_up_ = true;
    BOOST_FOREACH(RoutingInstance *rtinstance, chain_set_) {
        if (manager_->ServiceChainIsUp(rtinstance))
            continue;
        oper_state_up_ = false;
        break;
    }

    if (old_oper_state_up == oper_state_up_)
        return;

    BOOST_FOREACH(RoutingInstance *rtinstance, chain_set_) {
        manager_->UpdateServiceChain(rtinstance, oper_state_up_);
    }
}

template <typename T>
bool ServiceChainMgr<T>::RequestHandler(ServiceChainRequestT *req) {
    CHECK_CONCURRENCY("bgp::ServiceChain");
    BgpTable *table = NULL;
    BgpRoute *route = NULL;
    PrefixT aggregate_match = req->aggregate_match_;
    ServiceChainT *info = NULL;

    table = req->table_;
    route = req->rt_;
    info = static_cast<ServiceChainT *>(req->info_.get());

    // Table where the aggregate route needs to be added
    aggregate_match = req->aggregate_match_;

    ServiceChainState *state = NULL;
    if (route) {
        state = static_cast<ServiceChainState *>
            (listener_->GetMatchState(table, route, info));
    }

    switch (req->type_) {
        case ServiceChainRequestT::MORE_SPECIFIC_ADD_CHG: {
            assert(state);
            if (state->deleted()) {
                state->reset_deleted();
            }
            if (!info->AddMoreSpecific(aggregate_match, route))
                break;
            if (!info->IsConnectedRouteValid())
                break;
            if (!info->group_oper_state_up())
                break;

            typename ServiceChainT::ConnectedPathIdList path_ids;
            info->UpdateServiceChainRoute(
                aggregate_match, NULL, path_ids, true);
            break;
        }
        case ServiceChainRequestT::MORE_SPECIFIC_DELETE: {
            assert(state);
            if (info->DeleteMoreSpecific(aggregate_match, route)) {
                // Delete the aggregate route
                info->DeleteServiceChainRoute(aggregate_match, true);
            }
            info->RemoveMatchState(route, state);
            break;
        }
        case ServiceChainRequestT::CONNECTED_ROUTE_ADD_CHG: {
            assert(state);
            if (route->IsDeleted() || !route->BestPath() ||
                !route->BestPath()->IsFeasible())  {
                break;
            }
            UpdateServiceChainGroup(info->group());

            if (state->deleted()) {
                state->reset_deleted();
            }

            // Store the old path id list and populate the new one.
            typename ServiceChainT::ConnectedPathIdList path_ids =
                info->GetConnectedPathIds();
            info->SetConnectedRoute(route);

            if (!info->group_oper_state_up())
                break;

            UpdateServiceChainRoutes(info, path_ids);
            break;
        }
        case ServiceChainRequestT::CONNECTED_ROUTE_DELETE: {
            assert(state);
            UpdateServiceChainGroup(info->group());
            DeleteServiceChainRoutes(info);
            info->RemoveMatchState(route, state);
            info->SetConnectedRoute(NULL);
            break;
        }
        case ServiceChainRequestT::EXT_CONNECT_ROUTE_ADD_CHG: {
            assert(state);
            if (state->deleted()) {
                state->reset_deleted();
            }
            info->ext_connecting_routes()->insert(route);
            if (!info->IsConnectedRouteValid())
                break;
            if (!info->group_oper_state_up())
                break;
            RouteT *ext_route = dynamic_cast<RouteT *>(route);
            typename ServiceChainT::ConnectedPathIdList path_ids;
            info->UpdateServiceChainRoute(
                ext_route->GetPrefix(), ext_route, path_ids, false);
            break;
        }
        case ServiceChainRequestT::EXT_CONNECT_ROUTE_DELETE: {
            assert(state);
            if (info->ext_connecting_routes()->erase(route)) {
                RouteT *inet_route = dynamic_cast<RouteT *>(route);
                info->DeleteServiceChainRoute(inet_route->GetPrefix(), false);
            }
            info->RemoveMatchState(route, state);
            break;
        }
        case ServiceChainRequestT::UPDATE_ALL_ROUTES: {
            if (info->dest_table_unregistered())
                break;
            if (info->connected_table_unregistered())
                break;
            if (!info->connected_route())
                break;
            if (!info->group_oper_state_up())
                break;

            typename ServiceChainT::ConnectedPathIdList path_ids =
                info->GetConnectedPathIds();
            UpdateServiceChainRoutes(info, path_ids);
            break;
        }
        case ServiceChainRequestT::DELETE_ALL_ROUTES: {
            DeleteServiceChainRoutes(info);
            break;
        }
        case ServiceChainRequestT::STOP_CHAIN_DONE: {
            if (table == info->connected_table()) {
                info->set_connected_table_unregistered();
                if (!info->num_matchstate()) {
                    listener_->UnregisterMatchCondition(table, info);
                }
            }
            if (table == info->dest_table()) {
                info->set_dest_table_unregistered();
                if (!info->num_matchstate()) {
                    listener_->UnregisterMatchCondition(table, info);
                }
            }
            if (info->unregistered()) {
                chain_set_.erase(info->src_routing_instance());
                StartResolve();
            }
            RetryDelete();
            break;
        }
        default: {
            assert(false);
            break;
        }
    }

    if (state) {
        state->DecrementRefCnt();
        if (state->refcnt() == 0 && state->deleted()) {
            listener_->RemoveMatchState(table, route, info);
            delete state;
            if (!info->num_matchstate()) {
                if (info->dest_table_unregistered()) {
                    listener_->UnregisterMatchCondition(
                        info->dest_table(), info);
                }
                if (info->connected_table_unregistered()) {
                    listener_->UnregisterMatchCondition(
                        info->connected_table(), info);
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

template <typename T>
ServiceChainMgr<T>::ServiceChainMgr(BgpServer *server)
    : server_(server),
      listener_(server_->condition_listener(GetFamily())),
      resolve_trigger_(new TaskTrigger(
          bind(&ServiceChainMgr::ResolvePendingServiceChain, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
      group_trigger_(new TaskTrigger(
          bind(&ServiceChainMgr::ProcessServiceChainGroups, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::ServiceChain"), 0)),
      aggregate_host_route_(false),
      deleter_(new DeleteActor(this)),
      server_delete_ref_(this, server->deleter()) {
    if (service_chain_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        service_chain_task_id_ = scheduler->GetTaskId("bgp::ServiceChain");
    }

    process_queue_.reset(
        new WorkQueue<ServiceChainRequestT *>(service_chain_task_id_, 0,
                     bind(&ServiceChainMgr::RequestHandler, this, _1)));

    id_ = server->routing_instance_mgr()->RegisterInstanceOpCallback(
        bind(&ServiceChainMgr::RoutingInstanceCallback, this, _1, _2));

    BgpMembershipManager *membership_mgr = server->membership_mgr();
    registration_id_ = membership_mgr->RegisterPeerRegistrationCallback(
        bind(&ServiceChainMgr::PeerRegistrationCallback, this, _1, _2, _3));
}

template <typename T>
ServiceChainMgr<T>::~ServiceChainMgr() {
    assert(group_set_.empty());
    assert(group_map_.empty());
}

template <typename T>
void ServiceChainMgr<T>::Terminate() {
    process_queue_->Shutdown();
    RoutingInstanceMgr *ri_mgr = server_->routing_instance_mgr();
    ri_mgr->UnregisterInstanceOpCallback(id_);
    BgpMembershipManager *membership_mgr = server_->membership_mgr();
    membership_mgr->UnregisterPeerRegistrationCallback(registration_id_);
    server_delete_ref_.Reset(NULL);
}

template <typename T>
void ServiceChainMgr<T>::ManagedDelete() {
    deleter_->Delete();
}

template <typename T>
bool ServiceChainMgr<T>::MayDelete() const {
    if (!chain_set_.empty() || !pending_chains_.empty())
        return false;
    if (!group_set_.empty() || !group_map_.empty())
        return false;
    return true;
}

template <typename T>
void ServiceChainMgr<T>::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

template <typename T>
ServiceChainGroup *ServiceChainMgr<T>::FindServiceChainGroup(
    RoutingInstance *rtinstance) {
    if (ServiceChainIsPending(rtinstance)) {
        PendingChainState state = GetPendingServiceChain(rtinstance);
        return state.group;
    } else {
        const ServiceChain<T> *chain = FindServiceChain(rtinstance);
        return (chain ? chain->group() : NULL);
    }
}

template <typename T>
ServiceChainGroup *ServiceChainMgr<T>::FindServiceChainGroup(
    const string &group_name) {
    GroupMap::iterator loc = group_map_.find(group_name);
    return (loc != group_map_.end() ? loc->second : NULL);
}

template <typename T>
ServiceChainGroup *ServiceChainMgr<T>::LocateServiceChainGroup(
    const string &group_name) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    GroupMap::iterator loc = group_map_.find(group_name);
    ServiceChainGroup *group = (loc != group_map_.end()) ? loc->second : NULL;
    if (!group) {
        string temp_group_name(group_name);
        group = new ServiceChainGroup(this, temp_group_name);
        group_map_.insert(temp_group_name, group);
    }
    return group;
}

template <typename T>
void ServiceChainMgr<T>::UpdateServiceChainGroup(ServiceChainGroup *group) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper", "bgp::ServiceChain");

    if (!group)
        return;
    group_set_.insert(group);
    group_trigger_->Set();
}

template <typename T>
bool ServiceChainMgr<T>::ProcessServiceChainGroups() {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    BOOST_FOREACH(ServiceChainGroup *group, group_set_) {
        if (group->empty()) {
            string temp_group_name(group->name());
            group_map_.erase(temp_group_name);
        } else {
            group->UpdateOperState();
        }
    }

    group_set_.clear();
    RetryDelete();
    return true;
}

template <>
Address::Family ServiceChainMgr<ServiceChainInet>::GetFamily() const {
    return Address::INET;
}

template <>
Address::Family ServiceChainMgr<ServiceChainInet6>::GetFamily() const {
    return Address::INET6;
}

template <typename T>
void ServiceChainMgr<T>::Enqueue(ServiceChainRequestT *req) {
    process_queue_->Enqueue(req);
}

template <typename T>
bool ServiceChainMgr<T>::ServiceChainIsPending(RoutingInstance *rtinstance,
    string *reason) const {
    typename PendingChainList::const_iterator loc =
        pending_chains_.find(rtinstance);
    if (loc != pending_chains_.end()) {
        if (reason)
            *reason = loc->second.reason;
        return true;
    }
    return false;
}

template <typename T>
bool ServiceChainMgr<T>::ServiceChainIsUp(RoutingInstance *rtinstance) const {
    if (ServiceChainIsPending(rtinstance))
        return false;
    const ServiceChain<T> *service_chain = FindServiceChain(rtinstance);
    if (!service_chain)
        return false;
    return service_chain->IsConnectedRouteValid();
}

template <typename T>
bool ServiceChainMgr<T>::FillServiceChainInfo(RoutingInstance *rtinstance,
        ShowServicechainInfo *info) const {
    string pending_reason;
    if (ServiceChainIsPending(rtinstance, &pending_reason)) {
        info->set_state("pending");
        info->set_pending_reason(pending_reason);
        return true;
    }
    const ServiceChain<T> *service_chain = FindServiceChain(rtinstance);
    if (!service_chain)
        return false;
    service_chain->FillServiceChainInfo(info);
    return true;
}



template <typename T>
bool ServiceChainMgr<T>::LocateServiceChain(RoutingInstance *rtinstance,
    const ServiceChainConfig &config) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    // Verify whether the entry already exists.
    tbb::mutex::scoped_lock lock(mutex_);
    ServiceChainMap::iterator it = chain_set_.find(rtinstance);
    if (it != chain_set_.end()) {
        ServiceChainT *chain = static_cast<ServiceChainT *>(it->second.get());
        if (chain->CompareServiceChainConfig(config)) {
            BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                "No update in ServiceChain config : " << rtinstance->name());
            return true;
        }

        ServiceChainGroup *group = chain->group();
        if (group) {
            group->DeleteRoutingInstance(rtinstance);
            chain->clear_group();
        }

        // Update of ServiceChainConfig.
        // Add the routing instance to pending list so that service chain is
        // created after stop done callback for the current incarnation gets
        // invoked.
        if (config.service_chain_id.empty()) {
            group = NULL;
        } else {
            group = LocateServiceChainGroup(config.service_chain_id);
            group->AddRoutingInstance(rtinstance);
        }
        string reason = "Waiting for deletion of previous incarnation";
        AddPendingServiceChain(rtinstance, group, reason);

        // Wait for the delete complete callback.
        if (chain->deleted())
            return false;

        BgpConditionListener::RequestDoneCb cb =
            bind(&ServiceChainMgr::StopServiceChainDone, this, _1, _2);
        listener_->RemoveMatchCondition(chain->dest_table(), chain, cb);
        listener_->RemoveMatchCondition(chain->connected_table(), chain, cb);
        return true;
    }

    RoutingInstanceMgr *mgr = server_->routing_instance_mgr();
    RoutingInstance *dest = mgr->GetRoutingInstance(config.routing_instance);

    // Dissociate from the old ServiceChainGroup.
    ServiceChainGroup *group = FindServiceChainGroup(rtinstance);
    if (group)
        group->DeleteRoutingInstance(rtinstance);

    // Delete from the pending list. The instance would already have been
    // removed from the pending list if this method is called when trying
    // to resolve items in the pending list.  However, if this method is
    // called when processing a change in the service chain config, then
    // we may need to remove it from the pending list.
    DeletePendingServiceChain(rtinstance);

    // Locate the new ServiceChainGroup.
    if (config.service_chain_id.empty()) {
        group = NULL;
    } else {
        group = LocateServiceChainGroup(config.service_chain_id);
        group->AddRoutingInstance(rtinstance);
    }

    // Destination routing instance does not exist.
    if (!dest) {
        string reason = "Destination routing instance does not exist";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    // Destination routing instance is being deleted.
    if (dest->deleted()) {
        string reason = "Destination routing instance is being deleted";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    // Destination virtual network index is unknown.
    if (!dest->virtual_network_index()) {
        string reason = "Destination virtual network index is unknown";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    RoutingInstance *connected_ri = NULL;
    if (config.source_routing_instance == "") {
        connected_ri = rtinstance;
        assert(!rtinstance->deleted());
    } else {
        connected_ri = mgr->GetRoutingInstance(config.source_routing_instance);
    }

    // Connected routing instance does not exist.
    if (!connected_ri) {
        string reason = "Connected routing instance does not exist";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    // Connected routing instance is being deleted.
    if (connected_ri->deleted()) {
        string reason = "Connected routing instance is being deleted";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    // Add to pending queue if the service chain address is invalid.
    error_code ec;
    AddressT chain_addr =
        AddressT::from_string(config.service_chain_address, ec);
    if (ec != 0) {
        string reason = "Service chain address is invalid";
        AddPendingServiceChain(rtinstance, group, reason);
        return false;
    }

    // Get the BGP Tables to add condition
    BgpTable *connected_table = connected_ri->GetTable(GetFamily());
    assert(connected_table);
    BgpTable *dest_table = dest->GetTable(GetFamily());
    assert(dest_table);

    // Allocate the new service chain.
    ServiceChainPtr chain = ServiceChainPtr(new ServiceChainT(this, group,
        rtinstance, dest, connected_ri, config.prefix, chain_addr));

    if (aggregate_host_route()) {
        ServiceChainT *obj = static_cast<ServiceChainT *>(chain.get());
        obj->set_aggregate_enable();
    }

    // Add the new service chain request
    chain_set_.insert(make_pair(rtinstance, chain));
    listener_->AddMatchCondition(
        connected_table, chain.get(), BgpConditionListener::RequestDoneCb());
    listener_->AddMatchCondition(
        dest_table, chain.get(), BgpConditionListener::RequestDoneCb());

    return true;
}

template <typename T>
ServiceChain<T> *ServiceChainMgr<T>::FindServiceChain(
        const string &instance) const {
    RoutingInstance *rtinstance =
        server_->routing_instance_mgr()->GetRoutingInstance(instance);
    if (!rtinstance)
        return NULL;
    ServiceChainMap::const_iterator it = chain_set_.find(rtinstance);
    if (it == chain_set_.end())
        return NULL;
    ServiceChainT *chain = static_cast<ServiceChainT *>(it->second.get());
    return chain;
}

template <typename T>
ServiceChain<T> *ServiceChainMgr<T>::FindServiceChain(
    RoutingInstance *rtinstance) const {
    ServiceChainMap::const_iterator it = chain_set_.find(rtinstance);
    if (it == chain_set_.end())
        return NULL;
    ServiceChainT *chain = static_cast<ServiceChainT *>(it->second.get());
    return chain;
}

template <typename T>
bool ServiceChainMgr<T>::ResolvePendingServiceChain() {
    CHECK_CONCURRENCY("bgp::Config");
    for (typename PendingChainList::iterator it = pending_chains_.begin(), next;
         it != pending_chains_.end(); it = next) {
        next = it;
        ++next;
        RoutingInstance *rtinstance = it->first;
        ServiceChainGroup *group = it->second.group;
        if (group)
            group->DeleteRoutingInstance(rtinstance);
        pending_chains_.erase(it);
        const ServiceChainConfig *sc_config =
            rtinstance->config()->service_chain_info(GetFamily());
        if (sc_config)
            LocateServiceChain(rtinstance, *sc_config);
    }
    RetryDelete();
    return true;
}

template <typename T>
void ServiceChainMgr<T>::RoutingInstanceCallback(string name, int op) {
    if (op != RoutingInstanceMgr::INSTANCE_DELETE)
        StartResolve();
}

template <typename T>
void ServiceChainMgr<T>::StartResolve() {
    if (pending_chains_.empty())
        return;
    resolve_trigger_->Set();
}

template <typename T>
void ServiceChainMgr<T>::StopServiceChainDone(BgpTable *table,
                                           ConditionMatch *info) {
    // Post the RequestDone event to ServiceChain task to take Action
    ServiceChainRequestT *req =
        new ServiceChainRequestT(ServiceChainRequestT::STOP_CHAIN_DONE, table,
                                NULL, PrefixT(), ServiceChainPtr(info));
    Enqueue(req);
    return;
}

template <typename T>
void ServiceChainMgr<T>::StopServiceChain(RoutingInstance *rtinstance) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    // Remove the routing instance from pending chains list.
    tbb::mutex::scoped_lock lock(mutex_);
    if (ServiceChainIsPending(rtinstance)) {
        ServiceChainGroup *group = FindServiceChainGroup(rtinstance);
        if (group)
            group->DeleteRoutingInstance(rtinstance);
        DeletePendingServiceChain(rtinstance);
        RetryDelete();
    }

    ServiceChainT *chain = FindServiceChain(rtinstance);
    if (!chain)
        return;

    ServiceChainGroup *group = chain->group();
    if (group) {
        group->DeleteRoutingInstance(rtinstance);
        chain->clear_group();
    }

    if (chain->deleted())
        return;

    BgpConditionListener::RequestDoneCb cb =
        bind(&ServiceChainMgr::StopServiceChainDone, this, _1, _2);
    listener_->RemoveMatchCondition(chain->dest_table(), chain, cb);
    listener_->RemoveMatchCondition(chain->connected_table(), chain, cb);
}

template <typename T>
void ServiceChainMgr<T>::UpdateServiceChain(RoutingInstance *rtinstance,
    bool group_oper_state_up) {
    // Bail if there's no service chain for the instance.
    ServiceChainT *chain = FindServiceChain(rtinstance);
    if (!chain)
        return;

    // Update the state in the service chain.
    chain->set_group_oper_state_up(group_oper_state_up);

    // Post event to ServiceChain task to update/delete all routes.
    typename ServiceChainRequestT::RequestType req_type;
    if (group_oper_state_up) {
        req_type = ServiceChainRequestT::UPDATE_ALL_ROUTES;
    } else {
        req_type = ServiceChainRequestT::DELETE_ALL_ROUTES;
    }
    ServiceChainRequestT *req = new ServiceChainRequestT(
        req_type, NULL, NULL, PrefixT(), ServiceChainPtr(chain));
    Enqueue(req);
}

template <typename T>
void ServiceChainMgr<T>::UpdateServiceChainRoutes(ServiceChainT *chain,
    const typename ServiceChainT::ConnectedPathIdList &old_path_ids) {
    // Update ServiceChain routes for aggregates.
    typename ServiceChainT::PrefixToRouteListMap *vn_prefix_list =
        chain->prefix_to_route_list_map();
    for (typename ServiceChainT::PrefixToRouteListMap::iterator it =
        vn_prefix_list->begin(); it != vn_prefix_list->end(); ++it) {
        if (!it->second.empty())
            chain->UpdateServiceChainRoute(it->first, NULL, old_path_ids, true);
    }

    // Update ServiceChain routes for external connecting routes.
    for (typename ServiceChainT::ExtConnectRouteList::iterator it =
        chain->ext_connecting_routes()->begin();
        it != chain->ext_connecting_routes()->end(); ++it) {
        RouteT *ext_route = static_cast<RouteT *>(*it);
        chain->UpdateServiceChainRoute(
            ext_route->GetPrefix(), ext_route, old_path_ids, false);
    }
}

template <typename T>
void ServiceChainMgr<T>::DeleteServiceChainRoutes(ServiceChainT *chain) {
    // Delete ServiceChain routes for aggregates.
    typename ServiceChainT::PrefixToRouteListMap *vn_prefix_list =
        chain->prefix_to_route_list_map();
    for (typename ServiceChainT::PrefixToRouteListMap::iterator it =
        vn_prefix_list->begin(); it != vn_prefix_list->end(); ++it) {
        chain->DeleteServiceChainRoute(it->first, true);
    }

    // Delete ServiceChain routes for external connecting routes.
    for (typename ServiceChainT::ExtConnectRouteList::iterator it =
        chain->ext_connecting_routes()->begin();
        it != chain->ext_connecting_routes()->end(); ++it) {
        RouteT *ext_route = static_cast<RouteT *>(*it);
        chain->DeleteServiceChainRoute(ext_route->GetPrefix(), false);
    }
}

template <typename T>
void ServiceChainMgr<T>::PeerRegistrationCallback(IPeer *peer, BgpTable *table,
                                               bool unregister) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    // Bail if it's not an XMPP peer.
    if (!peer->IsXmppPeer())
        return;

    // Bail if there's no service chain for the instance.
    ServiceChainT *chain = FindServiceChain(table->routing_instance());
    if (!chain)
        return;

    // Post event to ServiceChain task to update all routes.
    ServiceChainRequestT *req =
        new ServiceChainRequestT(ServiceChainRequestT::UPDATE_ALL_ROUTES, NULL,
                                NULL, PrefixT(), ServiceChainPtr(chain));
    Enqueue(req);
}

template <typename T>
void ServiceChainMgr<T>::DisableResolveTrigger() {
    resolve_trigger_->set_disable();
}

template <typename T>
void ServiceChainMgr<T>::EnableResolveTrigger() {
    resolve_trigger_->set_enable();
}

template <typename T>
void ServiceChainMgr<T>::DisableGroupTrigger() {
    group_trigger_->set_disable();
}

template <typename T>
void ServiceChainMgr<T>::EnableGroupTrigger() {
    group_trigger_->set_enable();
}

template <typename T>
uint32_t ServiceChainMgr<T>::GetDownServiceChainCount() const {
    uint32_t count = 0;
    for (ServiceChainMap::const_iterator it = chain_set_.begin();
         it != chain_set_.end(); ++it) {
        const ServiceChainT *chain =
             static_cast<const ServiceChainT *>(it->second.get());
        if (!chain->IsConnectedRouteValid())
            count++;
    }
    return count;
}

// Explicit instantiation of ServiceChainMgr for INET and INET6.
template class ServiceChainMgr<ServiceChainInet>;
template class ServiceChainMgr<ServiceChainInet6>;
