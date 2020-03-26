/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_VXLAN_ROUTING_H
#define __AGENT_OPER_VXLAN_ROUTING_H

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/oper_db.h>

/**
 * Vxlan Routing Manager
 *
 * The manager is responsible for leaking type 2 route from evpn table as type 5
 * in routing vrf evpn table.
 * Bridge Vrf: Regular VN hosting VMI
 * Routing VRF: Used for routing for these bridge vrfs.
 *
 * How is leak done?
 *
 * Say there are two bridge vrf: red and blue linked for routing to l3vrf.
 * Leak happens in following steps:
 * 1) Walk of red and blue evpn table is done to visit every route.
 *
 * 2) Each evpn route then picks the IP component(zero IP is ignored). This evpn
 * route add a request to program inet route for IP with a special
 * evpn_routing_peer. The nexthop will be a table nexthop pointing to l3vrf.
 *
 * 3) Notification is received for modified inet route. This is needed to pick
 * up the l3 interface nh. So all the routes will localvmport peer path (i.e.
 * VMI path) will further leak the route to l3vrf evpn table with zero mac and
 * IP. Nexthop will be l3 interface nh with only vxlan encap. Inet routes with
 * no local NH will not take any further action. Because of the above steps all
 * inet routes in red and blue (IP from evpn) will point to table NH.
 *
 * 4) Evpn notification are also notified for l3vrf. This table is populated via
 * step#3 above or via BGP exposed ones. Both types will trigger Inet route
 * addition to l3vrf inet table. It will be done with evpn routes l3 interface
 * nh.
 *
 * 5) In both red and blue vrf default route is also added to redirect all
 * unknown prefixes for routing to l3vrf.
 *
 * Same path is used to withdraw routes.
 *
 *
 * How l3vrf and red/blue vrf are linked?
 * l3vrf is linked to logical router. VMI participating in red/blue are linked
 * to logical router. This ensures proper l3vrf is assigned to red/blue.
 * Multiple l3vrf are also supported.
 * VxlanRoutingVrfMapper: This tracks the VN to LR and LR to RoutedVrfInfo
 * mapping. RoutedVrfInfo contains l3vrf as routing_vrf and list of all bridge
 * vrf attached. So red and blue goes here.
 *
 */

/**
 * VxlanRoutingState
 * State to track inet and evpn table listeners.
 */
struct VxlanRoutingState : public DBState {
    VxlanRoutingState(VxlanRoutingManager *mgr,
                      VrfEntry *vrf);
    virtual ~VxlanRoutingState();

    DBTable::ListenerId inet4_id_;
    DBTable::ListenerId inet6_id_;
    DBTable::ListenerId evpn_id_;
    AgentRouteTable *inet4_table_;
    AgentRouteTable *inet6_table_;
    AgentRouteTable *evpn_table_;
    bool is_bridge_vrf_; // tracks if the VRF is bridge vrf
};

/**
 * VNstate
 * tracks all VMI attached with the logical router uuid
 */
struct VxlanRoutingVnState : public DBState {
    typedef std::set<const VmInterface *> VmiList;
    typedef VmiList::iterator VmiListIter;

    VxlanRoutingVnState(VxlanRoutingManager *mgr);
    virtual ~VxlanRoutingVnState();

    void AddVmi(const VnEntry *vn, const VmInterface *vmi);
    //Deletes VMI from set and checks if all VMI are gone or logical router uuid
    //changes because remaining VMI is connected to other LR.
    void DeleteVmi(const VnEntry *vn, const VmInterface *vmi);
    boost::uuids::uuid logical_router_uuid() const;

    std::set<const VmInterface *> vmi_list_;
    bool is_routing_vn_;
    boost::uuids::uuid logical_router_uuid_;
    //Hold vrf reference to handle vn with null vrf
    VrfEntryRef vrf_ref_;
    VxlanRoutingManager *mgr_;
};

/**
 * VmiState
 * Captures movement of VMI among LR.
 */
struct VxlanRoutingVmiState : public DBState {
    VxlanRoutingVmiState();
    virtual ~VxlanRoutingVmiState();

    VnEntryRef vn_entry_;
    boost::uuids::uuid logical_router_uuid_;
};

/**
 * VxlanRoutingRouteWalker
 * Incarnation of AgentRouteWalker. Listens to evpn type 2 routes.
 * Started when l3vrf is added/deleted or bridge vrf is added/deleted.
 */
class VxlanRoutingRouteWalker : public AgentRouteWalker {
public:
    VxlanRoutingRouteWalker(const std::string &name,
                            VxlanRoutingManager *mgr,
                            Agent *agent);
    virtual ~VxlanRoutingRouteWalker();
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    VxlanRoutingManager *mgr_;
    DISALLOW_COPY_AND_ASSIGN(VxlanRoutingRouteWalker);
};

/**
 * VxlanRoutingVrfMapper
 * As mentioned above in functionality, it is used to track following:
 * - VN to LR uuid mapping
 * - LR to RoutedVrfInfo mapping.
 * RoutedVrfInfo - Contains l3vrf as routing vrf and list of all bridge vrf
 * linked to this routing vrf.
 * Along with these it has a walker for walking evpn tables.
 * EVPN walk is triggered for following reasons:
 * - detection of l3vrf, walks all bridge evpn linked to same.
 * - bridge vrf addition - Here evpn table of this bridge vrf is walked.
 * - deletion of vrf
 * (If multiple walks get scheduled for evpn table then they are collapsed and
 * only one walk is done)
 */
class VxlanRoutingVrfMapper {
public:
    struct RoutedVrfInfo {
        typedef std::set<const VnEntry *> BridgeVnList;
        typedef BridgeVnList::iterator BridgeVnListIter;
        RoutedVrfInfo() : parent_vn_entry_(),
        routing_vrf_(NULL), bridge_vn_list_() {
        }
        virtual ~RoutedVrfInfo() {
        }

        const VnEntry *parent_vn_entry_;
        const VrfEntry *routing_vrf_;
        BridgeVnList bridge_vn_list_;
    };
    //Logical router - RoutedVrfInfo(Set of l3 vrf and bridge vrf)
    typedef std::map<boost::uuids::uuid, RoutedVrfInfo> LrVrfInfoMap;
    typedef LrVrfInfoMap::iterator LrVrfInfoMapIter;
    //VNEntry to LR id
    typedef std::map<const VnEntry *, boost::uuids::uuid> VnLrSet;
    typedef VnLrSet::iterator VnLrSetIter;
    //Tracks all walkers on evpn tables, if needed the walk can be restarted
    //instead of spawning new one for a table.
    typedef std::map<const EvpnAgentRouteTable *, DBTable::DBTableWalkRef>
        EvpnTableWalker;

    VxlanRoutingVrfMapper(VxlanRoutingManager *mgr);
    virtual ~VxlanRoutingVrfMapper();

    void RouteWalkDone(DBTable::DBTableWalkRef walk_ref,
                       DBTableBase *partition);
    //TODO better way to release logical router from lr_vrf_info_map_
    //Easier way will be to add logical router in db and trigger delete of this
    //via LR delete in same.
    void TryDeleteLogicalRouter(LrVrfInfoMapIter &it);
    bool IsEmpty() const {
        return ((vn_lr_set_.size() == 0) &&
                (lr_vrf_info_map_.size() == 0));
    }

private:
    friend class VxlanRoutingManager;
    void WalkBridgeVrfs(const RoutedVrfInfo &routing_vrf_info);
    void WalkEvpnTable(EvpnAgentRouteTable *table);
    //Using VN's Lr uuid identify the routing vrf
    const VrfEntry *GetRoutingVrfUsingVn(const VnEntry *vn);
    const VrfEntry *GetRoutingVrfUsingEvpnRoute(const EvpnRouteEntry *rt);
    const VrfEntry *GetRoutingVrfUsingUuid(const boost::uuids::uuid &uuid);
    const boost::uuids::uuid GetLogicalRouterUuidUsingRoute(const AgentRoute *rt);

    VxlanRoutingManager *mgr_;
    LrVrfInfoMap lr_vrf_info_map_;
    VnLrSet vn_lr_set_;
    EvpnTableWalker evpn_table_walker_;
    DISALLOW_COPY_AND_ASSIGN(VxlanRoutingVrfMapper);
};

/**
 * VxlanRoutingManager
 * Manages following:
 * - state creation on each vrf
 * - Notification registerations
 * - Listeners for notification
 * - shutdown/register
 * - config listener for vxlan routing enabled/disabled
 */
class VxlanRoutingManager {
public:
    VxlanRoutingManager(Agent *agent);
    virtual ~VxlanRoutingManager();

    //Oper handler
    void Register();
    void Shutdown();

    //Listener to vxlan config
    void Config();

    void Enabled();
    void Disabled();
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    void BridgeVnNotify(const VnEntry *vn, VxlanRoutingVnState *vn_state);
    void RoutingVnNotify(const VnEntry *vn, VxlanRoutingVnState *vn_state);
    void VnWalkDone(DBTable::DBTableWalkRef walk_ref,
                    DBTableBase *partition);
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void VmiNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool RouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    void HandleDefaultRoute(const VrfEntry *vrf, bool bridge_vrf=false);
    void FillSandeshInfo(VxlanRoutingResp *resp);
    DBTable::ListenerId vn_listener_id() const {
        return vn_listener_id_;
    }
    DBTable::ListenerId vmi_listener_id() const {
        return vmi_listener_id_;
    }
    DBTable::ListenerId vrf_listener_id() const {
        return vrf_listener_id_;
    }
    const VxlanRoutingVrfMapper &vrf_mapper() const {
        return vrf_mapper_;
    }
    AgentRouteWalker* walker() {
        return walker_.get();
    }

private:
    friend class VxlanRoutingRouteWalker;
    void DeleteDefaultRoute(const VrfEntry *vrf);
    void UpdateDefaultRoute(const VrfEntry *vrf,
                            const VrfEntry *routing_vrf);
    void DeleteInetRoute(DBTablePartBase *partition, DBEntryBase *e);
    void UpdateInetRoute(DBTablePartBase *partition, DBEntryBase *e,
                         const VrfEntry *routing_vrf);
    void UpdateEvpnType5Route(Agent *agent,
                              const AgentRoute *rt,
                              const AgentPath *path,
                              const VrfEntry *routing_vrf);
    bool InetRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnRouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnType5RouteNotify(DBTablePartBase *partition, DBEntryBase *e);
    bool EvpnType2RouteNotify(DBTablePartBase *partition, DBEntryBase *e);

    Agent *agent_;
    AgentRouteWalkerPtr walker_;
    DBTable::ListenerId vn_listener_id_;
    DBTable::ListenerId vrf_listener_id_;
    DBTable::ListenerId vmi_listener_id_;
    VxlanRoutingVrfMapper vrf_mapper_;
    DISALLOW_COPY_AND_ASSIGN(VxlanRoutingManager);
};

#endif
