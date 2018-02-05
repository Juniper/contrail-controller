/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/logging.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/bridge_route.h>
#include <oper/inet_unicast_route.h>
#include <oper/evpn_route.h>
#include <oper/agent_route.h>
#include <oper/agent_route_walker.h>
#include <oper/project_config.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/vxlan_routing_manager.h>

using namespace std;

VxlanRoutingState::VxlanRoutingState(VxlanRoutingManager *mgr,
                                     VrfEntry *vrf) {
    inet4_table_ = vrf->GetInet4UnicastRouteTable();
    inet6_table_ = vrf->GetInet6UnicastRouteTable();
    evpn_table_ = vrf->GetEvpnRouteTable();
    inet4_id_ = inet4_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
    inet6_id_ = inet6_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
    evpn_id_ = evpn_table_->
        Register(boost::bind(&VxlanRoutingManager::RouteNotify,
                             mgr, _1, _2));
}

VxlanRoutingState::~VxlanRoutingState() {
    inet4_table_->Unregister(inet4_id_);
    inet6_table_->Unregister(inet6_id_);
    evpn_table_->Unregister(evpn_id_);
}

VxlanRoutingRouteWalker::VxlanRoutingRouteWalker(const std::string &name,
                                                 VxlanRoutingManager *mgr,
                                                 Agent *agent) :
    AgentRouteWalker(name, agent), mgr_(mgr) {
}

VxlanRoutingRouteWalker::~VxlanRoutingRouteWalker() {
}

//Only take notification of evpn type 2 routes.
//Change in them will trigger change in rest.
bool VxlanRoutingRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                              DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    if (!evpn_rt)
        return true;
    assert(evpn_rt->IsType2());

    return mgr_->EvpnType2RouteNotify(partition, e);
}

VxlanRoutingVrfMapper::VxlanRoutingVrfMapper(VxlanRoutingManager *mgr) :
    mgr_(mgr), lr_vrf_info_map_(), vn_lr_set_(), evpn_table_walker_() {
}

VxlanRoutingVrfMapper::~VxlanRoutingVrfMapper() {
}

VxlanRoutingVrfMapper::RoutedVrfInfo& VxlanRoutingVrfMapper::GetRoutedVrfInfo
(const VnEntry *vn) {
    return lr_vrf_info_map_[vn->logical_router_uuid()];
}

void VxlanRoutingVrfMapper::WalkEvpnTable(EvpnAgentRouteTable *table) {
    DBTable::DBTableWalkRef walk_ref;
    EvpnTableWalker::iterator it = evpn_table_walker_.find(table);
    if (it == evpn_table_walker_.end()) {
        walk_ref = table->
            AllocWalker(boost::bind(&VxlanRoutingManager::RouteNotify,
                                    mgr_, _1, _2),
                        boost::bind(&VxlanRoutingVrfMapper::RouteWalkDone,
                                    this, _1, _2));
        evpn_table_walker_[table] = walk_ref;
    } else {
        walk_ref = it->second;
    }
    table->WalkAgain(walk_ref);
}

void VxlanRoutingVrfMapper::RouteWalkDone(DBTable::DBTableWalkRef walk_ref,
                                          DBTableBase *partition) {
    const EvpnAgentRouteTable *table = static_cast<const EvpnAgentRouteTable *>
        (walk_ref->table());
    EvpnTableWalker::iterator it = evpn_table_walker_.find(table);
    assert(it != evpn_table_walker_.end());
    evpn_table_walker_.erase(it);
}

void VxlanRoutingVrfMapper::WalkBridgeVrfs
(const VxlanRoutingVrfMapper::RoutedVrfInfo &routed_vrf_info)
{
    //Start walk on all l2 table
    VxlanRoutingVrfMapper::RoutedVrfInfo::BridgeVnListIter it =
        routed_vrf_info.bridge_vn_list_.begin();
    while (it != routed_vrf_info.bridge_vn_list_.end()) {
        const VnEntry *vn = static_cast<const VnEntry *>(*it);
        EvpnAgentRouteTable *evpn_table =
            static_cast<EvpnAgentRouteTable *>(vn->GetVrf()->
                                               GetEvpnRouteTable());
        WalkEvpnTable(evpn_table);
        it++;
    }
}

const VrfEntry *VxlanRoutingVrfMapper::GetRoutingVrfUsingVn
(const VnEntry *vn) {
    return GetRoutedVrfInfo(vn).routing_vrf_;
}

void VxlanRoutingVrfMapper::UpdateRoutedVrfInfo(const EvpnRouteEntry *evpn_rt) {
    //Local VM path provides interface to get LR.
    AgentPath *path = evpn_rt->FindLocalVmPortPath();
    if (path == NULL) {
        return;
    }

    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>
        (path->nexthop());
    if (!nh) {
        return;
    }

    const VmInterface *vmi = dynamic_cast<const VmInterface *>
        (nh->GetInterface());
    if (!vmi) {
        return;
    }

    const boost::uuids::uuid &lr_uuid = vmi->logical_router_uuid();
    //VMI's vn can never be l3 vrf.
    assert(vmi->vn()->logical_router_uuid() == nil_uuid());

    VxlanRoutingVrfMapper::VnLrSetIter vn_lr_it = vn_lr_set_.find(vmi->vn());
    if ((vn_lr_it != vn_lr_set_.end()) &&
        (vn_lr_it->second != lr_uuid)) {
        //VN moved from one LR to another LR, renotify all routes.
        LrVrfInfoMapIter old_lr_vrf_it =
            lr_vrf_info_map_.find(vn_lr_it->second);
        old_lr_vrf_it->second.bridge_vn_list_.erase(vmi->vn());
        TryDeleteLogicalRouter(old_lr_vrf_it);
    }

    vn_lr_set_[vmi->vn()] = lr_uuid;
    if (lr_vrf_info_map_[lr_uuid].bridge_vn_list_.find(vmi->vn()) ==
        lr_vrf_info_map_[lr_uuid].bridge_vn_list_.end()) {
        lr_vrf_info_map_[lr_uuid].bridge_vn_list_.insert(vmi->vn());
        //TODO agent being single partition, walk here might be overkill.
        const VrfEntry *vrf = static_cast<const VrfEntry *>(vmi->vrf());
        WalkEvpnTable(static_cast<EvpnAgentRouteTable *>(vrf->
                                                GetEvpnRouteTable()));
    }
}

// Invoked everytime when a vrf is pulled out of use.
// Holds on object till all bridge and routing vrf are gone.
void VxlanRoutingVrfMapper::TryDeleteLogicalRouter(LrVrfInfoMapIter &it) {
    if ((it->second.routing_vrf_ == NULL) &&
        (it->second.bridge_vn_list_.size() == 0)) {
        lr_vrf_info_map_.erase(it);
    }
}

/**
 * VxlanRoutingManager
 */
VxlanRoutingManager::VxlanRoutingManager(Agent *agent) :
    vxlan_routing_enabled_(false), walker_(),
    vn_listener_id_(), vrf_listener_id_(),
    vrf_mapper_(this), agent_(agent) {
}

VxlanRoutingManager::~VxlanRoutingManager() {
}

void VxlanRoutingManager::Register() {
    //Register for project config changes which provides vxlan routing enable
    //knob.
    agent_->oper_db()->project_config()->Register(
          boost::bind(&VxlanRoutingManager::Config, this));

    //Walker to go through routes in bridge evpn tables.
    walker_.reset(new VxlanRoutingRouteWalker("VxlanRoutingManager", this,
                                              agent_));
    agent_->oper_db()->agent_route_walk_manager()->
        RegisterWalker(static_cast<AgentRouteWalker *>(walker_.get()));

    //Register all listener ids.
    vrf_listener_id_ = agent_->vrf_table()->Register(
        boost::bind(&VxlanRoutingManager::VrfNotify, this, _1, _2));
    vn_listener_id_ = agent_->vn_table()->
        Register(boost::bind(&VxlanRoutingManager::VnNotify,
                             this, _1, _2));
}

void VxlanRoutingManager::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->vrf_table()->Unregister(vn_listener_id_);
    agent_->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(walker_.get());
    walker_.reset(NULL);
}

//Listens to enabling of vxlan routing
void VxlanRoutingManager::Config() {
    if (agent_->oper_db()->project_config()->vxlan_routing() ==
        vxlan_routing_enabled_) {
        return;
    }

    if (vxlan_routing_enabled_ !=
        agent_->oper_db()->project_config()->vxlan_routing()) {
        vxlan_routing_enabled_ =
            agent_->oper_db()->project_config()->vxlan_routing();
        walker_->StartVrfWalk();
    }
}

/**
 * VNNotify
 * Handles routing vrf i.e. VRF meant for doing evpn routing.
 * Addition or deletion of same add/withdraws route imported from bridge vrf in
 * routing vrf.
 * Walk is issued for the routes of bridge vrf's evpn table.
 *
 * For bridge VRF, only delete of VN is handled here. Add has no operation as
 * add of VN does not give any info on LR/Routing VRF to use.
 * When delete is seen withdraw from the list of bridge list.
 */
void VxlanRoutingManager::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    VnEntry *vn = static_cast<VnEntry *>(e);
    bool withdraw = false;
    bool update = false;
    VxlanRoutingVrfMapper::VnLrSetIter it = vrf_mapper_.vn_lr_set_.find(vn);

    if (vn->IsDeleted() ||
        (vn->GetVrf() == NULL) ||
        (vn->vxlan_routing_vn() == false)) {
        update = false;
        withdraw = true;
    } else {
        update = true;
        if ((it != vrf_mapper_.vn_lr_set_.end()) &&
            (it->second != vn->logical_router_uuid())) {
            withdraw = true;
        }
    }

    if (withdraw) {
        if (it == vrf_mapper_.vn_lr_set_.end()) {
            return;
        }

        VxlanRoutingVrfMapper::LrVrfInfoMapIter routing_info_it =
            vrf_mapper_.lr_vrf_info_map_.find(it->second);
        //Delete only if parent VN is same as notified VN coz it may so happen
        //that some other VN has taken the ownership  of this LR and
        //notification of same came before this VN.
        if (routing_info_it != vrf_mapper_.lr_vrf_info_map_.end()) {
            if (routing_info_it->second.parent_vn_entry_ == vn) {
                //Routing VN/VRF
                //Reset parent vn and routing vrf
                routing_info_it->second.parent_vn_entry_ = NULL;
                routing_info_it->second.routing_vrf_ = NULL;
                vrf_mapper_.WalkBridgeVrfs(routing_info_it->second);
            } else {
                //Bridge VN/VRF
                routing_info_it->second.bridge_vn_list_.erase(vn);
            }
            //Trigger delete of logical router
            vrf_mapper_.TryDeleteLogicalRouter(routing_info_it);
        }
        vrf_mapper_.vn_lr_set_.erase(it);
    }

    if (update) {
        if (vn->logical_router_uuid() == nil_uuid()) {
            //This is bridge VN as LR is not known. LR id will get updated when
            //first evpn route update is seen in the associated VRF.
            return;
        }

        if (it == vrf_mapper_.vn_lr_set_.end()) {
            vrf_mapper_.vn_lr_set_[vn] = vn->logical_router_uuid();
        }

        VxlanRoutingVrfMapper::RoutedVrfInfo routed_vrf_info =
            vrf_mapper_.lr_vrf_info_map_[vn->logical_router_uuid()];
        //Take the ownership of LR
        routed_vrf_info.parent_vn_entry_ = vn;
        if (routed_vrf_info.routing_vrf_ != vn->GetVrf()) {
            routed_vrf_info.routing_vrf_ = vn->GetVrf();
            vrf_mapper_.WalkBridgeVrfs(routed_vrf_info);
        }
    }
}

/**
 * VrfNotify
 * This listener is used to identify bridge vrf and not routing vrfs.
 * If bridge vrf is associated to a routing vrf, then default routes are added
 * in bridge vrf's inet table to redirect all lookupis in routing vrf inet
 * table.
 * Other than that state is added in vrf which tracks the listener for routes in
 * VRF's route table(evpn and inet). For more info in route notification refer
 * to RouteNotify.
 */
void VxlanRoutingManager::VrfNotify(DBTablePartBase *partition,
                                    DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    if (vrf->GetName().compare(agent_->fabric_policy_vrf_name()) != 0)
        return;

    VxlanRoutingState *state = dynamic_cast<VxlanRoutingState *>(vrf->
                             GetState(partition->parent(), vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (state) {
            vrf->ClearState(partition->parent(), vrf_listener_id_);
            delete state;
        }
        return;
    } else {
        //Vrf added/changed.
        if (!state) {
            state = new VxlanRoutingState(this, vrf);
            vrf->SetState(partition->parent(), vrf_listener_id_, state);
        }
    }

    HandleDefaultRoute(vrf);
}

void VxlanRoutingManager::UpdateEvpnType5Route(Agent *agent,
                                            const AgentRoute *route,
                                            const AgentPath *path,
                                            const VrfEntry *routing_vrf) {
    const InetUnicastRouteEntry *inet_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(routing_vrf->GetEvpnRouteTable());
    //Add route in evpn table
    DBRequest nh_req;
    InterfaceNHKey *key = static_cast<InterfaceNHKey *>
        (path->nexthop()->GetDBRequestKey().get());
    key->set_flags(key->flags() | InterfaceNHFlags::VXLAN_ROUTING);
    nh_req.key.reset(key);
    nh_req.data.reset(NULL);
    evpn_table->AddType5Route(agent->local_vm_peer(),
                              routing_vrf->GetName(),
                              inet_rt->addr(),
                              routing_vrf->vxlan_id(),
                              new EvpnRoutingData(nh_req,
                                                  routing_vrf));
}

//Handles change in NH of local vm port path
//For all host routes with non local vm port path, evpn route in routing vrf
//need not be added. It should come from CN.
bool VxlanRoutingManager::InetRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry*>(e);
    const AgentPath *local_vm_port_path = inet_rt->FindLocalVmPortPath();

    //Further leaking to type 5 evpn table should be only done for local vmi.
    if (!local_vm_port_path) {
        return true;
    }

    const EvpnRoutingPath *evpn_routing_path =
        static_cast<const EvpnRoutingPath *>(inet_rt->
                                             FindPath(agent_->evpn_routing_peer()));
    //Only routes with evpn_routing_peer path has to be leaked.
    if (!evpn_routing_path) {
        return true;
    }

    const VrfEntry *routing_vrf = evpn_routing_path->routing_vrf();
    assert(routing_vrf != inet_rt->vrf());
    if (inet_rt->IsDeleted() || (local_vm_port_path == NULL) ||
        (routing_vrf == NULL)) {
        evpn_routing_path->DeleteEvpnType5Route(agent_, inet_rt);
        return true;
    }

    UpdateEvpnType5Route(agent_, inet_rt, local_vm_port_path, routing_vrf);
    return true;
}

bool VxlanRoutingManager::EvpnType5RouteNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    VrfEntry *vrf = evpn_rt->vrf();
    assert(evpn_rt->IsType5());

    if (evpn_rt->IsDeleted()) {
        InetUnicastAgentRouteTable *inet_table =
            vrf->GetInetUnicastRouteTable(evpn_rt->ip_addr());
        inet_table->Delete(agent_->evpn_routing_peer(),
                           vrf->GetName(),
                           evpn_rt->ip_addr(),
                           evpn_rt->GetVmIpPlen());
        return true;
    }

    InetUnicastAgentRouteTable *inet_table =
        evpn_rt->vrf()->GetInetUnicastRouteTable(evpn_rt->ip_addr());

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(evpn_rt->GetActiveNextHop()->GetDBRequestKey().get());
    inet_table->AddEvpnRoutingRoute(evpn_rt->ip_addr(),
                                    evpn_rt->GetVmIpPlen(),
                                    vrf,
                                    agent_->evpn_routing_peer(),
                                    nh_req);
    return true;
}

void VxlanRoutingManager::DeleteInetRoute(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    VrfEntry *bridge_vrf = evpn_rt->vrf();
    //Add Inet route to point to table NH in l2 vrf inet
    const IpAddress &ip_addr = evpn_rt->ip_addr();
    if (ip_addr.is_unspecified())
        return;

    InetUnicastAgentRouteTable *inet_table =
        bridge_vrf->GetInetUnicastRouteTable(ip_addr);
    inet_table->Delete(agent_->evpn_routing_peer(),
                       bridge_vrf->GetName(), ip_addr,
                       evpn_rt->GetVmIpPlen());
}

void VxlanRoutingManager::UpdateInetRoute(DBTablePartBase *partition,
                                          DBEntryBase *e,
                                          const VrfEntry *routing_vrf) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    VrfEntry *bridge_vrf = evpn_rt->vrf();

    //Add Inet route to point to table NH in l2 vrf inet
    InetUnicastAgentRouteTable *inet_table =
        bridge_vrf->GetInetUnicastRouteTable(evpn_rt->ip_addr());
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new VrfNHKey(routing_vrf->GetName(), false, false));
    nh_req.data.reset(new VrfNHData(false, false, false));
    inet_table->AddEvpnRoutingRoute(evpn_rt->ip_addr(),
                                    evpn_rt->GetVmIpPlen(),
                                    routing_vrf,
                                    agent_->evpn_routing_peer(),
                                    nh_req);
}

/**
 * EvpnType2RouteNotify
 * All routes local or non-local should add inet route for IP in bridge vrf inet
 * table.
 * Zero IP address will be ignored.
 */
bool VxlanRoutingManager::EvpnType2RouteNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    const VrfEntry *routing_vrf = (vrf_mapper_.
        GetRoutedVrfInfo(evpn_rt->vrf()->vn())).routing_vrf_;
    assert(evpn_rt->IsType2() == true);

    if (evpn_rt->ip_addr().is_unspecified()) {
        return true;
    }

    if (evpn_rt->IsDeleted() || !routing_vrf) {
        DeleteInetRoute(partition, e);
    } else {
        vrf_mapper_.UpdateRoutedVrfInfo(evpn_rt);
        UpdateInetRoute(partition, e, routing_vrf);
    }
    return true;
}

bool VxlanRoutingManager::EvpnRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    assert(evpn_rt != NULL);

    if (!evpn_rt->is_multicast())
        return true;

    //In typer 5 mac is always zero
    if (evpn_rt->IsType5()) {
        return EvpnType5RouteNotify(partition, e);
    } else {
        return EvpnType2RouteNotify(partition, e);
    }

    return true;
}

bool VxlanRoutingManager::RouteNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry*>(e);
    if (inet_rt) {
        return InetRouteNotify(partition, e);
    }

    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    if (evpn_rt) {
        return EvpnRouteNotify(partition, e);
    }
    return true;
}

void VxlanRoutingManager::HandleDefaultRoute(const VrfEntry *vrf) {
    if (vrf->vn() && (vrf->vn()->vxlan_routing_vn() == false)) {
        const VrfEntry *routing_vrf =
            vrf_mapper_.GetRoutingVrfUsingVn(vrf->vn());
        if (!routing_vrf || vrf->IsDeleted()) {
            DeleteDefaultRoute(vrf);
        } else {
            UpdateDefaultRoute(vrf, routing_vrf);
        }
    }
}

void VxlanRoutingManager::DeleteDefaultRoute(const VrfEntry *vrf) {
    vrf->GetInet4UnicastRouteTable()->
        Delete(agent_->evpn_routing_peer(), vrf->GetName(),
               Ip4Address(), 0);
    vrf->GetInet6UnicastRouteTable()->
        Delete(agent_->evpn_routing_peer(), vrf->GetName(),
               Ip6Address(), 0);
}

void VxlanRoutingManager::UpdateDefaultRoute(const VrfEntry *bridge_vrf,
                                             const VrfEntry *routing_vrf) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new VrfNHKey(routing_vrf->GetName(), false, false));
    nh_req.data.reset(new VrfNHData(false, false, false));
    bridge_vrf->GetInet4UnicastRouteTable()->
        AddEvpnRoutingRoute(Ip4Address(), 0, routing_vrf,
                            agent_->evpn_routing_peer(),
                            nh_req);
    bridge_vrf->GetInet6UnicastRouteTable()->
        AddEvpnRoutingRoute(Ip6Address(), 0, routing_vrf,
                            agent_->evpn_routing_peer(),
                            nh_req);
}
