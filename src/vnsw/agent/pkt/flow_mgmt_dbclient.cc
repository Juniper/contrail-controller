#include "pkt/flow_mgmt_dbclient.h"

void FlowMgmtDbClient::Init() {
    acl_listener_id_ = agent_->acl_table()->Register
        (boost::bind(&FlowMgmtDbClient::AclNotify, this, _1, _2));

    interface_listener_id_ = agent_->interface_table()->Register
        (boost::bind(&FlowMgmtDbClient::InterfaceNotify, this, _1, _2));

    vn_listener_id_ = agent_->vn_table()->Register
        (boost::bind(&FlowMgmtDbClient::VnNotify, this, _1, _2));

    vrf_listener_id_ = agent_->vrf_table()->Register
            (boost::bind(&FlowMgmtDbClient::VrfNotify, this, _1, _2));

    nh_listener_id_ = agent_->nexthop_table()->Register
            (boost::bind(&FlowMgmtDbClient::NhNotify, this, _1, _2));
    return;
}

void FlowMgmtDbClient::Shutdown() {
    agent_->acl_table()->Unregister(acl_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->nexthop_table()->Unregister(nh_listener_id_);
}

FlowMgmtDbClient::FlowMgmtDbClient(Agent *agent, FlowMgmtManager *mgr) :
    agent_(agent),
    mgr_(mgr),
    acl_listener_id_(),
    interface_listener_id_(),
    vn_listener_id_(),
    vm_listener_id_(),
    vrf_listener_id_(),
    nh_listener_id_() {
}

FlowMgmtDbClient::~FlowMgmtDbClient() {
}

void FlowMgmtDbClient::AddEvent(const DBEntry *entry, FlowMgmtState *state) {
    mgr_->AddEvent(entry, state->gen_id_);
}

void FlowMgmtDbClient::DeleteEvent(const DBEntry *entry, FlowMgmtState *state) {
    state->gen_id_++;
    mgr_->DeleteEvent(entry, state->gen_id_);
}

void FlowMgmtDbClient::ChangeEvent(const DBEntry *entry, FlowMgmtState *state) {
    mgr_->ChangeEvent(entry, state->gen_id_);
}

////////////////////////////////////////////////////////////////////////////
// Interface notification handler
////////////////////////////////////////////////////////////////////////////
static DBState *ValidateGenId(DBTableBase *table, DBEntry *entry,
                              DBTableBase::ListenerId id, uint32_t gen_id) {
    FlowMgmtDbClient::FlowMgmtState *state =
        static_cast<FlowMgmtDbClient::FlowMgmtState *>(entry->GetState(table,
                                                                      id));
    if (state == NULL)
        return NULL;

    if (state->gen_id_ > gen_id)
        return NULL;

    return state;
}

void FlowMgmtDbClient::FreeInterfaceState(Interface *intf, uint32_t gen_id) {
    VmInterface *vm_port = dynamic_cast<VmInterface *>(intf);
    if (vm_port == NULL)
        return;

    if ((intf->IsDeleted() == false) && (vm_port->vn() != NULL))
        return;

    DBState *state = ValidateGenId(intf->get_table(), intf,
                                   interface_listener_id_, gen_id);
    if (state == NULL)
        return;

    intf->ClearState(intf->get_table(), interface_listener_id_);
    delete state;
}

void FlowMgmtDbClient::InterfaceNotify(DBTablePartBase *part, DBEntryBase *e) {
    Interface *intf = static_cast<Interface *>(e);
    if (intf->type() != Interface::VM_INTERFACE) {
        return;
    }

    VmInterface *vm_port = static_cast<VmInterface *>(intf);
    const VnEntry *new_vn = vm_port->vn();

    VmIntfFlowHandlerState *state = static_cast<VmIntfFlowHandlerState *>
        (e->GetState(part->parent(), interface_listener_id_));
    if (intf->IsDeleted() || new_vn == NULL) {
        if (state) {
            DeleteEvent(vm_port, state);
        }
        return;
    }

    const VmInterface::SecurityGroupEntryList &new_sg_l = vm_port->sg_list();
    bool changed = false;

    if (state == NULL) {
        state = new VmIntfFlowHandlerState(NULL);
        e->SetState(part->parent(), interface_listener_id_, state);
        // Force change for first time
        state->policy_ = vm_port->policy_enabled();
        state->sg_l_ = new_sg_l;
        state->vn_ = new_vn;
        state->vrf_assign_acl_ = vm_port->vrf_assign_acl();
        changed = true;
    } else {
        if (state->vn_.get() != new_vn) {
            changed = true;
            state->vn_ = new_vn;
        }
        if (state->policy_ != vm_port->policy_enabled()) {
            changed = true;
            state->policy_ = vm_port->policy_enabled();
        }
        if (state->sg_l_.list_ != new_sg_l.list_) {
            changed = true;
            state->sg_l_ = new_sg_l;
        }
        if (state->vrf_assign_acl_.get() != vm_port->vrf_assign_acl()) {
            changed = true;
            state->vrf_assign_acl_ = vm_port->vrf_assign_acl();
        }
    }

    if (changed) {
        AddEvent(vm_port, state);
    }
}

////////////////////////////////////////////////////////////////////////////
// VN notification handler
////////////////////////////////////////////////////////////////////////////
void FlowMgmtDbClient::FreeVnState(VnEntry *vn, uint32_t gen_id) {
    if (vn->IsDeleted() == false) {
        return;
    }

    DBState *state = ValidateGenId(vn->get_table(), vn, vn_listener_id_,
                                   gen_id);
    if (state == NULL)
        return;

    vn->ClearState(vn->get_table(), vn_listener_id_);
    delete state;
}

void FlowMgmtDbClient::VnNotify(DBTablePartBase *part, DBEntryBase *e) {
    // Add/Delete Acl:
    // Resync all Vn flows with new VN network policies
    VnEntry *vn = static_cast<VnEntry *>(e);
    VnFlowHandlerState *state = static_cast<VnFlowHandlerState *>
        (e->GetState(part->parent(), vn_listener_id_));
    AclDBEntryConstRef acl = NULL;
    AclDBEntryConstRef macl = NULL;
    AclDBEntryConstRef mcacl = NULL;
    bool enable_rpf = true;
    bool flood_unknown_unicast = false;

    if (vn->IsDeleted()) {
        if (state) {
            DeleteEvent(vn, state);
        }
        return;
    }

    bool changed = false;
    if (state != NULL) { 
        acl = state->acl_;
        macl = state->macl_;
        mcacl = state->mcacl_;
        enable_rpf = state->enable_rpf_;
        flood_unknown_unicast = state->flood_unknown_unicast_;
    }

    const AclDBEntry *new_acl = vn->GetAcl();
    const AclDBEntry *new_macl = vn->GetMirrorAcl();
    const AclDBEntry *new_mcacl = vn->GetMirrorCfgAcl();
    bool new_enable_rpf = vn->enable_rpf();
    bool new_flood_unknown_unicast = vn->flood_unknown_unicast();
    
    if (state == NULL) {
        state = new VnFlowHandlerState(new_acl, new_macl, new_mcacl,
                                       new_enable_rpf,
                                       new_flood_unknown_unicast);
        e->SetState(part->parent(), vn_listener_id_, state);
        changed = true;
    }

    if (acl != new_acl || macl != new_macl || mcacl !=new_mcacl ||
        enable_rpf != new_enable_rpf ||
        flood_unknown_unicast != new_flood_unknown_unicast) {
        state->acl_ = new_acl;
        state->macl_ = new_macl;
        state->mcacl_ = new_mcacl;
        state->enable_rpf_ = new_enable_rpf;
        state->flood_unknown_unicast_ = new_flood_unknown_unicast;
        changed = true;
    }

    if (changed) {
        AddEvent(vn, state);
    }
}

////////////////////////////////////////////////////////////////////////////
// ACL notification handler
////////////////////////////////////////////////////////////////////////////
void FlowMgmtDbClient::FreeAclState(AclDBEntry *acl, uint32_t gen_id) {
    if (acl->IsDeleted() == false)
        return;

    DBState *state = ValidateGenId(acl->get_table(), acl, acl_listener_id_,
                                   gen_id);
    if (state == NULL)
        return;

    acl->ClearState(acl->get_table(), acl_listener_id_);
    delete state;
}

void FlowMgmtDbClient::AclNotify(DBTablePartBase *part, DBEntryBase *e) {
    AclDBEntry *acl = static_cast<AclDBEntry *>(e);
    AclFlowHandlerState *state =
        static_cast<AclFlowHandlerState *>(e->GetState(part->parent(),
                                                       acl_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            DeleteEvent(acl, state);
        }
        return;
    }

    if (!state) {
        state = new AclFlowHandlerState();
        e->SetState(part->parent(), acl_listener_id_, state);
    }
    AddEvent(acl, state);
}

////////////////////////////////////////////////////////////////////////////
// NH notification handler
////////////////////////////////////////////////////////////////////////////
void FlowMgmtDbClient::FreeNhState(NextHop *nh, uint32_t gen_id) {
    if (nh->IsDeleted() == false)
        return;

    DBState *state = ValidateGenId(nh->get_table(), nh, nh_listener_id_,
                                   gen_id);
    if (state == NULL)
        return;

    nh->ClearState(nh->get_table(), nh_listener_id_);
    delete state;
}

void FlowMgmtDbClient::NhNotify(DBTablePartBase *part, DBEntryBase *e) {
    NextHop *nh = static_cast<NextHop *>(e);
    NhFlowHandlerState *state =
        static_cast<NhFlowHandlerState *>(e->GetState(part->parent(),
                                                      nh_listener_id_));

    if (nh->IsDeleted()) {
        if (state) {
            DeleteEvent(nh, state);
        }
        return;
    }

    if (!state) {
        state = new NhFlowHandlerState();
        nh->SetState(part->parent(), nh_listener_id_, state);
    }
    AddEvent(nh, state);
    return;
}

////////////////////////////////////////////////////////////////////////////
// VRF Notification handlers
////////////////////////////////////////////////////////////////////////////
void FlowMgmtDbClient::FreeVrfState(VrfEntry *vrf, uint32_t gen_id) {
    if (vrf->IsDeleted() == false)
        return;

    VrfFlowHandlerState *state = static_cast<VrfFlowHandlerState *>
        (ValidateGenId(vrf->get_table(), vrf, vrf_listener_id_, gen_id));
    if (state == NULL)
        return;

    state->Unregister(vrf);
    vrf->ClearState(vrf->get_table(), vrf_listener_id_);
    delete state;
}

void FlowMgmtDbClient::VrfFlowHandlerState::Unregister(VrfEntry *vrf) {
    // Register to the Inet4 Unicast Table
    InetUnicastAgentRouteTable *inet_table =
        static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    inet_table->Unregister(inet_listener_id_);

    inet_table = static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet6UnicastRouteTable());
    inet_table->Unregister(inet6_listener_id_);

    // Register to the Bridge Unicast Table
    BridgeAgentRouteTable *bridge_table =
        static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    bridge_table->Unregister(bridge_listener_id_);
    LOG(DEBUG, "ROUTE-TABLE-UNREGISTER"
        << " Inet Listener-Id: " << inet_listener_id_
        << " Inet6 Listener-Id: " << inet6_listener_id_
        << " Bridge Listener-Id: " << bridge_listener_id_);
}

void FlowMgmtDbClient::VrfFlowHandlerState::Register(FlowMgmtDbClient *client,
                                                     VrfEntry *vrf) {
    // Register to the Inet4 Unicast Table
    InetUnicastAgentRouteTable *inet_table =
        static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());

    inet_listener_id_ =
        inet_table->Register(boost::bind(&FlowMgmtDbClient::RouteNotify, client,
                                         this, Agent::INET4_UNICAST, _1, _2));

    inet_table = static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet6UnicastRouteTable());
    inet6_listener_id_ =
        inet_table->Register(boost::bind(&FlowMgmtDbClient::RouteNotify, client,
                                         this, Agent::INET6_UNICAST, _1, _2));

    // Register to the Bridge Unicast Table
    BridgeAgentRouteTable *bridge_table =
        static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    bridge_listener_id_ =
        bridge_table->Register(boost::bind(&FlowMgmtDbClient::RouteNotify,
                                           client, this, Agent::BRIDGE, _1,
                                           _2));

    LOG(DEBUG, "ROUTE-TABLE-REGISTER"
        << " Inet Listener-Id: " << inet_listener_id_
        << " Inet6 Listener-Id: " << inet6_listener_id_
        << " Bridge Listener-Id: " << bridge_listener_id_);
}

void FlowMgmtDbClient::VrfNotify(DBTablePartBase *part, DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    VrfFlowHandlerState *state = static_cast<VrfFlowHandlerState *>
        (e->GetState(part->parent(), vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (state ) {
            DeleteEvent(vrf, state);
        }
        return;
    }
    if (state == NULL) {
        state = new VrfFlowHandlerState();
        state->Register(this, vrf);
        vrf->SetState(part->parent(), vrf_listener_id_, state);
        AddEvent(vrf, state);
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////
// FlowTableRequest handlers for Routes
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtDbClient::TraceMsg(AgentRoute *entry, const AgentPath *path,
                         const SecurityGroupList &sg_list, bool deleted) {
    InetUnicastRouteEntry *inet = dynamic_cast<InetUnicastRouteEntry *>(entry);
    if (inet) {
        FLOW_TRACE(RouteUpdate,
                   inet->vrf()->GetName(),
                   inet->addr().to_string(),
                   inet->plen(),
                   path ? path->dest_vn_name() : "",
                   inet->IsDeleted(),
                   deleted,
                   sg_list.size(),
                   sg_list);
    }

    BridgeRouteEntry *bridge = dynamic_cast<BridgeRouteEntry *>(entry);
    if (bridge) {
        FLOW_TRACE(RouteUpdate,
                   bridge->vrf()->GetName(),
                   bridge->mac().ToString(),
                   bridge->plen(),
                   path ? path->dest_vn_name() : "",
                   bridge->IsDeleted(),
                   deleted,
                   sg_list.size(),
                   sg_list);
    }
}

void FlowMgmtDbClient::FreeRouteState(AgentRoute *route, uint32_t gen_id) {
    if (route->IsDeleted() == false)
        return;

    VrfEntry *vrf = route->vrf();
    VrfFlowHandlerState *vrf_state = static_cast<VrfFlowHandlerState *>
        (vrf->GetState(vrf->get_table(), vrf_listener_id_));
    if (vrf_state == NULL)
        return;

    DBTableBase::ListenerId id;
    if (dynamic_cast<InetUnicastRouteEntry *>(route)) {
        if (route->GetTableType() == Agent::INET4_UNICAST)
            id = vrf_state->inet_listener_id_;
        else
            id = vrf_state->inet6_listener_id_;
    } else if (dynamic_cast<BridgeRouteEntry *>(route)) {
        id = vrf_state->bridge_listener_id_;
    } else {
        return;
    }

    DBState *state = ValidateGenId(route->get_table(), route, id, gen_id);
    if (state == NULL)
        return;

    route->ClearState(route->get_table(), id);
    delete state;
}

bool FlowMgmtDbClient::HandleTrackingIpChange(const AgentRoute *rt,
                                              RouteFlowHandlerState *state) {
    bool ret = false;
    RouteFlowHandlerState::FixedIpMap new_map;

    //Maintain a list of interface to fixed-ip mapping for
    //a given route, we need this map because there can be
    //multiple path from different local vm path peer.
    //If the route has fixed-ip change then all the the flows
    //dependent on this route will be reevaluated.
    for(Route::PathList::const_iterator it = rt->GetPathList().begin();
            it != rt->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
            continue;
        }

        if (path->nexthop() == NULL ||
            path->nexthop()->GetType() != NextHop::INTERFACE) {
            continue;
        }

        const InterfaceNH *nh = static_cast<InterfaceNH *>(path->nexthop());
        InterfaceConstRef intf = nh->GetInterface();

        IpAddress new_fixed_ip = path->GetFixedIp();
        if (new_fixed_ip == Ip4Address(0)) {
            continue;
        }

        new_map.insert(RouteFlowHandlerState::FixedIpEntry(intf, new_fixed_ip));

        RouteFlowHandlerState::FixedIpMap::const_iterator old_it =
            state->fixed_ip_map_.find(intf);
        if (old_it != state->fixed_ip_map_.end()) {
            if (new_fixed_ip != old_it->second) {
                ret = true;
            }
        }
    }
    
    //Check if any path has been deleted
    RouteFlowHandlerState::FixedIpMap::const_iterator old_it =
        state->fixed_ip_map_.begin();
    for (;old_it != state->fixed_ip_map_.end(); old_it++) {
        if (new_map.find(old_it->first) == new_map.end()) {
            ret = true;
            break;
        }
    }

    state->fixed_ip_map_ = new_map;
    return ret;
}

void FlowMgmtDbClient::RouteNotify(VrfFlowHandlerState *vrf_state,
                                   Agent::RouteTableType type,
                                   DBTablePartBase *partition, DBEntryBase *e) {
    DBTableBase::ListenerId id = vrf_state->GetListenerId(type);
    RouteFlowHandlerState *state =
        static_cast<RouteFlowHandlerState *>(e->GetState(partition->parent(),
                                                         id));
    AgentRoute *route = static_cast<AgentRoute *>(e);
    const AgentPath *path = route->GetActivePath();
    SecurityGroupList new_sg_l;
    // Get new sg-list. Sort, the sg-list to aid in comparison
    if (path) {
        new_sg_l = route->GetActivePath()->sg_list();
        sort(new_sg_l.begin(), new_sg_l.end());
    }
    TraceMsg(route, path, new_sg_l, vrf_state->deleted_);

    if (route->IsDeleted()) {
        if (state) {
            DeleteEvent(route, state);
        }
        return;
    }

    if (route->is_multicast()) {
        return;
    }

    bool new_route = false;
    if (state == NULL) {
        state  = new RouteFlowHandlerState();
        route->SetState(partition->parent(), id, state);
        AddEvent(route, state);
        new_route = true;
    }

    bool changed = false;
    // Handle SG change
    if (state->sg_l_ != new_sg_l) {
        state->sg_l_ = new_sg_l;
        changed = true;
    }

    //Trigger RPF NH sync, if active nexthop changes
    const NextHop *active_nh = route->GetActiveNextHop();
    const NextHop *local_nh = NULL;
    if (active_nh->GetType() == NextHop::COMPOSITE) {
        InetUnicastRouteEntry *inet_route =
            dynamic_cast<InetUnicastRouteEntry *>(route);
        assert(inet_route);
        //If destination is ecmp, all remote flow would
        //have RPF NH set to that local component NH
        local_nh = inet_route->GetLocalNextHop();
    }

    if ((state->active_nh_ != active_nh) || (state->local_nh_ != local_nh)) {
        state->active_nh_ = active_nh;
        state->local_nh_ = local_nh;
        changed = true;
    }

    if (HandleTrackingIpChange(route, state)) {
        //Tracking IP change can result in flow change
        //i.e in case of NAT new reverse flow might be
        //created, hence enqueue a ADD event so that flow will
        //be reevaluated.
        if (new_route == false) {
            AddEvent(route, state);
        }
    }

    if (changed == true && new_route == false) {
        ChangeEvent(route, state);
    }
}

/////////////////////////////////////////////////////////////////////////////
// FlowTableRequest message handler
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtDbClient::FreeDBState(const DBEntry *entry, uint32_t gen_id) {
    if (dynamic_cast<const Interface *>(entry)) {
        DBTable *table = agent_->interface_table();
        Interface *intf = static_cast<Interface *>(table->Find(entry));
        FreeInterfaceState(intf, gen_id);
        return true;
    }

    if (dynamic_cast<const VnEntry *>(entry)) {
        DBTable *table = agent_->vn_table();
        VnEntry *vn = static_cast<VnEntry *>(table->Find(entry));
        FreeVnState(vn, gen_id);
        return true;
    }

    if (dynamic_cast<const AclDBEntry *>(entry)) {
        DBTable *table = agent_->acl_table();
        AclDBEntry *acl = static_cast<AclDBEntry *> (table->Find(entry));
        FreeAclState(acl, gen_id);
        return true;
    }

    if (dynamic_cast<const NextHop *>(entry)) {
        DBTable *table = agent_->nexthop_table();
        NextHop *nh = static_cast<NextHop *> (table->Find(entry));
        FreeNhState(nh, gen_id);
        return true;
    }

    if (dynamic_cast<const VrfEntry *>(entry)) {
        DBTable *table = agent_->vrf_table();
        VrfEntry *vrf = static_cast<VrfEntry *> (table->Find(entry));
        FreeVrfState(vrf, gen_id);
        return true;
    }

    if (dynamic_cast<const AgentRoute *>(entry)) {
        VrfEntry *vrf = (static_cast<const AgentRoute *>(entry))->vrf();
        AgentRoute *rt = NULL;
        if (dynamic_cast<const InetUnicastRouteEntry *>(entry)) {
            DBTable *table = NULL;
            if ((dynamic_cast<const AgentRoute *>(entry))->GetTableType()
                == Agent::INET4_UNICAST)
                table = vrf->GetInet4UnicastRouteTable();
            else
                table = vrf->GetInet6UnicastRouteTable();
            rt = static_cast<AgentRoute *>(table->Find(entry));
        } else {
            DBTable *table = vrf->GetBridgeRouteTable();
            rt = static_cast<AgentRoute *>(table->Find(entry));
        }
        FreeRouteState(rt, gen_id);
        return true;
    }

    assert(0);
    return true;
}
