/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cmn/agent_cmn.h>
#include <pkt/pkt_handler.h>
#include <oper/route_common.h>
#include <services/icmpv6_proto.h>

Icmpv6Proto::Icmpv6Proto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::ICMPV6, io) {
    vn_table_listener_id_ = agent->vn_table()->Register(
                             boost::bind(&Icmpv6Proto::VnNotify, this, _2));
    vrf_table_listener_id_ = agent->vrf_table()->Register(
                             boost::bind(&Icmpv6Proto::VrfNotify, this, _1, _2));
    interface_listener_id_ = agent->interface_table()->Register(
                             boost::bind(&Icmpv6Proto::InterfaceNotify,
                                         this, _2));

    boost::shared_ptr<PktInfo> pkt_info(new PktInfo(PktHandler::ICMPV6, NULL));
    icmpv6_handler_.reset(new Icmpv6Handler(agent, pkt_info, io));

    timer_ = TimerManager::CreateTimer(io, "Icmpv6Timer",
             TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
             PktHandler::ICMPV6);
    timer_->Start(kRouterAdvertTimeout,
                  boost::bind(&Icmpv6Handler::RouterAdvertisement,
                              icmpv6_handler_.get(), this));
}

Icmpv6Proto::~Icmpv6Proto() {
}

void Icmpv6Proto::Shutdown() {
    agent_->vn_table()->Unregister(vn_table_listener_id_);
    agent_->vrf_table()->Unregister(vrf_table_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

ProtoHandler *Icmpv6Proto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                             boost::asio::io_service &io) {
    return new Icmpv6Handler(agent(), info, io);
}

Icmpv6VrfState *Icmpv6Proto::CreateAndSetVrfState(VrfEntry *vrf) {
    Icmpv6VrfState *state = new Icmpv6VrfState(agent_, this, vrf,
                                               vrf->GetInet6UnicastRouteTable());
    state->set_route_table_listener_id(vrf->GetInet6UnicastRouteTable()->
        Register(boost::bind(&Icmpv6VrfState::RouteUpdate, state, _1, _2)));
    vrf->SetState(vrf->get_table_partition()->parent(),
                  vrf_table_listener_id_, state);
    return state;
}

void Icmpv6Proto::VnNotify(DBEntryBase *entry) {
    if (entry->IsDeleted()) return;

    VnEntry *vn = static_cast<VnEntry *>(entry);
    VrfEntry *vrf = vn->GetVrf();
    if (!vrf || vrf->IsDeleted()) return;

    if (vrf->GetName() == agent_->fabric_vrf_name())
        return;

    if (vn->layer3_forwarding()) {
        Icmpv6VrfState *state = static_cast<Icmpv6VrfState *>(vrf->GetState(
                             vrf->get_table_partition()->parent(),
                             vrf_table_listener_id_));
        if (state == NULL) {
            state = CreateAndSetVrfState(vrf);
        }
        if (state->default_routes_added()) {
            return;
        }

        boost::system::error_code ec;
        Ip6Address addr = Ip6Address::from_string(IPV6_ALL_ROUTERS_ADDRESS, ec);
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet6UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                                                             addr, 128,
                                                             vn->GetName());
        /* We need route for PKT0_LINKLOCAL_ADDRESS so that vrouter can respond
         * to NDP requests for PKT0_LINKLOCAL_ADDRESS. Even though the nexthop
         * for this route is pkt0, vrouter never sends pkts pointing to this
         * route on pkt0.
         */
        addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet6UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                                                             addr, 128,
                                                             vn->GetName());
        state->set_default_routes_added(true);
    }
}

void Icmpv6Proto::VrfNotify(DBTablePartBase *part, DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (vrf->GetName() == agent_->fabric_vrf_name())
        return;

    Icmpv6VrfState *state = static_cast<Icmpv6VrfState *>(vrf->GetState(
                             vrf->get_table_partition()->parent(),
                             vrf_table_listener_id_));
    if (state && entry->IsDeleted()) {
        boost::system::error_code ec;
        Ip6Address addr = Ip6Address::from_string(IPV6_ALL_ROUTERS_ADDRESS, ec);
        // enqueue delete request on fabric VRF
        agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
                                                        vrf->GetName(),
                                                        addr, 128, NULL);
        addr = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
        agent_->fabric_inet4_unicast_table()->DeleteReq(agent_->local_peer(),
                                                        vrf->GetName(),
                                                        addr, 128, NULL);
        state->set_default_routes_added(false);
        state->Delete();
    }
    if (!state) {
        CreateAndSetVrfState(vrf);
    }
}

void Icmpv6Proto::InterfaceNotify(DBEntryBase *entry) {
    Interface *interface = static_cast<Interface *>(entry);
    if (interface->type() != Interface::VM_INTERFACE)
        return;

    Icmpv6Stats stats;
    VmInterface *vm_interface = static_cast<VmInterface *>(entry);
    VmInterfaceMap::iterator it = vm_interfaces_.find(vm_interface);
    if (interface->IsDeleted()) {
        if (it != vm_interfaces_.end()) {
            vm_interfaces_.erase(it);
        }
    } else {
        if (it == vm_interfaces_.end()) {
            vm_interfaces_.insert(VmInterfacePair(vm_interface, stats));
        }
    }
}

void Icmpv6Proto::ValidateAndClearVrfState(VrfEntry *vrf) {
    if (!vrf->IsDeleted())
        return;

    DBState *state = static_cast<DBState *>
        (vrf->GetState(vrf->get_table_partition()->parent(),
                       vrf_table_listener_id_));
    if (state) {
        vrf->ClearState(vrf->get_table_partition()->parent(),
                        vrf_table_listener_id_);
    }
}

void Icmpv6VrfState::RouteUpdate(DBTablePartBase *part, DBEntryBase *entry) {
    InetUnicastRouteEntry *route = static_cast<InetUnicastRouteEntry *>(entry);

    Icmpv6RouteState *state = static_cast<Icmpv6RouteState *>
        (entry->GetState(part->parent(), route_table_listener_id_));

    if (entry->IsDeleted() || deleted_) {
        if (state) {
            entry->ClearState(part->parent(), route_table_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new Icmpv6RouteState(this, route->vrf_id(), route->addr(),
                                     route->plen());
        entry->SetState(part->parent(), route_table_listener_id_, state);
    }

    //Check if there is a local VM path, if yes send a
    //Neighbor Solicit request, to trigger route preference state machine
    if (state && route->vrf()->GetName() != agent_->fabric_vrf_name()) {
        state->SendNeighborSolicitForAllIntf(route);
    }
}

bool Icmpv6VrfState::DeleteRouteState(DBTablePartBase *part, DBEntryBase *ent) {
    RouteUpdate(part, ent);
    return true;
}

void Icmpv6VrfState::Delete() {
    deleted_ = true;
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(rt_table_, NULL,
            boost::bind(&Icmpv6VrfState::DeleteRouteState, this, _1, _2),
            boost::bind(&Icmpv6VrfState::WalkDone, this, _1, this));
}

void Icmpv6VrfState::WalkDone(DBTableBase *partition, Icmpv6VrfState *state) {
    icmp_proto_->ValidateAndClearVrfState(vrf_);
    rt_table_->Unregister(route_table_listener_id_);
    table_delete_ref_.Reset(NULL);
    delete state;
}

Icmpv6VrfState::Icmpv6VrfState(Agent *agent_ptr, Icmpv6Proto *proto,
                               VrfEntry *vrf_entry, AgentRouteTable *table):
    agent_(agent_ptr), icmp_proto_(proto), vrf_(vrf_entry), rt_table_(table),
    route_table_listener_id_(DBTableBase::kInvalidId),
    table_delete_ref_(this, table->deleter()), deleted_(false),
    default_routes_added_(false) {
}

Icmpv6VrfState::~Icmpv6VrfState() {
}

Icmpv6RouteState::Icmpv6RouteState(Icmpv6VrfState *vrf_state, uint32_t vrf_id,
                                   IpAddress ip, uint8_t plen) :
    vrf_state_(vrf_state), ns_req_timer_(NULL), vrf_id_(vrf_id), vm_ip_(ip),
    plen_(plen) {
}

Icmpv6RouteState::~Icmpv6RouteState() {
    if (ns_req_timer_) {
        ns_req_timer_->Cancel();
        TimerManager::DeleteTimer(ns_req_timer_);
    }
}

bool Icmpv6RouteState::SendNeighborSolicit() {
    if (wait_for_traffic_map_.size() == 0) {
        return false;
    }

    bool ret = false;
    boost::shared_ptr<PktInfo> pkt(new PktInfo(vrf_state_->agent(),
                                               ICMP_PKT_SIZE,
                                               PktHandler::ICMPV6, 0));
    Icmpv6Handler handler(vrf_state_->agent(), pkt,
                         *(vrf_state_->agent()->event_manager()->io_service()));

    WaitForTrafficIntfMap::iterator it = wait_for_traffic_map_.begin();
    for (;it != wait_for_traffic_map_.end(); it++) {
        if (it->second >= kMaxRetry) {
            continue;
        }

        VmInterface *vm_intf = static_cast<VmInterface *>(
             vrf_state_->agent()->interface_table()->FindInterface(it->first));
        if (!vm_intf) {
            continue;
        }
        it->second++;
        handler.SendNeighborSolicit(gw_ip_.to_v6(), vm_ip_.to_v6(), it->first,
                                    vrf_id_);
        vrf_state_->icmp_proto()->IncrementStatsNeighborSolicit(vm_intf);
        ret = true;
    }
    return ret;
}

void Icmpv6RouteState::StartTimer() {
    if (ns_req_timer_ == NULL) {
        ns_req_timer_ = TimerManager::CreateTimer(
                *(vrf_state_->agent()->event_manager()->io_service()),
                "Neighbor Solicit Request timer for VM",
                TaskScheduler::GetInstance()->GetTaskId("Agent::Services"),
                PktHandler::ICMPV6);
    }
    ns_req_timer_->Start(kTimeout,
                         boost::bind(&Icmpv6RouteState::SendNeighborSolicit,
                                     this));
}

//Send Neighbor Solicit request on interface in Active-BackUp mode
//So that preference of route can be incremented if the VM replies with
//Neighbor Advertisement
void Icmpv6RouteState::SendNeighborSolicitForAllIntf
    (const InetUnicastRouteEntry *route) {
    WaitForTrafficIntfMap new_wait_for_traffic_map;
    for (Route::PathList::const_iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() &&
            path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
            if (path->subnet_service_ip().is_unspecified() ||
                !path->subnet_service_ip().is_v6()) {
                continue;
            }
            const NextHop *nh = path->ComputeNextHop(vrf_state_->agent());
            if (nh->GetType() != NextHop::INTERFACE) {
                continue;
            }

            const InterfaceNH *intf_nh =
                static_cast<const  InterfaceNH *>(nh);
            const Interface *intf =
                static_cast<const Interface *>(intf_nh->GetInterface());
            if (intf->type() != Interface::VM_INTERFACE) {
                //Ignore non vm interface nexthop
                continue;
            }
            gw_ip_ = path->subnet_service_ip();
            uint32_t intf_id = intf->id();
            bool wait_for_traffic = path->path_preference().wait_for_traffic();
            //Build new list of interfaces in active state
            if (wait_for_traffic == true) {
                WaitForTrafficIntfMap::const_iterator wait_for_traffic_it =
                    wait_for_traffic_map_.find(intf_id);
                if (wait_for_traffic_it == wait_for_traffic_map_.end()) {
                    new_wait_for_traffic_map.insert(std::make_pair(intf_id, 0));
                } else {
                    new_wait_for_traffic_map.insert(std::make_pair(intf_id,
                        wait_for_traffic_it->second));
                }
            }
        }
    }


    wait_for_traffic_map_ = new_wait_for_traffic_map;
    if (wait_for_traffic_map_.size() > 0) {
        SendNeighborSolicit();
        StartTimer();
    }
}

Icmpv6Proto::Icmpv6Stats *Icmpv6Proto::VmiToIcmpv6Stats(VmInterface *i) {
    VmInterfaceMap::iterator it = vm_interfaces_.find(i);
    if (it == vm_interfaces_.end()) {
        return NULL;
    }
    return &it->second;
}

void Icmpv6Proto::IncrementStatsRouterSolicit(VmInterface *vmi) {
    stats_.icmpv6_router_solicit_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_router_solicit_++;
    }
}

void Icmpv6Proto::IncrementStatsRouterAdvert(VmInterface *vmi) {
    stats_.icmpv6_router_advert_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_router_advert_++;
    }
}

void Icmpv6Proto::IncrementStatsPingRequest(VmInterface *vmi) {
    stats_.icmpv6_ping_request_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_ping_request_++;
    }
}

void Icmpv6Proto::IncrementStatsPingResponse(VmInterface *vmi) {
    stats_.icmpv6_ping_response_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_ping_response_++;
    }
}

void Icmpv6Proto::IncrementStatsNeighborSolicit(VmInterface *vmi) {
    stats_.icmpv6_neighbor_solicit_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_neighbor_solicit_++;
    }
}

void Icmpv6Proto::IncrementStatsNeighborAdvert(VmInterface *vmi) {
    stats_.icmpv6_neighbor_advert_++;
    Icmpv6Stats *stats = VmiToIcmpv6Stats(vmi);
    if (stats) {
        stats->icmpv6_neighbor_advert_++;
    }
}
