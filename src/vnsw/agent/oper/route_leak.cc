#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/vrouter.h>
#include <oper/route_leak.h>

void RouteLeakState::AddIndirectRoute(const AgentRoute *route) {
    InetUnicastAgentRouteTable *table = dest_vrf_->GetInet4UnicastRouteTable();
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = uc_rt->GetActivePath();
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(active_path->nexthop());

    if (nh == NULL) {
        return;
    }

    Ip4Address gw_ip = *(nh->GetDip());

    if (gw_ip == uc_rt->addr().to_v4() &&
        InetUnicastAgentRouteTable::FindResolveRoute(dest_vrf_->GetName(),
                                                     uc_rt->addr().to_v4())) {
        bool resolved = false;
        MacAddress mac;
        const Interface *itf = agent_->vhost_interface();
        ArpNHKey nh_key(dest_vrf_->GetName(), uc_rt->addr().to_v4(), false);
        ArpNH *arp_nh = static_cast<ArpNH *>(agent_->nexthop_table()->
                                             FindActiveEntry(&nh_key));
        if (arp_nh) {
            resolved = arp_nh->GetResolveState();
            mac = arp_nh->GetMac();
            itf = arp_nh->GetInterface();
        }
        InetUnicastAgentRouteTable::CheckAndAddArpRoute
            (dest_vrf_->GetName(), uc_rt->addr().to_v4(), mac, itf,
             resolved, active_path->dest_vn_list(),
             active_path->sg_list(), active_path->tag_list());
        return;
    }

    const Peer *peer = agent_->local_peer();
    peer_list_.insert(peer);

    if (gw_ip == uc_rt->addr().to_v4()) {
        gw_ip = agent_->vhost_default_gateway();
    }

    table->AddGatewayRoute(peer, dest_vrf_->GetName(),
                           uc_rt->addr().to_v4(),
                           uc_rt->plen(),
                           gw_ip,
                           active_path->dest_vn_list(),
                           MplsTable::kInvalidExportLabel,
                           active_path->sg_list(),
                           active_path->tag_list(),
                           active_path->communities(), true);
}

void RouteLeakState::AddInterfaceRoute(const AgentRoute *route,
                                       const AgentPath *path) {
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = path;

    if (active_path == NULL) {
        active_path = uc_rt->GetActivePath();
    }

    InterfaceNH *intf_nh = dynamic_cast<InterfaceNH *>(active_path->nexthop());
    if (intf_nh == NULL) {
        return;
    }

    if (uc_rt->IsHostRoute() &&
        uc_rt->addr() == agent_->router_id()) {
        //Dont overwrite vhost IP in default VRF
        if (intf_nh->GetInterface() != agent_->vhost_interface()) {
            return;
        }
    }

    if (intf_nh->GetInterface()->type() == Interface::PACKET) {
        peer_list_.insert(agent_->local_peer());
        InetUnicastAgentRouteTable *table =
            static_cast<InetUnicastAgentRouteTable *>
            (dest_vrf_->GetInet4UnicastRouteTable());

        table->AddHostRoute(dest_vrf_->GetName(), uc_rt->addr(), uc_rt->plen(),
                            "", true);
        return;
    }

    if (intf_nh->GetInterface()->type() == Interface::VM_INTERFACE) {
        const VmInterface *vm_intf =
            static_cast<const VmInterface *>(intf_nh->GetInterface());
        if (vm_intf->vmi_type() == VmInterface::VHOST) {
            if (uc_rt->addr() == agent_->router_id()) {
                if (uc_rt->FindLocalVmPortPath() == NULL) {
                    peer_list_.insert(agent_->local_peer());
                } else {
                    peer_list_.insert(agent_->fabric_rt_export_peer());
                }
                AddReceiveRoute(route);
                return;
            }
        }
    }

    const Peer *peer = active_path->peer();
    if (uc_rt->FindLocalVmPortPath() == NULL) {
        peer = agent_->local_peer();
    }

    /* Don't export /32 routes on fabric-vrf, if they are part of vrouter's
     * subnet list. To disable export, use local_peer */
    if ((uc_rt->IsHostRoute()) &&
        dest_vrf_->GetName() == agent_->fabric_vrf_name()) {
        if (agent_->oper_db()->vrouter()->IsSubnetMember(uc_rt->addr())) {
            peer = agent_->local_peer();
        }
    }

    peer_list_.insert(peer);
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_nh->GetIfUuid(),
            intf_nh->GetInterface()->name());
    LocalVmRoute *local_vm_route = NULL;
    local_vm_route =
        new LocalVmRoute(intf_key, MplsTable::kInvalidExportLabel,
                         VxLanTable::kInvalidvxlan_id, false,
                         active_path->dest_vn_list(), InterfaceNHFlags::INET4,
                         SecurityGroupList(),
                         TagList(),
                         CommunityList(),
                         active_path->path_preference(),
                         Ip4Address(0), EcmpLoadBalance(),
                         false, false, peer->sequence_number(),
                         false, true);
    local_vm_route->set_native_vrf_id(uc_rt->vrf()->rd());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, dest_vrf_->GetName(),
                                          uc_rt->addr(), uc_rt->plen()));
    req.data.reset(local_vm_route);

    AgentRouteTable *table =
        agent_->vrf_table()->GetInet4UnicastRouteTable(dest_vrf_->GetName());
    if (table) {
        table->Process(req);
    }
}

void RouteLeakState::AddCompositeRoute(const AgentRoute *route) {
    for(Route::PathList::const_iterator it = route->GetPathList().begin();
        it != route->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
            AddInterfaceRoute(route, path);
        }
    }
}

void RouteLeakState::AddReceiveRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = uc_rt->GetActivePath();

    /* This is a defensive check added to prevent the code below from casting NHs
     * not containing an interface to RECEIVE NH */
    const NextHop* nh = active_path->nexthop();
    if ((nh->GetType() != NextHop::INTERFACE) &&
        (nh->GetType() != NextHop::RECEIVE)) {
        return;
    }

    const ReceiveNH *rch_nh =
        static_cast<const ReceiveNH*>(active_path->nexthop());
    const VmInterface *vm_intf =
        static_cast<const VmInterface *>(rch_nh->GetInterface());

    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(
                dest_vrf_->GetInet4UnicastRouteTable());

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, vm_intf->GetUuid(),
                           vm_intf->name());
    table->AddVHostRecvRoute(agent_->fabric_rt_export_peer(),
                             dest_vrf_->GetName(),
                             vmi_key,
                             uc_rt->addr(),
                             uc_rt->plen(),
                             agent_->fabric_vn_name(), false, true);
}

bool RouteLeakState::CanAdd(const InetUnicastRouteEntry *rt) {
    //Never replace resolve route and default route
    InetUnicastAgentRouteTable *table =
        agent_->fabric_vrf()->GetInet4UnicastRouteTable();

    if (rt->addr() == Ip4Address(0) && rt->plen() == 0) {
        return false;
    }

    InetUnicastRouteEntry *rsl_rt = table->FindResolveRoute(rt->addr().to_v4());
    if (rsl_rt && rt->addr() == rsl_rt->addr() &&
        rt->plen() == rsl_rt->plen()) {
        //Dont overwrite resolve route
        return false;
    }

    if (rt->IsHostRoute() &&
        rt->addr() == agent_->vhost_default_gateway()) {
        return false;
    }

    //Always add gateway and DNS routes
    const InterfaceNH *nh =
        dynamic_cast<const InterfaceNH *>(rt->GetActiveNextHop());
    if (nh && nh->GetInterface()->type() == Interface::PACKET) {
        return true;
    }

    if ((rt->GetActivePath()->tunnel_bmap() & TunnelType::NativeType()) == 0) {
        return false;
    }

    return true;
}

void RouteLeakState::AddRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);

    if (CanAdd(uc_rt) == false) {
        DeleteRoute(route, peer_list_);
        return;
    }

    std::set<const Peer *> old_peer_list = peer_list_;
    peer_list_.clear();

    if (uc_rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL) {
        AddIndirectRoute(route);
    } else if ((uc_rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE)||
            (route->FindLocalVmPortPath() &&
             route->FindLocalVmPortPath()->nexthop() &&
             route->FindLocalVmPortPath()->nexthop()->GetType()
                            == NextHop::COMPOSITE)) {
        AddCompositeRoute(route);
    } else if (uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE) {
        AddInterfaceRoute(route, route->FindLocalVmPortPath());
    }

    bool sync = false;
    if (old_peer_list != peer_list_) {
        sync = true;
    }

    std::set<const Peer *>::iterator it = old_peer_list.begin();
    while(it != old_peer_list.end()) {
        std::set<const Peer *>::iterator prev_it = it;
        it++;
        if (peer_list_.find(*prev_it) != peer_list_.end()) {
            old_peer_list.erase(prev_it);
        }
    }

    DeleteRoute(route, old_peer_list);

    if (sync) {
        InetUnicastAgentRouteTable *table =
            dest_vrf_->GetInet4UnicastRouteTable();
        table->ResyncRoute(agent_->fabric_rt_export_peer(),
                           dest_vrf_->GetName(), uc_rt->addr(), uc_rt->plen());
    }
}

void RouteLeakState::DeleteRoute(const AgentRoute *route,
                                 const std::set<const Peer *> &peer_list) {
    if (dest_vrf_ == NULL) {
        return;
    }

    std::set<const Peer *>::const_iterator it = peer_list.begin();
    for(; it != peer_list.end(); it++) {
        const InetUnicastRouteEntry *uc_rt =
            static_cast<const InetUnicastRouteEntry *>(route);
        dest_vrf_->GetInet4UnicastRouteTable()->Delete(*it,
                                                       dest_vrf_->GetName(),
                                                       uc_rt->addr(),
                                                       uc_rt->plen());
    }
}

RouteLeakVrfState::RouteLeakVrfState(VrfEntry *source_vrf,
                                     VrfEntry *dest_vrf):
    source_vrf_(source_vrf), dest_vrf_(dest_vrf), deleted_(false) {

    AgentRouteTable *table = source_vrf->GetInet4UnicastRouteTable();
    route_listener_id_ =  table->Register(boost::bind(&RouteLeakVrfState::Notify,
                                                      this, _1, _2));

    //Walker would be used to address change of dest VRF table
    //Everytime dest vrf change all the route from old dest VRF
    //would be deleted and added to new dest VRF if any
    //If VRF is deleted upon walk done state would be deleted.
    walk_ref_ = table->AllocWalker(
                    boost::bind(&RouteLeakVrfState::WalkCallBack, this, _1, _2),
                    boost::bind(&RouteLeakVrfState::WalkDoneInternal, this, _2));
    table->WalkTable(walk_ref_);
}

RouteLeakVrfState::~RouteLeakVrfState() {
    source_vrf_->GetInet4UnicastRouteTable()->ReleaseWalker(walk_ref_);
    source_vrf_->GetInet4UnicastRouteTable()->Unregister(route_listener_id_);
}

void RouteLeakVrfState::WalkDoneInternal(DBTableBase *part) {
    if (deleted_) {
        delete this;
    }
}

bool RouteLeakVrfState::WalkCallBack(DBTablePartBase *partition, DBEntryBase *entry) {
    Notify(partition, entry);
    return true;
}

void RouteLeakVrfState::AddDefaultRoute() {
    InetUnicastAgentRouteTable *table = source_vrf_->GetInet4UnicastRouteTable();

    VnListType vn_list;
    vn_list.insert(table->agent()->fabric_vn_name());

    table->AddGatewayRoute(table->agent()->local_peer(),
                           source_vrf_->GetName(), Ip4Address(0), 0,
                           table->agent()->vhost_default_gateway(), vn_list,
                           MplsTable::kInvalidLabel, SecurityGroupList(),
                           TagList(), CommunityList(), true);
}

void RouteLeakVrfState::DeleteDefaultRoute() {
    InetUnicastAgentRouteTable *table = source_vrf_->GetInet4UnicastRouteTable();
    table->Delete(table->agent()->local_peer(), source_vrf_->GetName(),
                  Ip4Address(0), 0);
}

void RouteLeakVrfState::Delete() {
    deleted_ = true;
    source_vrf_->GetInet4UnicastRouteTable()->WalkAgain(walk_ref_);
    DeleteDefaultRoute();
}

bool RouteLeakVrfState::Notify(DBTablePartBase *partition, DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    RouteLeakState *state =
        static_cast<RouteLeakState *>(entry->GetState(partition->parent(),
                                                      route_listener_id_));

    if (route->IsDeleted() || deleted_) {
        if (state) {
            //Delete the route
            entry->ClearState(partition->parent(), route_listener_id_);
            state->DeleteRoute(route, state->peer_list());
            delete state;
        }
        return true;
    }

    if (state == NULL && dest_vrf_) {
        state = new RouteLeakState(dest_vrf_->GetInet4UnicastRouteTable()->agent(),
                                   NULL);
        route->SetState(partition->parent(), route_listener_id_, state);
    }

    if (state == NULL) {
        return true;
    }

    if (state->dest_vrf() != dest_vrf_) {
        state->DeleteRoute(route, state->peer_list());
    }

    if (state->dest_vrf() != dest_vrf_) {
        //Add the route in new VRF
        state->set_dest_vrf(dest_vrf_.get());
    }

    if (state->dest_vrf()) {
        state->AddRoute(route);
    }
    return true;
}

void RouteLeakVrfState::SetDestVrf(VrfEntry *vrf) {
    if (dest_vrf_ != vrf) {
        dest_vrf_ = vrf;
        source_vrf_->GetInet4UnicastRouteTable()->WalkAgain(walk_ref_);
    }

    if (vrf == NULL) {
        DeleteDefaultRoute();
    } else {
        AddDefaultRoute();
    }
}

RouteLeakManager::RouteLeakManager(Agent *agent): agent_(agent) {
    vrf_listener_id_ = agent_->vrf_table()->Register(
                           boost::bind(&RouteLeakManager::Notify, this, _1, _2));
}

RouteLeakManager::~RouteLeakManager() {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
}

void RouteLeakManager::Notify(DBTablePartBase *partition, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    RouteLeakVrfState *state =
        static_cast<RouteLeakVrfState *>(entry->GetState(partition->parent(),
                                                         vrf_listener_id_));

    if (vrf->IsDeleted()) {
        if (state) {
            entry->ClearState(partition->parent(), vrf_listener_id_);
            state->Delete();
        }
        return;
    }


    if (state == NULL && vrf->forwarding_vrf()) {
        state = new RouteLeakVrfState(vrf, NULL);
    }

    if (state == NULL) {
        return;
    }

    vrf->SetState(partition->parent(), vrf_listener_id_, state);

    if (vrf->forwarding_vrf() != state->dest_vrf()) {
        state->SetDestVrf(vrf->forwarding_vrf());
    }
}

void RouteLeakManager::ReEvaluateRouteExports() {
    if (vrf_walk_ref_.get() == NULL) {
        vrf_walk_ref_ = agent_->vrf_table()->AllocWalker(
            boost::bind(&RouteLeakManager::VrfWalkNotify, this, _1, _2),
            boost::bind(&RouteLeakManager::VrfWalkDone, this, _2));
    }
    agent_->vrf_table()->WalkAgain(vrf_walk_ref_);
}

bool RouteLeakManager::VrfWalkNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    RouteLeakVrfState *state =
        static_cast<RouteLeakVrfState *>(e->GetState(partition->parent(),
                                                     vrf_listener_id_));
    if (vrf->IsDeleted()) {
        return true;
    }
    /* Ignore VRFs on which routes are not leaked by RouteLeakManager */
    if (state == NULL) {
        return true;
    }
    if (state->deleted()) {
        return true;
    }

    StartRouteWalk(vrf, state);
    return true;
}

void RouteLeakManager::VrfWalkDone(DBTableBase *part) {
}

void RouteLeakManager::StartRouteWalk(VrfEntry *vrf, RouteLeakVrfState *state) {
    InetUnicastAgentRouteTable *table = vrf->GetInet4UnicastRouteTable();
    if (!table) {
        return;
    }
    DBTable::DBTableWalkRef rt_table_walk_ref = table->AllocWalker(
        boost::bind(&RouteLeakVrfState::Notify, state, _1, _2),
        boost::bind(&RouteLeakManager::RouteWalkDone, this, _2));
    table->WalkAgain(rt_table_walk_ref);
}

void RouteLeakManager::RouteWalkDone(DBTableBase *part) {
}
