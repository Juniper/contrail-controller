/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/service_chaining.h"

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>

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
#include "bgp/tunnel_encap/tunnel_encap.h"
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
template<>
int ServiceChainMgr<ServiceChainEvpn>::service_chain_task_id_ = -1;
template<>
int ServiceChainMgr<ServiceChainEvpn6>::service_chain_task_id_ = -1;

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

/**
  * Replicate prefix received at head-end of service chain to appropriate
  * table depending on address-family.
  * ----------------------------------------------------------------------
  *      Family       service-chain address-family     Replication Table
  * ----------------------------------------------------------------------
  *      EVPN                  INET                      InetTable
  *      EVPN                  INET6                     Inet6Table
  *      INET                  INET                      EvpnTable
  *      INET6                 INET6                     EvpnTable
  * ----------------------------------------------------------------------
  * @param partition           -   Reference to DBTablePartition where the
  *                                service-chain route is to be replicated
  * @param route               -   Reference to service-chain route
  * @param table               -   Reference to BgpTable where service-chain
  *                                route is to be replicated
  * @param prefix              -   Prefix
  * @param create              -   when "true" indicates that route should be
  *                                created if not found and
  */
template<typename T>
void ServiceChain<T>::GetReplicationFamilyInfo(DBTablePartition  *&partition,
    BgpRoute *&route, BgpTable *&table, PrefixT prefix, bool create) {
    /**
      * EVPN prefix received at head end of service-chain.
      * Replicate route to <vrf>.inet[6] table depending on
      * whether the EVPN prefix carries inet[6] traffic.
      */
    RoutingInstance *src_ri = src_routing_instance();
    IpAddress addr = prefix.addr();
    int plen = prefix.prefixlen();
    if (GetSCFamily() == SCAddress::EVPN) {
        /**
         * EVPN route carrying inet prefix
         */
        table = src_ri->GetTable(Address::INET);
        Ip4Prefix inet_prefix = Ip4Prefix(addr.to_v4(), plen);
        InetRoute inet_route(inet_prefix);
        partition = static_cast<DBTablePartition *>
                        (table->GetTablePartition(&inet_route));
        route = static_cast<BgpRoute *>(partition->Find(&inet_route));
        if (create) {
            /**
             * Create case. Create route if not found.
             */
            if (route == NULL) {
                route = new InetRoute(inet_prefix);
                partition->Add(route);
            } else {
                route->ClearDelete();
            }
        }
    } else if (GetSCFamily() == SCAddress::EVPN6) {
        /**
         * EVPN route carrying inet6 prefix
         */
        table = src_ri->GetTable(Address::INET6);
        Inet6Prefix inet6_prefix = Inet6Prefix(addr.to_v6(), plen);
        Inet6Route inet6_route(inet6_prefix);
        partition = static_cast<DBTablePartition *>
                        (table->GetTablePartition(&inet6_route));
        route = static_cast<BgpRoute *>(partition->Find(&inet6_route));
        if (create) {
            /**
             * Create case. Create route if not found.
             */
            if (route == NULL) {
                route = new Inet6Route(inet6_prefix);
                partition->Add(route);
            } else {
                route->ClearDelete();
            }
        }
    } else {
        /**
          * INET/INET6 prefix at head end of service-chain.
          * Replicate route to <vrf>.evpn.0 table.
          */
        table = src_ri->GetTable(Address::EVPN);
        string type_rd_tag("5-0:0-0-");
        string prefix_str = type_rd_tag + addr.to_string() + "/" +
                            boost::lexical_cast<std::string>(plen);
        EvpnPrefix evpn_prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute evpn_route(evpn_prefix);
        partition = static_cast<DBTablePartition *>
                        (table->GetTablePartition(&evpn_route));
        route = static_cast<BgpRoute *>(partition->Find(&evpn_route));
        if (create) {
            /**
             * Create case. Create route if not found.
             */
            if (route == NULL) {
                route = new EvpnRoute(evpn_prefix);
                partition->Add(route);
            } else {
                route->ClearDelete();
            }
        }
    }
}

/**
  * Process service-chain path and add to service-chain route if needed.
  *
  * @param path_id    -  PathID
  * @param path       -  Path to be processed
  * @param attr       -  Path attribute
  * @param route      -  Reference to Service-Chain route
  * @param partition  -  Reference to DBTablePartition where the
  *                      service-chain route is to be replicated
  * @param aggregate  -  "true" indicates aggregate route
  * @param bgptable   -  BgpTable the service-chain route belongs to
  */
template<typename T>
void ServiceChain<T>::ProcessServiceChainPath(uint32_t path_id, BgpPath *path,
    BgpAttrPtr attr, BgpRoute *&route, DBTablePartition *&partition,
    bool aggregate, BgpTable *bgptable) {
    BgpPath *existing_path =
            route->FindPath(BgpPath::ServiceChain, NULL, path_id);
    uint32_t label = path->GetLabel();
    bool path_updated = false;

    /**
      * If inserting into EVPN table, the label should the vxlan_id of
      * the connected RI if non-zero and if not, the VNI.
      */
    if (bgptable->family() == Address::EVPN) {
        const RoutingInstance *conn_ri =
            bgptable->server()->routing_instance_mgr()->GetRoutingInstance(
                RoutingInstanceMgr::GetPrimaryRoutingInstanceName(
                    connected_->name()));
        if (!conn_ri) {
            // conn_ri is not expected to be found only in unit tests.
            assert(bgp_log_test::unit_test());
            conn_ri = connected_routing_instance();
        }
        label = conn_ri->vxlan_id();
        if (!label) {
            label = conn_ri->virtual_network_index();
        }
    }

    /**
     * Check if there is an existing path that can be reused.
     */
    if (existing_path != NULL) {
        // Existing path can be reused.
        if ((attr.get() == existing_path->GetAttr()) &&
            (path->GetLabel() == existing_path->GetLabel()) &&
            (path->GetFlags() == existing_path->GetFlags())) {
            return;
        }

        /**
         * Remove existing path, new path will be added below.
         */
        path_updated = true;
        route->RemovePath(BgpPath::ServiceChain, NULL, path_id);
    }

    /**
     * No existing path or un-usable existing path
     * Create new path and insert into service-chain route
     */
    BgpPath *new_path =
        new BgpPath(path_id, BgpPath::ServiceChain, attr.get(),
                    path->GetFlags(), label);
    route->InsertPath(new_path);
    partition->Notify(route);

    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        (path_updated ? "Updated " : "Added ") <<
        (aggregate ? "Aggregate" : "ExtConnected") <<
        " ServiceChain path " << route->ToString() <<
        " path_id " << BgpPath::PathIdString(path_id) <<
        " in table " << bgptable->name() <<
        " .Path label: " << label);
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
    RoutingInstance *connected, const vector<string> &subnets, AddressT addr,
    bool head, bool retain_as_path)
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
      sc_head_(head),
      retain_as_path_(retain_as_path),
      src_table_delete_ref_(this, src_table()->deleter()),
      dest_table_delete_ref_(this, dest_table()->deleter()),
      connected_table_delete_ref_(this, connected_table()->deleter()) {
    for (vector<string>::const_iterator it = subnets.begin();
         it != subnets.end(); ++it) {
        string prefix = *it;
        error_code ec;
        /**
          * For EVPN, need to construct EVPN prefix from IPv4 or IPv6 subnet.
          * Make sure AF in prefix matches service_chain AF.
          */
        if (GetFamily() == Address::EVPN) {
            prefix = "5-0:0-0-" + prefix;
            EvpnPrefix subnet = EvpnPrefix::FromString(prefix, &ec);
            if (GetSCFamily() == SCAddress::EVPN) {
                if (subnet.family() == Address::INET6) {
                    continue;
                }
            } else {
                if (subnet.family() != Address::INET6) {
                    continue;
                }
            }
        }
        PrefixT ipam_subnet = PrefixT::FromString(prefix, &ec);
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
    return connected_->GetTable(GetConnectedFamily());
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
        // For EVPN service-chaining, we are only interested in Type 5 routes
        // from the destination table. Ignore any other route.
        if (GetFamily() == Address::EVPN) {
            if (!IsEvpnType5Route(route)) {
                return (false);
            }
        }

        // Skip connected routes
        if (IsConnectedRoute(route)) {
            return false;
        }

        // Skip aggregate routes
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

                    const OriginVnPath *ovnpath =
                        attr ? attr->origin_vn_path() : NULL;
                    if (ovnpath && ovnpath->Contains(
                                server->autonomous_system(), src_vn_index)) {
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
               IsConnectedRoute(route, true)) {
        // Connected routes from source table
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

    BgpConditionListener *listener = manager_->GetListener();
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

/**
 * Check if route belongs to connected table.
 * For EVPN, connected routes could belong to INET or INET6 AF.
 * For routes in the destination table we can use template AF.
 */
template <typename T>
bool ServiceChain<T>::IsConnectedRoute(BgpRoute *route,
                                       bool is_conn_table) const {
    if (is_conn_table && GetFamily() == Address::EVPN) {
        if (GetConnectedFamily() == Address::INET) {
            InetRoute *inet_route = dynamic_cast<InetRoute *>(route);
            return (service_chain_addr() == inet_route->GetPrefix().addr());
        } else {
            Inet6Route *inet6_route = dynamic_cast<Inet6Route *>(route);
            return (service_chain_addr() == inet6_route->GetPrefix().addr());
        }
    } else {
        /**
         * Non-EVPN and EVPN destination table case
         */
        RouteT *ip_route = dynamic_cast<RouteT *>(route);
        return (service_chain_addr() == ip_route->GetPrefix().addr());
    }
}

/**
 * Check if EVPN route being re-originated is Type 5
 * Also make sure that the address-family of the prefix carried in the EVPN
 * route matches the service-chain family. This will avoid the routes from
 * being re-originated twice.
 */
template <typename T>
bool ServiceChain<T>::IsEvpnType5Route(BgpRoute *route) const {
    if (GetFamily() != Address::EVPN) {
        return false;
    }

    EvpnRoute *evpn_route = static_cast<EvpnRoute *>(route);
    EvpnPrefix prefix = evpn_route->GetPrefix();
    if (prefix.type() != EvpnPrefix::IpPrefixRoute) {
        return false;
    }
    if (GetSCFamily() == SCAddress::EVPN &&
        prefix.family() == Address::INET6) {
       return false;
    }
    if (GetSCFamily() == SCAddress::EVPN6 &&
        prefix.family() == Address::INET) {
       return false;
    }
    return true;
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

/*
 * To support BMS to VM service-chaining, we could have traffic being
 * chained between different address-families. This entails the need for
 * replicating the service-chain route across address-families.
 * The different possibilities are listed below.
 *                    ---------------------------------------------------
 *                    |             service-chain info                  |
 * ----------------------------------------------------------------------
 *  Traffic direction | Destination AF    Source AF   Replication Table |
 * ----------------------------------------------------------------------
 *    VM(v4) --> BMS  |   EVPN             INET        InetTable        |
 *    VM(v6) --> BMS  |   EVPN             INET6       Inet6Table       |
 *    BMS --> VM (v4) |   INET             INET        EvpnTable        |
 *    BMS --> VM (v6) |   INET6            INET6       EvpnTable        |
 * ----------------------------------------------------------------------
 *
 * This is only done at the RI belonging to the head SI in the service
 * chain. At the RIs belonging to other SIs in the chain, we always
 * install the route only in the INET or INET6 table and not in the EVPN
 * table. This is because, at the RI belonging to the first SI in the
 * service-chain, we may need to originate a Type 5 route if a BMS happens
 * to be connected to it.
 */
template <typename T>
void ServiceChain<T>::DeleteServiceChainRoute(PrefixT prefix, bool aggregate) {

    CHECK_CONCURRENCY("bgp::ServiceChain");

    /*
     * For deletion within the same AF.
     * At the RIs belonging to SIs NOT at the head of the service-chain, do
     * not need to delete EVPN SC routes since only INET or INET6 routes
     * would have been installed.
     */
    if (is_sc_head() || GetFamily() != Address::EVPN) {
        BgpTable *bgptable = src_table();
        RouteT rt_key(prefix);
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(bgptable->
                GetTablePartition(&rt_key));
        BgpRoute *service_chain_route =
            static_cast<BgpRoute *>(partition->Find(&rt_key));

        if (service_chain_route && !service_chain_route->IsDeleted()) {
            DeleteServiceChainRouteInternal(service_chain_route, partition,
                                            bgptable, aggregate);
        }
    }

    /*
     * For deletion from replication table.
     * At the RIs belonging to SIs NOT at the head of the service-chain, do
     * not need to delete SC routes in the EVPN table since they would have
     * been installed only in the INET or INET6 table..
     */
    if (is_sc_head() || GetFamily() == Address::EVPN) {
        BgpTable *repl_table;
        BgpRoute *repl_sc_route;
        DBTablePartition *repl_partition;
        GetReplicationFamilyInfo(repl_partition, repl_sc_route, repl_table,
                             prefix, false);
        if (repl_sc_route && !repl_sc_route->IsDeleted()) {
            DeleteServiceChainRouteInternal(repl_sc_route, repl_partition,
                                            repl_table, aggregate);
        }
    }
}

template <typename T>
void ServiceChain<T>::DeleteServiceChainRouteInternal(
                                     BgpRoute          *service_chain_route,
                                     DBTablePartition  *partition,
                                     BgpTable          *bgptable,
                                     bool              aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

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

/*
 * To support BMS to VM service-chaining, we could have traffic being
 * chained between different address-families. This entails the need for
 * replicating the service-chain route across address-families.
 * The different possibilities are listed below.
 *                    ---------------------------------------------------
 *                    |             service-chain info                  |
 * ----------------------------------------------------------------------
 *  Traffic direction | Destination AF    Source AF   Replication Table |
 * ----------------------------------------------------------------------
 *    VM(v4) --> BMS  |   EVPN             INET        InetTable        |
 *    VM(v6) --> BMS  |   EVPN             INET6       Inet6Table       |
 *    BMS --> VM (v4) |   INET             INET        EvpnTable        |
 *    BMS --> VM (v6) |   INET6            INET6       EvpnTable        |
 * ----------------------------------------------------------------------
 *
 * This is only done at the RI belonging to the head SI in the service
 * chain. At the RIs belonging to other SIs in the chain, we always
 * install the route only in the INET or INET6 table and not in the EVPN
 * table. This is because, at the RI belonging to the first SI in the
 * service-chain, we may need to originate a Type 5 route if a BMS happens
 * to be connected to it.
 */
template <typename T>
void ServiceChain<T>::UpdateServiceChainRoute(PrefixT prefix,
    const RouteT *orig_route, const ConnectedPathIdList &old_path_ids,
    bool aggregate) {

    CHECK_CONCURRENCY("bgp::ServiceChain");

    /*
     * For re-origination within the same AF.
     * At the RIs belonging to SIs NOT at the head of the service-chain, do
     * not install EVPN SC routes. Only install INET or INET6 routes.
     */
    if (is_sc_head() || GetFamily() != Address::EVPN) {
        BgpTable *bgptable = src_table();
        RouteT rt_key(prefix);
        DBTablePartition *partition =
            static_cast<DBTablePartition *>
                (bgptable->GetTablePartition(&rt_key));
        BgpRoute *service_chain_route =
            static_cast<BgpRoute *>(partition->Find(&rt_key));

        if (service_chain_route == NULL) {
            service_chain_route = new RouteT(prefix);
            partition->Add(service_chain_route);
        } else {
            service_chain_route->ClearDelete();
        }

        UpdateServiceChainRouteInternal(orig_route, old_path_ids,
                                        service_chain_route, partition,
                                        bgptable, aggregate);
    }

    /*
     * For re-origination to replication table.
     * At the RIs belonging to SIs NOT at the head of the service-chain, do
     * not install SC routes in the EVPN table. Only install INET or INET6
     * routes.
     */
    if (is_sc_head() || GetFamily() == Address::EVPN) {
        BgpTable *repl_table;
        BgpRoute *repl_sc_route;
        DBTablePartition *repl_partition;
        GetReplicationFamilyInfo(repl_partition, repl_sc_route, repl_table,
                                 prefix, true);
        UpdateServiceChainRouteInternal(orig_route, old_path_ids,
                                        repl_sc_route, repl_partition,
                                        repl_table, aggregate);
   }
}

template <typename T>
void ServiceChain<T>::UpdateServiceChainRouteInternal(const RouteT *orig_route,
    const ConnectedPathIdList &old_path_ids, BgpRoute *service_chain_route,
    DBTablePartition *partition, BgpTable *bgptable, bool aggregate) {
    CHECK_CONCURRENCY("bgp::ServiceChain");

    int vn_index = dest_routing_instance()->virtual_network_index();
    BgpServer *server = dest_routing_instance()->server();
    OriginVn origin_vn(server->autonomous_system(), vn_index);
    OriginVn origin_vn4(server->autonomous_system(), AS_TRANS);
    OriginVn origin_vn_trans(AS_TRANS, vn_index);
    const OriginVnPath::OriginVnValue origin_vn_bytes = origin_vn.GetExtCommunity();
    const OriginVnPath::OriginVnValue origin_vn_trans_bytes =
              origin_vn_trans.GetExtCommunity();
    const OriginVnPath::OriginVnValue origin_vn4_bytes =
              origin_vn4.GetExtCommunity();

    SiteOfOrigin soo;
    ExtCommunity::ExtCommunityList sgid_list;
    ExtCommunity::ExtCommunityList tag_list;
    LoadBalance load_balance;
    bool load_balance_present = false;
    const Community *orig_community = NULL;
    const OriginVnPath *orig_ovnpath = NULL;
    const AsPath *orig_aspath = NULL;
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
            orig_aspath = orig_attr->as_path();
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
    OriginVnPathPtr new_ovnpath;
    if (server->autonomous_system() > AS2_MAX && vn_index > 0xffff) {
        new_ovnpath = ovnpath_db->PrependAndLocate(orig_ovnpath,
                                                   origin_vn4_bytes);
        new_ovnpath = ovnpath_db->PrependAndLocate(new_ovnpath.get(),
                                                   origin_vn_trans_bytes);
    } else {
        new_ovnpath = ovnpath_db->PrependAndLocate(
                                  orig_ovnpath, origin_vn_bytes);
    }

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

        // Add the export route target list from the source routing instance
        // when inserting into EVPN table. This is required for the case when
        // service-chain routes are replicated to the BGP table to be used on
        // the BMS. The TOR switch does not import the cooked-up RT of the
        // service-RI. It only imports the RTs of the primary RI. Hence, we
        // add the primary RIs RTs to the route.
        // NOTE: There is an assumption that connected_ri on the head SI
        // will always point to the primary RI. Need to change that if the
        // assumption is not true.
        // Also, we pick only the export route targets in the range used by
        // schema transformer for non user-configured RTs.
        if (is_sc_head() && bgptable->family() == Address::EVPN) {
            ExtCommunity::ExtCommunityList export_list;
            const RoutingInstance *conn_ri =
                server->routing_instance_mgr()->GetRoutingInstance(
                    RoutingInstanceMgr::GetPrimaryRoutingInstanceName(
                        connected_->name()));
            if (!conn_ri) {
                // conn_ri is not expected to be found only in unit tests.
                assert(bgp_log_test::unit_test());
                conn_ri = connected_routing_instance();
            }
            BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                "Adding primary RI " << conn_ri->name() << " route targets " <<
                "to service-chain route for EVPN table " << bgptable->name());
            BOOST_FOREACH(const RouteTarget &rtarget,
                          conn_ri->GetExportList()) {
                if (ExtCommunity::get_rtarget_val(
                    rtarget.GetExtCommunity()) != 0) {
                    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG,
                        BGP_LOG_FLAG_TRACE, "RT value " << rtarget.ToString());
                    export_list.push_back(rtarget.GetExtCommunity());
                }
            }
            new_ext_community = extcomm_db->AppendAndLocate(
                new_ext_community.get(), export_list);
        }

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
        if (server->autonomous_system() > AS2_MAX && vn_index > 0xffff) {
            new_ext_community = extcomm_db->ReplaceOriginVnAndLocate(
                                new_ext_community.get(), origin_vn4_bytes);
            new_ext_community = extcomm_db->AppendAndLocate(
                                new_ext_community.get(), origin_vn_trans_bytes);
        } else {
            new_ext_community = extcomm_db->ReplaceOriginVnAndLocate(
                                new_ext_community.get(), origin_vn_bytes);
        }

        // Connected routes always have mpls (udp or gre)  as encap.
        // If updating service-chain route in the EVPN table, change
        // tunnel encap to include VxLAN and MPLS. BMS only supports VxLAN.
        // Vrouter has the choice of using VxLAN or MPLS based on config.
        if (is_sc_head() && bgptable->family() == Address::EVPN) {
            ExtCommunity::ExtCommunityList encaps_list;
            vector<string> tunnel_encaps = boost::assign::list_of("vxlan");
            BOOST_FOREACH(string encap, tunnel_encaps) {
                encaps_list.push_back(TunnelEncap(encap).GetExtCommunity());
            }
            new_ext_community = extcomm_db->
                ReplaceTunnelEncapsulationAndLocate(new_ext_community.get(),
                                                    encaps_list);
        }

        // Replace extended community, community and origin vn path.
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            attr, new_ext_community);
        new_attr =
            attr_db->ReplaceCommunityAndLocate(new_attr.get(), new_community);
        new_attr = attr_db->ReplaceOriginVnPathAndLocate(new_attr.get(),
            new_ovnpath);

        // Strip as_path if needed. This is required when the connected route is
        // learnt via BGP. If retain_as_path knob is configured replace the
        // AsPath with the value from the original route.
        if (retain_as_path() && orig_aspath) {
            new_attr = attr_db->ReplaceAsPathAndLocate(new_attr.get(),
                                                       orig_aspath);
        } else {
            new_attr = attr_db->ReplaceAsPathAndLocate(new_attr.get(),
                                                       AsPathPtr());
        }

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
        ProcessServiceChainPath(path_id, connected_path, new_attr,
                                service_chain_route, partition,
                                aggregate, bgptable);
        new_path_ids.insert(path_id);
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

/**
  * Get appropriate listener for watching routes.
  * For EVPN connected table, listener will depend on the connected AF
  *
  * @param addr          -  service-chain address
  * @param is_conn_table -  "true" indicates connected table
  * @return              -  Pointer to BgpConditionListener object
  */
template <typename T>
BgpConditionListener *ServiceChainMgr<T>::GetListener() {
    return server_->condition_listener(GetSCFamily());
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
      listener_(GetListener()),
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

/**
 * Address Family - used for GetTable().
 */
template <>
Address::Family ServiceChainMgr<ServiceChainInet>::GetFamily() const {
    return Address::INET;
}

template <>
Address::Family ServiceChainMgr<ServiceChainInet6>::GetFamily() const {
    return Address::INET6;
}

template <>
Address::Family ServiceChainMgr<ServiceChainEvpn>::GetFamily() const {
    return Address::EVPN;
}

template <>
Address::Family ServiceChainMgr<ServiceChainEvpn6>::GetFamily() const {
    return Address::EVPN;
}

/**
 * Connected Table Family. For EVPN, it is INET or INET6.
 */
template <>
Address::Family ServiceChainMgr<ServiceChainInet>::GetConnectedFamily() const {
    return Address::INET;
}

template <>
Address::Family ServiceChainMgr<ServiceChainInet6>::GetConnectedFamily() const {
    return Address::INET6;
}

template <>
Address::Family ServiceChainMgr<ServiceChainEvpn>::GetConnectedFamily() const {
    return Address::INET;
}

template <>
Address::Family ServiceChainMgr<ServiceChainEvpn6>::GetConnectedFamily() const {
    return Address::INET6;
}

/**
 * Service Chain Family
 */
template <>
SCAddress::Family ServiceChainMgr<ServiceChainInet>::GetSCFamily() const {
    return SCAddress::INET;
}

template <>
SCAddress::Family ServiceChainMgr<ServiceChainInet6>::GetSCFamily() const {
    return SCAddress::INET6;
}

template <>
SCAddress::Family ServiceChainMgr<ServiceChainEvpn>::GetSCFamily() const {
    return SCAddress::EVPN;
}

template <>
SCAddress::Family ServiceChainMgr<ServiceChainEvpn6>::GetSCFamily() const {
    return SCAddress::EVPN6;
}

template <>
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

    /**
     * Get the BGP Tables to add condition
     * For EVPN, connected table will be INET/INET6 depending on
     * whether prefix carried is v4/v6.
     */
    BgpTable *connected_table = NULL;
    connected_table = connected_ri->GetTable(GetConnectedFamily());
    assert(connected_table);
    BgpTable *dest_table = dest->GetTable(GetFamily());
    assert(dest_table);

    // Allocate the new service chain.
    ServiceChainPtr chain = ServiceChainPtr(new ServiceChainT(this, group,
        rtinstance, dest, connected_ri, config.prefix, chain_addr,
        config.sc_head, config.retain_as_path));

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
            rtinstance->config()->service_chain_info(GetSCFamily());
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
template class ServiceChainMgr<ServiceChainEvpn>;
template class ServiceChainMgr<ServiceChainEvpn6>;
