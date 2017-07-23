#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/route_leak.h>

void RouteLeakState::AddIndirectRoute(const AgentRoute *route) {
    InetUnicastAgentRouteTable *table = dest_vrf_->GetInet4UnicastRouteTable();
    const InetUnicastRouteEntry *uc_rt = 
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = uc_rt->GetActivePath();
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(active_path->nexthop());
    Ip4Address gw_ip = *(nh->GetDip());

    if (InetUnicastAgentRouteTable::FindResolveRoute(dest_vrf_->GetName(),
                                                     uc_rt->addr().to_v4())) {
        InetUnicastAgentRouteTable::CheckAndAddArpReq(dest_vrf_->GetName(),
                                                      uc_rt->addr().to_v4(),
                                                      agent_->vhost_interface(),
                                                      active_path->dest_vn_list(),
                                                      active_path->sg_list(),
                                                      active_path->tag_list());
        return;
    }

    table->AddGatewayRoute(agent_->local_vm_peer(),
                           dest_vrf_->GetName(),
                           uc_rt->addr().to_v4(),
                           uc_rt->plen(),
                           gw_ip,
                           active_path->dest_vn_list(),
                           MplsTable::kInvalidLabel,
                           active_path->sg_list(),
                           active_path->tag_list(),
                           active_path->communities());
}

void RouteLeakState::AddInterfaceRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt = 
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = uc_rt->GetActivePath();

    InterfaceNH *intf_nh = dynamic_cast<InterfaceNH *>(active_path->nexthop());
    if (intf_nh == NULL) {
        return;
    }

    if (intf_nh->GetInterface()->type() == Interface::PACKET) {
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
                AddReceiveRoute(route);
                return;
            }
        }
    }

    InetUnicastAgentRouteTable::AddLocalVmRoute(agent_->local_vm_peer(),
                                                dest_vrf_->GetName(),
                                                uc_rt->addr(),
                                                uc_rt->plen(),
                                                intf_nh->GetIfUuid(),
                                                active_path->dest_vn_list(),
                                                MplsTable::kInvalidLabel,
                                                active_path->sg_list(),
                                                active_path->tag_list(),
                                                active_path->communities(),
                                                false,
                                                active_path->path_preference(),
                                                Ip4Address(0),
                                                EcmpLoadBalance(), false, false,
                                                intf_nh->GetInterface()->name());
}

void RouteLeakState::AddReceiveRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    const AgentPath *active_path = uc_rt->GetActivePath();

    const ReceiveNH *rch_nh =
        static_cast<const ReceiveNH*>(active_path->nexthop());
    const VmInterface *vm_intf =
        static_cast<const VmInterface *>(rch_nh->GetInterface());

    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(
                dest_vrf_->GetInet4UnicastRouteTable());

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, vm_intf->GetUuid(),
                           vm_intf->name());
    table->AddVHostRecvRoute(agent_->local_vm_peer(),
                             dest_vrf_->GetName(),
                             vmi_key,
                             uc_rt->addr(),
                             uc_rt->plen(),
                             agent_->fabric_vn_name(), false);
}

void RouteLeakState::AddRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt = 
        static_cast<const InetUnicastRouteEntry *>(route);

    if (uc_rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL) {
        AddIndirectRoute(route);
    } else if (uc_rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE) {
        AddInterfaceRoute(route);
    }
}

void RouteLeakState::DeleteRoute(const AgentRoute *route) {
    const InetUnicastRouteEntry *uc_rt =
        static_cast<const InetUnicastRouteEntry *>(route);
    dest_vrf_->GetInet4UnicastRouteTable()->DeleteReq(agent_->local_vm_peer(),
                                                      dest_vrf_->GetName(),
                                                      uc_rt->addr(),
                                                      uc_rt->plen(),
                                                      NULL);
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
}

RouteLeakVrfState::~RouteLeakVrfState() {
    source_vrf_->GetInet4UnicastRouteTable()->Unregister(route_listener_id_);
    walk_ref_.reset();
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

void RouteLeakVrfState::Delete() {
    deleted_ = true;
    source_vrf_->GetInet4UnicastRouteTable()->WalkAgain(walk_ref_);
}

void RouteLeakVrfState::Notify(DBTablePartBase *partition, DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    RouteLeakState *state =
        static_cast<RouteLeakState *>(entry->GetState(partition->parent(),
                                                      route_listener_id_));

    if (route->IsDeleted() || deleted_) {
        if (state) {
            //Delete the route
            entry->ClearState(partition->parent(), route_listener_id_);
            state->DeleteRoute(route);
            delete state;
        }
        return;
    }

    if (state == NULL) {
        state = new RouteLeakState(dest_vrf_->GetInet4UnicastRouteTable()->agent(), 
                                   NULL);
        route->SetState(partition->parent(), route_listener_id_, state);
    }

    if (state->dest_vrf() && state->dest_vrf() != dest_vrf_) {
        state->DeleteRoute(route);
    }

    if (dest_vrf_ && state->dest_vrf() != dest_vrf_) {
        //Add the route in new VRF
        state->set_dest_vrf(dest_vrf_.get());
    }
    state->AddRoute(route);
}

void RouteLeakVrfState::SetDestVrf(VrfEntry *vrf) {
    if (dest_vrf_ != vrf) {
        dest_vrf_ = vrf;
        source_vrf_->GetInet4UnicastRouteTable()->WalkAgain(walk_ref_);
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
