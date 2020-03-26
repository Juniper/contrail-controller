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
    if (vrf->vn() && (vrf->vn()->vxlan_routing_vn() == false)) {
        is_bridge_vrf_ = true;
    }
}

VxlanRoutingState::~VxlanRoutingState() {
    inet4_table_->Unregister(inet4_id_);
    inet6_table_->Unregister(inet6_id_);
    evpn_table_->Unregister(evpn_id_);
}

VxlanRoutingVnState::VxlanRoutingVnState(VxlanRoutingManager *mgr) :
    vmi_list_(), is_routing_vn_(false),
    logical_router_uuid_(boost::uuids::nil_uuid()), mgr_(mgr) {
}

VxlanRoutingVnState::~VxlanRoutingVnState() {
}

void VxlanRoutingVnState::AddVmi(const VnEntry *vn, const VmInterface *vmi) {
    assert(vmi->logical_router_uuid() != boost::uuids::nil_uuid());
    VmiListIter it = vmi_list_.find(vmi);
    if (it != vmi_list_.end()) {
        return;
    }

    vmi_list_.insert(vmi);
    if ((logical_router_uuid_ != vmi->logical_router_uuid()) &&
        (*(vmi_list_.begin()) == vmi)) {
        mgr_->BridgeVnNotify(vn, this);
    }
}

void VxlanRoutingVnState::DeleteVmi(const VnEntry *vn, const VmInterface *vmi) {
    VmiListIter it = vmi_list_.find(vmi);
    if (it == vmi_list_.end()) {
        return;
    }
    vmi_list_.erase(vmi);
    mgr_->BridgeVnNotify(vn, this);
}

VxlanRoutingVmiState::VxlanRoutingVmiState() :
    vn_entry_(NULL), logical_router_uuid_(boost::uuids::nil_uuid()) {
}

VxlanRoutingVmiState::~VxlanRoutingVmiState() {
}

boost::uuids::uuid VxlanRoutingVnState::logical_router_uuid() const {
    if (vmi_list_.size() == 0)
        return boost::uuids::nil_uuid();

    return (*(vmi_list_.begin()))->logical_router_uuid();
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
    if (!evpn_rt || (evpn_rt->vrf()->vn() == NULL))
        return true;
    if (!evpn_rt->IsType2())
        return true;

    return mgr_->EvpnType2RouteNotify(partition, e);
}

VxlanRoutingVrfMapper::VxlanRoutingVrfMapper(VxlanRoutingManager *mgr) :
    mgr_(mgr), lr_vrf_info_map_(), vn_lr_set_(), evpn_table_walker_() {
}

VxlanRoutingVrfMapper::~VxlanRoutingVrfMapper() {
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
    //Every time walk is issued for bridge table revisit default routes
    mgr_->HandleDefaultRoute(table->vrf_entry());
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
    //Start walk on all l2 tables
    VxlanRoutingVrfMapper::RoutedVrfInfo::BridgeVnListIter it =
        routed_vrf_info.bridge_vn_list_.begin();
    while (it != routed_vrf_info.bridge_vn_list_.end()) {
        const VnEntry *vn = static_cast<const VnEntry *>(*it);
        const VrfEntry *vrf = vn->GetVrf();
        if (vrf) {
            EvpnAgentRouteTable *evpn_table =
                static_cast<EvpnAgentRouteTable *>(vrf->GetEvpnRouteTable());
            if (!evpn_table)
                continue;
            WalkEvpnTable(evpn_table);
        }
        it++;
    }
}

const VrfEntry *VxlanRoutingVrfMapper::GetRoutingVrfUsingVn
(const VnEntry *vn) {
    VnLrSetIter it = vn_lr_set_.find(vn);
    if (it != vn_lr_set_.end()) {
        return GetRoutingVrfUsingUuid(it->second);
    }
    return NULL;
}

const VrfEntry *VxlanRoutingVrfMapper::GetRoutingVrfUsingEvpnRoute
(const EvpnRouteEntry *rt) {
    return GetRoutingVrfUsingUuid(GetLogicalRouterUuidUsingRoute(rt));
}

const VrfEntry *VxlanRoutingVrfMapper::GetRoutingVrfUsingUuid
(const boost::uuids::uuid &uuid) {
    LrVrfInfoMapIter it = lr_vrf_info_map_.find(uuid);
    if (it != lr_vrf_info_map_.end()) {
        return it->second.routing_vrf_;
    }
    return NULL;
}

const boost::uuids::uuid VxlanRoutingVrfMapper::GetLogicalRouterUuidUsingRoute
(const AgentRoute *rt) {
    using boost::uuids::nil_uuid;

    //Local VM path provides interface to get LR.
    AgentPath *path = rt->FindLocalVmPortPath();
    if (path == NULL) {
        return nil_uuid();
    }

    const InterfaceNH *nh = dynamic_cast<const InterfaceNH *>
        (path->nexthop());
    if (!nh) {
        return nil_uuid();
    }

    const VmInterface *vmi = dynamic_cast<const VmInterface *>
        (nh->GetInterface());
    if (!vmi || !vmi->vn()) {
        return nil_uuid();
    }

    const VxlanRoutingVnState *vn_state =
        dynamic_cast<const VxlanRoutingVnState *>(vmi->vn()->
                           GetAgentDBEntryState(mgr_->vn_listener_id()));
    if ((vn_state == NULL) || (vn_state->vmi_list_.size() == 0)) {
        return nil_uuid();
    }

    return vn_state->logical_router_uuid_;
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
    agent_(agent), walker_(), vn_listener_id_(),
    vrf_listener_id_(), vmi_listener_id_(), vrf_mapper_(this) {
}

VxlanRoutingManager::~VxlanRoutingManager() {
}

void VxlanRoutingManager::Register() {
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
    vmi_listener_id_ = agent_->interface_table()->Register(
        boost::bind(&VxlanRoutingManager::VmiNotify, this, _1, _2));
}

void VxlanRoutingManager::Shutdown() {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->vrf_table()->Unregister(vn_listener_id_);
    agent_->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(walker_.get());
    walker_.reset(NULL);
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
    VxlanRoutingVnState *vn_state = dynamic_cast<VxlanRoutingVnState *>
        (vn->GetAgentDBEntryState(vn_listener_id_));
    if (vn->IsDeleted()) {
        if (!vn_state)
            return;

        if (vn_state->is_routing_vn_) {
            RoutingVnNotify(vn, vn_state);
        } else {
            BridgeVnNotify(vn, vn_state);
        }

        //Delete State
        vn->ClearState(partition->parent(), vn_listener_id_);
        delete vn_state;
        return;
    }

    if (!vn_state) {
        vn_state = new VxlanRoutingVnState(this);
        vn->SetState(partition->parent(), vn_listener_id_, vn_state);
    }

    if (vn->vxlan_routing_vn()) {
        vn_state->is_routing_vn_ = vn->vxlan_routing_vn();
    }

    vn_state->vrf_ref_ = vn->GetVrf();
    if (vn_state->is_routing_vn_) {
        vn_state->logical_router_uuid_ = vn->logical_router_uuid();
        RoutingVnNotify(vn, vn_state);
    } else {
        BridgeVnNotify(vn, vn_state);
    }

    return;
}

void UpdateLogicalRouterUuid(const VnEntry *vn,
                             VxlanRoutingVnState *vn_state) {
    using boost::uuids::nil_uuid;

    if (vn_state->vmi_list_.size() == 0) {
        vn_state->logical_router_uuid_ = nil_uuid();
    }

    VxlanRoutingVnState::VmiListIter it = vn_state->vmi_list_.begin();
    while (it != vn_state->vmi_list_.end()) {
        vn_state->logical_router_uuid_ = (*it)->logical_router_uuid();
        if ((*it)->logical_router_uuid() != nil_uuid()) {
            return;
        }
        //Delete VMI with no lr uuid, vmi update will handle rest.
        vn_state->vmi_list_.erase(it);
        if (vn_state->vmi_list_.size() == 0) {
            vn_state->logical_router_uuid_ = nil_uuid();
            return;
        }
        it = vn_state->vmi_list_.begin();
    }
    return;
}

void VxlanRoutingManager::BridgeVnNotify(const VnEntry *vn,
                                         VxlanRoutingVnState *vn_state) {
    using boost::uuids::nil_uuid;

    if (vn->logical_router_uuid() != nil_uuid()) {
        return;
    }

    VxlanRoutingVrfMapper::VnLrSetIter it = vrf_mapper_.vn_lr_set_.find(vn);
    VxlanRoutingVrfMapper::LrVrfInfoMapIter routing_info_it =
        vrf_mapper_.lr_vrf_info_map_.end();
    bool withdraw = false;
    bool update = true;

    //Update lr uuid in case some vmi is deleted or added.
    UpdateLogicalRouterUuid(vn, vn_state);
    if (vn->IsDeleted() || (vn->GetVrf() == NULL)) {
        withdraw = true;
        update = false;
    }

    if (it != vrf_mapper_.vn_lr_set_.end() &&
        (it->second != vn_state->logical_router_uuid_) &&
        (vn_state->logical_router_uuid_ != nil_uuid())) {
        withdraw = true;
    }

    if (vn_state->logical_router_uuid_ == nil_uuid()) {
        withdraw = true;
        update = false;
    }

    if (it != vrf_mapper_.vn_lr_set_.end()) {
        routing_info_it = vrf_mapper_.lr_vrf_info_map_.find(it->second);
    }

    //Handles deletion case
    if (withdraw) {
        if (routing_info_it != vrf_mapper_.lr_vrf_info_map_.end()) {
            VxlanRoutingVrfMapper::RoutedVrfInfo::BridgeVnListIter br_it =
                routing_info_it->second.bridge_vn_list_.find(vn);
            if (br_it != routing_info_it->second.bridge_vn_list_.end()) {
                routing_info_it->second.bridge_vn_list_.erase(br_it);
            }
            //Trigger delete of logical router
            vrf_mapper_.TryDeleteLogicalRouter(routing_info_it);
        }
        vrf_mapper_.vn_lr_set_.erase(vn);
    }

    if (update) {
        vrf_mapper_.vn_lr_set_[vn] = vn_state->logical_router_uuid_;
        if (vrf_mapper_.vn_lr_set_[vn] == nil_uuid()) {
            return;
        }

        VxlanRoutingVrfMapper::RoutedVrfInfo &lr_vrf_info =
            vrf_mapper_.lr_vrf_info_map_[vn_state->logical_router_uuid_];
        lr_vrf_info.bridge_vn_list_.insert(vn);
    }

    //Without vrf walks cant be scheduled
    if (!vn_state->vrf_ref_.get()) {
        return;
    }

    //Walk Evpn table if withdraw or update was done
    if (update || withdraw) {
        EvpnAgentRouteTable *evpn_table =
            static_cast<EvpnAgentRouteTable *>(vn_state->vrf_ref_.get()->
                                               GetEvpnRouteTable());
        if (evpn_table) {
            vrf_mapper_.WalkEvpnTable(evpn_table);
        }
    }
    return;
}

void VxlanRoutingManager::RoutingVnNotify(const VnEntry *vn,
                                          VxlanRoutingVnState *vn_state) {

    bool withdraw = false;
    bool update = false;
    VxlanRoutingVrfMapper::VnLrSetIter it = vrf_mapper_.vn_lr_set_.find(vn);

    if (vn->IsDeleted() ||
        (vn->GetVrf() == NULL) ||
        (vn_state->is_routing_vn_ == false)) {
        update = false;
        withdraw = true;
    } else {
        update = true;
        if (it != vrf_mapper_.vn_lr_set_.end()) {
            //LR uuid changed, so withdraw from old and add new.
            if (it->second != vn_state->logical_router_uuid_) {
                withdraw = true;
            }
        }
    }

    if (withdraw && (it != vrf_mapper_.vn_lr_set_.end())) {
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
            }
            //Trigger delete of logical router
            vrf_mapper_.TryDeleteLogicalRouter(routing_info_it);
        }
        vrf_mapper_.vn_lr_set_.erase(it);
    }

    if (update) {
        if (vn_state->logical_router_uuid_ == boost::uuids::nil_uuid()) {
            return;
        }

        if (it == vrf_mapper_.vn_lr_set_.end()) {
            vrf_mapper_.vn_lr_set_[vn] = vn_state->logical_router_uuid_;
        }

        VxlanRoutingVrfMapper::RoutedVrfInfo &routed_vrf_info =
            vrf_mapper_.lr_vrf_info_map_[vn_state->logical_router_uuid_];
        //Take the ownership of LR
        routed_vrf_info.parent_vn_entry_ = vn;
        if (routed_vrf_info.routing_vrf_ != vn->GetVrf()) {
            routed_vrf_info.routing_vrf_ = vn->GetVrf();
            vrf_mapper_.WalkBridgeVrfs(routed_vrf_info);
        }
    }
}

void VxlanRoutingManager::VnWalkDone(DBTable::DBTableWalkRef walk_ref,
                                     DBTableBase *partition) {
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
    if (vrf->GetName().compare(agent_->fabric_vrf_name()) == 0)
        return;
    if (vrf->GetName().compare(agent_->fabric_policy_vrf_name()) == 0)
        return;

    VxlanRoutingState *state = dynamic_cast<VxlanRoutingState *>(vrf->
                             GetState(partition->parent(), vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (state) {
            HandleDefaultRoute(vrf, state->is_bridge_vrf_);
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
        if (vrf->vn() && vrf->vn()->vxlan_routing_vn()) {
            vrf->set_routing_vrf(true);
        }
    }

    HandleDefaultRoute(vrf, state->is_bridge_vrf_);
}

void VxlanRoutingManager::VmiNotify(DBTablePartBase *partition,
                                    DBEntryBase *e) {
    VmInterface *vmi = dynamic_cast<VmInterface *>(e);
    if (!vmi) {
        return;
    }

    VnEntry *vn = vmi->GetNonConstVn();
    VxlanRoutingVnState *vn_state = NULL;
    VxlanRoutingVmiState *vmi_state = dynamic_cast<VxlanRoutingVmiState *>(vmi->
                             GetAgentDBEntryState(vmi_listener_id_));
    if (vmi->IsDeleted() || (vn == NULL) ||
        (vmi->logical_router_uuid() == boost::uuids::nil_uuid())) {
        if (!vmi_state) {
            return;
        }
        vn = vmi_state->vn_entry_.get();
        vn_state = dynamic_cast<VxlanRoutingVnState *>
            (vn->GetAgentDBEntryState(vn_listener_id_));
        if (vn_state)
            vn_state->DeleteVmi(vn, vmi);
        vmi->ClearState(partition->parent(), vmi_listener_id_);
        delete vmi_state;
        return;
    }

    if ((vmi->device_type() != VmInterface::VMI_ON_LR) ||
        (vmi->vmi_type() != VmInterface::ROUTER)) {
        return;
    }

    if (vmi->logical_router_uuid() == boost::uuids::nil_uuid()) {
        return;
    }

    //Without VN no point of update
    if (!vn) {
        return;
    }

    if (!vmi_state) {
        vmi_state = new VxlanRoutingVmiState();
        vmi->SetState(partition->parent(), vmi_listener_id_, vmi_state);
        vmi_state->vn_entry_ = vn;
    }
    //Update logical_router_uuid
    vmi_state->logical_router_uuid_ = vmi->logical_router_uuid();

    //Its necessary to add state on VN so as to push VMI. VN notify can come
    //after VMI notify.
    VnNotify(vn->get_table_partition(), vn);
    //Now get VN state and add/delete VMI there
    vn_state = dynamic_cast<VxlanRoutingVnState *>
        (vn->GetAgentDBEntryState(vn_listener_id_));
    vn_state->AddVmi(vn, vmi);
}

void VxlanRoutingManager::UpdateEvpnType5Route(Agent *agent,
                                            const AgentRoute *route,
                                            const AgentPath *path,
                                            const VrfEntry *routing_vrf) {
    const InetUnicastRouteEntry *inet_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(routing_vrf->GetEvpnRouteTable());
    if (!evpn_table)
        return;

    //Add route in evpn table
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    NextHopKey *key = (static_cast<NextHopKey *>
        (path->nexthop()->GetDBRequestKey().get()))->Clone();
    InterfaceNHKey *intf_nh_key = static_cast<InterfaceNHKey *>(key);
    intf_nh_key->set_flags(intf_nh_key->flags() |
                           InterfaceNHFlags::VXLAN_ROUTING);
    nh_req.key.reset(intf_nh_key);
    nh_req.data.reset(new InterfaceNHData(routing_vrf->GetName()));
    evpn_table->AddType5Route(agent->local_vm_export_peer(),
                              routing_vrf->GetName(),
                              inet_rt->addr(),
                              routing_vrf->vxlan_id(),
                              new EvpnRoutingData(nh_req,
                                                  path->sg_list(),
                                                  path->communities(),
                                                  path->path_preference(),
                                                  path->ecmp_load_balance(),
                                                  path->tag_list(),
                                                  routing_vrf,
                                                  routing_vrf->vxlan_id(),
                                                  path->dest_vn_list()));
}

//Handles change in NH of local vm port path
//For all host routes with non local vm port path, evpn route in routing vrf
//need not be added. It should come from CN.
bool VxlanRoutingManager::InetRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const InetUnicastRouteEntry *inet_rt =
        dynamic_cast<const InetUnicastRouteEntry*>(e);
    if (inet_rt->vrf()->vn() == NULL)
        return true;

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
    //This delete aggresively handles delete on local vm peer port going off
    //before evpn routing path
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
    nh_req.key.reset((static_cast<NextHopKey *>
                     (evpn_rt->GetActiveNextHop()->GetDBRequestKey().get()))->
                     Clone());
    nh_req.data.reset(new InterfaceNHData(vrf->GetName()));
    const AgentPath *p = evpn_rt->GetActivePath();
    inet_table->AddEvpnRoutingRoute(evpn_rt->ip_addr(),
                                    evpn_rt->GetVmIpPlen(),
                                    vrf,
                                    agent_->evpn_routing_peer(),
                                    p->sg_list(),
                                    p->communities(),
                                    p->path_preference(),
                                    p->ecmp_load_balance(),
                                    p->tag_list(),
                                    nh_req,
                                    p->vxlan_id(),
                                    p->peer()->GetType() == Peer::BGP_PEER ?
                                    p->dest_vn_list() : p->evpn_dest_vn_list());
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

    InetUnicastRouteEntry key(bridge_vrf, ip_addr, evpn_rt->GetVmIpPlen(), false);
    // Find next highest matching route
    InetUnicastRouteEntry *inet_rt = inet_table->FindRouteUsingKey(key);
    if (inet_rt) {
        const EvpnRoutingPath *evpn_routing_path =
            static_cast<const EvpnRoutingPath *>(inet_rt->
                                             FindPath(agent_->evpn_routing_peer()));
        if (evpn_routing_path) {
            evpn_routing_path->DeleteEvpnType5Route(agent_, inet_rt);
        }
    }

    DBRequest nh_req(DBRequest::DB_ENTRY_DELETE);
    inet_table->Delete(agent_->evpn_routing_peer(),
                       bridge_vrf->GetName(), ip_addr,
                       evpn_rt->GetVmIpPlen(),
                       new EvpnRoutingData(nh_req,
                                           SecurityGroupList(),
                                           CommunityList(),
                                           PathPreference(),
                                           EcmpLoadBalance(),
                                           TagList(),
                                           NULL,
                                           0,
                                           VnListType()));
}

void VxlanRoutingManager::UpdateInetRoute(DBTablePartBase *partition,
                                          DBEntryBase *e,
                                          const VrfEntry *routing_vrf) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(e);
    VrfEntry *bridge_vrf = evpn_rt->vrf();
    const AgentPath *p = evpn_rt->GetActivePath();

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
                                    p->sg_list(),
                                    p->communities(),
                                    p->path_preference(),
                                    p->ecmp_load_balance(),
                                    p->tag_list(),
                                    nh_req,
                                    routing_vrf->vxlan_id(),
                                    p->dest_vn_list());
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
    assert(evpn_rt->IsType2() == true);

    if (evpn_rt->ip_addr().is_unspecified()) {
        return true;
    }

    const VrfEntry *routing_vrf =
        vrf_mapper_.GetRoutingVrfUsingEvpnRoute(evpn_rt);
    if (evpn_rt->IsDeleted() || !routing_vrf) {
        DeleteInetRoute(partition, e);
    } else {
        UpdateInetRoute(partition, e, routing_vrf);
    }
    return true;
}

bool VxlanRoutingManager::EvpnRouteNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const EvpnRouteEntry *evpn_rt =
        dynamic_cast<const EvpnRouteEntry *>(e);
    assert(evpn_rt != NULL);

    if (evpn_rt->is_multicast())
        return true;

    //In typer 5 mac is always zero
    if (evpn_rt->IsType5()) {
        // Allow EVPN type5 route for service-chain vrf. vrf()->vn() might
        // not be applicable for service-chain vrf.
        return EvpnType5RouteNotify(partition, e);
    } else if (evpn_rt->vrf()->vn()) {
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

void VxlanRoutingManager::HandleDefaultRoute(const VrfEntry *vrf, bool bridge_vrf) {
    if (vrf->vn() && (vrf->vn()->vxlan_routing_vn() == false)) {
        const VrfEntry *routing_vrf =
            vrf_mapper_.GetRoutingVrfUsingVn(vrf->vn());
        if (!routing_vrf || vrf->IsDeleted()) {
            DeleteDefaultRoute(vrf);
            vrf->vn()->set_lr_vrf(NULL);
        } else {
            UpdateDefaultRoute(vrf, routing_vrf);
            vrf->vn()->set_lr_vrf(routing_vrf);
        }
    } else if (bridge_vrf) {
        if (vrf->IsDeleted()) {
            DeleteDefaultRoute(vrf);
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
    DBRequest v4_nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    v4_nh_req.key.reset(new VrfNHKey(routing_vrf->GetName(), false, false));
    v4_nh_req.data.reset(new VrfNHData(false, false, false));
    bridge_vrf->GetInet4UnicastRouteTable()->
        AddEvpnRoutingRoute(Ip4Address(), 0, routing_vrf,
                            agent_->evpn_routing_peer(),
                            SecurityGroupList(),
                            CommunityList(),
                            PathPreference(),
                            EcmpLoadBalance(),
                            TagList(),
                            v4_nh_req,
                            routing_vrf->vxlan_id(),
                            VnListType());
    DBRequest v6_nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    v6_nh_req.key.reset(new VrfNHKey(routing_vrf->GetName(), false, false));
    v6_nh_req.data.reset(new VrfNHData(false, false, false));
    bridge_vrf->GetInet6UnicastRouteTable()->
        AddEvpnRoutingRoute(Ip6Address(), 0, routing_vrf,
                            agent_->evpn_routing_peer(),
                            SecurityGroupList(),
                            CommunityList(),
                            PathPreference(),
                            EcmpLoadBalance(),
                            TagList(),
                            v6_nh_req,
                            routing_vrf->vxlan_id(),
                            VnListType());
}

/**
 * sandesh request
 */
void VxlanRoutingManager::FillSandeshInfo(VxlanRoutingResp *resp) {
    VxlanRoutingVrfMapper::LrVrfInfoMapIter it1 =
       vrf_mapper_.lr_vrf_info_map_.begin();
    std::vector<VxlanRoutingMap> vr_map;
    while (it1 != vrf_mapper_.lr_vrf_info_map_.end()) {
        VxlanRoutingMap vxlan_routing_map;
        vxlan_routing_map.set_logical_router_uuid(UuidToString(it1->first));
        vxlan_routing_map.set_routing_vrf(it1->second.routing_vrf_->
                                            GetName());
        vxlan_routing_map.set_parent_routing_vn(it1->second.parent_vn_entry_->
                                            GetName());
        VxlanRoutingVrfMapper::RoutedVrfInfo::BridgeVnListIter it2 =
            it1->second.bridge_vn_list_.begin();
        while (it2 != it1->second.bridge_vn_list_.end()) {
            VxlanRoutingBridgeVrf bridge_vrf;
            if ((*it2)->GetVrf()) {
                bridge_vrf.set_bridge_vrf((*it2)->GetVrf()->GetName());
            }
            bridge_vrf.set_bridge_vn((*it2)->GetName());
            vxlan_routing_map.bridge_vrfs.push_back(bridge_vrf);
            it2++;
        }
        vr_map.push_back(vxlan_routing_map);
        it1++;
    }
    resp->set_vr_map(vr_map);
}

void VxlanRoutingReq::HandleRequest() const {
    VxlanRoutingResp *resp = new VxlanRoutingResp();
    Agent *agent = Agent::GetInstance();
    VxlanRoutingManager *vxlan_routing_mgr =
        agent->oper_db()->vxlan_routing_manager();
    if (vxlan_routing_mgr) {
        resp->set_context(context());
        vxlan_routing_mgr->FillSandeshInfo(resp);
    }
    resp->set_more(false);
    resp->Response();
    return;
}
