/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/ecmp_load_balance.h>
#include <oper/route_common.h>
#include <oper/peer.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/mirror_table.h>
#include "oper/tunnel_nh.h"

#include <controller/controller_vrf_export.h>
#include <controller/controller_init.h>
#include <controller/controller_export.h> 
#include <controller/controller_peer.h>
#include <controller/controller_types.h>

RouteExport::State::State() : 
    DBState(), exported_(false), fabric_multicast_exported_(false),
    force_chg_(false), label_(MplsTable::kInvalidLabel), vn_(), sg_list_(),
    tunnel_type_(TunnelType::INVALID), path_preference_(),
    destination_(), source_(), ecmp_load_balance_(), isid_(0) {
}

bool RouteExport::State::Changed(const AgentRoute *route, const AgentPath *path) const {
    if (exported_ == false)
        return true;

    if (force_chg_ == true)
        return true;

    if (label_ != path->GetActiveLabel())
        return true;

    if (tunnel_type_ != path->tunnel_type()) {
        return true;
    };

    if (vn_ != path->dest_vn_name())
        return true;

    if (sg_list_ != path->sg_list())
        return true;

    if (communities_ != path->communities())
        return true;

    if (path_preference_ != path->path_preference())
        return true;

    if(ecmp_load_balance_ != path->ecmp_load_balance())
        return true;

    if (etree_leaf_ != path->etree_leaf())
        return true;

    return false;
}

void RouteExport::State::Update(const AgentRoute *route, const AgentPath *path) {
    force_chg_ = false;
    label_ = path->GetActiveLabel();
    vn_ = path->dest_vn_name();
    sg_list_ = path->sg_list();
    communities_ = path->communities();
    tunnel_type_ = path->tunnel_type();
    path_preference_ = path->path_preference();
    ecmp_load_balance_ = path->ecmp_load_balance();
    etree_leaf_ = path->etree_leaf();
}

RouteExport::RouteExport(AgentRouteTable *rt_table):
    rt_table_(rt_table), marked_delete_(false), 
    table_delete_ref_(this, rt_table->deleter()) {
}

RouteExport::~RouteExport() {
    if (rt_table_) {
        rt_table_->Unregister(id_);
    }
    table_delete_ref_.Reset(NULL);
}

void RouteExport::ManagedDelete() {
    marked_delete_ = true;
}

// Route entry add/change/del notification handler
void RouteExport::Notify(const Agent *agent,
                         AgentXmppChannel *bgp_xmpp_peer,
                         bool associate,
                         Agent::RouteTableType type, 
                         DBTablePartBase *partition,
                         DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);

    // Primitive checks for non-delete notification
    if (!route->IsDeleted()) {
        // If there is no active BGP peer attached to channel, ignore 
        // non-delete notification for this channel
        if (!AgentXmppChannel::IsBgpPeerActive(agent, bgp_xmpp_peer))
            return;

        // Extract the listener ID of active BGP peer for route table to which
        // this route entry belongs to. Listeners of route table can be active
        // bgp peers as well as decommisioned BGP peers(if they exist). Active
        // and decommisoned BGP peer can co-exist till cleanup timer is fired.
        // During this interval ignore notification for decommisioned bgp peer
        // listener id  
        VrfEntry *vrf = route->vrf();
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(bgp_xmpp_peer->
                                                   bgp_peer_id());
        DBTableBase::ListenerId vrf_id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *vs = 
            static_cast<VrfExport::State *>(vrf->GetState(vrf->get_table(),
                                                          vrf_id));
        // If VRF state is not present then listener has not been added.
        // Addition of listener later will result in walk to notify all routes.
        // That in turn will add state as well by calling current routine.
        // Therefore return when empty VRF state is found.
        if (!vs)
            return;

        //Make sure that vrf has been subscribed before route is published.
        if (vs->IsExportable(bgp_xmpp_peer->sequence_number()))
            return;

        // There may be instances when decommisioned peer is not yet
        // unregistered while a new peer is already present. So there will be
        // two notifications. If its for decommisioned peer then ignore the same
        // by checking the listener id with active bgp peer listener id.
        DBTableBase::ListenerId id = vs->rt_export_[route->GetTableType()]->
            GetListenerId();
        if (id != id_)
            return;
    }

    //If channel is no more active, ignore any updates.
    //It may happen that notify is enqueued before channel is removed.
    if (!AgentXmppChannel::IsBgpPeerActive(agent, bgp_xmpp_peer))
        return;

    if (route->is_multicast()) {
        MulticastNotify(bgp_xmpp_peer, associate, partition, e);
    } else {
        UnicastNotify(bgp_xmpp_peer, partition, e, type);
    }
}

void RouteExport::UnicastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                                DBTablePartBase *partition, DBEntryBase *e,
                                Agent::RouteTableType type) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    //TODO Currently BRIDGE notifications are coming because multicast route
    //are installed in same. Once multicast route is shifted to EVPN table
    //then there will be no export from Bridge and check below can be removed.
    if (route->GetTableType() == Agent::BRIDGE)
        return;

    if (route->vrf()->ShouldExportRoute() == false) {
        return;
    }

    AgentRouteTable *table = static_cast<AgentRouteTable *>
        (partition->parent());
    State *state = static_cast<State *>(route->GetState(partition->parent(),
                                                        id_));
    AgentPath *path = route->FindLocalVmPortPath();

    std::stringstream path_str;
    if (path && path->peer())
        path_str << path->peer()->GetName();
    else
        path_str << "None";

    if (marked_delete_) {
        //Ignore route updates on delete marked vrf
        goto done;
    }

    if (!state && route->IsDeleted()) {
        goto done;
    }

    if (state == NULL) {
        state = new State();
        route->SetState(partition->parent(), id_, state);
    }

    if (path && !path->is_local()) {
        if (state->Changed(route, path)) {
            VnListType vn_list;
            vn_list.insert(state->vn_);
            state->Update(route, path);
            state->exported_ = 
                AgentXmppChannel::ControllerSendRouteAdd(bgp_xmpp_peer, 
                        static_cast<AgentRoute * >(route),
                        path->NexthopIp(table->agent()), vn_list,
                        state->label_, path->GetTunnelBmap(),
                        &path->sg_list(), &path->communities(),
                        type, state->path_preference_,
                        state->ecmp_load_balance_);
        }
    } else {
        if (state->exported_ == true) {
            VnListType vn_list;
            vn_list.insert(state->vn_);
            AgentXmppChannel::ControllerSendRouteDelete(bgp_xmpp_peer, 
                    static_cast<AgentRoute *>(route), vn_list,
                    (state->tunnel_type_ == TunnelType::VXLAN ?
                     state->label_ : 0),
                    TunnelType::AllType(), NULL, NULL,
                    type, state->path_preference_);
            state->exported_ = false;
        }
    }
done:
    if (route->IsDeleted()) {
        if (state) {
            route->ClearState(partition->parent(), id_);
            delete state;
        }
    }
}

static const AgentPath *GetMulticastExportablePath(const Agent *agent,
                                                   const AgentRoute *route) {
    const AgentPath *active_path = route->FindPath(agent->local_vm_peer());
    //OVS peer path
    if (active_path == NULL) {
        const EvpnRouteEntry *evpn_route =
            dynamic_cast<const EvpnRouteEntry *>(route);
        active_path = evpn_route ? evpn_route->FindOvsPath() : NULL;
    }
    //If no loca peer, then look for tor peer as that should also result
    //in export of route.
    if (active_path == NULL)
        active_path = route->FindPath(agent->multicast_tor_peer());
    //Subnet discard
    if (active_path == NULL) {
        const AgentPath *local_path = route->FindPath(agent->local_peer());
        if (local_path && !agent->tor_agent_enabled()) {
            return local_path;
        }
    }

    return active_path;
}

bool RouteExport::MulticastRouteCanDissociate(const AgentRoute *route) {
    bool can_dissociate = route->IsDeleted();
    Agent *agent = static_cast<AgentRouteTable*>(route->get_table())->
        agent();
    const AgentPath *local_path = route->FindPath(agent->local_peer());
    if (local_path && !agent->tor_agent_enabled()) {
        return can_dissociate;
    }
    if (route->is_multicast()) {
        const AgentPath *active_path = GetMulticastExportablePath(agent, route);
        if (active_path == NULL)
            return true;
        const NextHop *nh = active_path ? active_path->ComputeNextHop(agent) : NULL;
        const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
        if (cnh && cnh->ComponentNHCount() == 0)
            return true;
    }
    return can_dissociate;
}

void RouteExport::SubscribeFabricMulticast(const Agent *agent,
                                           AgentXmppChannel *bgp_xmpp_peer,
                                           AgentRoute *route,
                                           RouteExport::State *state) {
    const AgentPath *active_path = GetMulticastExportablePath(agent, route);
    //Agent running as tor(simulate_evpn_tor) - dont subscribe
    //Route has path with peer OVS_PEER i.e. TOR agent mode - dont subscribe
    //Subscribe condition:
    //first time subscription or force change 
    if (!(agent->simulate_evpn_tor()) &&
        (active_path->peer()->GetType() != Peer::OVS_PEER) &&
        ((state->fabric_multicast_exported_ == false) ||
         (state->force_chg_ == true))) {
        //TODO optimize by checking for force_chg? In other cases duplicate
        //request can be filtered.
        if (state->fabric_multicast_exported_ == true) {
            //Unsubscribe before re-sending subscription, this makes sure in any
            //corner case control-node does not see this as a duplicate request.
            AgentXmppChannel::ControllerSendMcastRouteDelete(bgp_xmpp_peer,
                                                             route);
            state->fabric_multicast_exported_ = false;
        }
        //Sending 255.255.255.255 for fabric tree
        state->fabric_multicast_exported_ =
            AgentXmppChannel::ControllerSendMcastRouteAdd(bgp_xmpp_peer,
                                                          route);
    }
}

// Handles subscription of multicast routes.
// Following are the kind of subscription:
// Fabric - For fabric replication tree
// EVPN Ingress replication - For adding compute node in EVPN replication list.
// TOR Ingress replication - For adding in TOR replication list (relevant for
// TSN).
//
// For Tor-agent its a route with tunnel NH and there is no subscription.
void RouteExport::MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                                  bool associate,
                                  DBTablePartBase *partition, 
                                  DBEntryBase *e) {
    Agent *agent = bgp_xmpp_peer->agent();
    AgentRoute *route = static_cast<AgentRoute *>(e);
    State *state = static_cast<State *>(route->GetState(partition->parent(), id_));
    bool route_can_be_dissociated = MulticastRouteCanDissociate(route);

    //Handle withdraw for following cases:
    //- Route is not having any active multicast exportable path or is deleted.
    //- associate(false): Bgp Peer has gone down and state needs to be removed. 
    if (route_can_be_dissociated || !associate) {
        if (state == NULL) {
            return;
        }

        if (state->fabric_multicast_exported_ == true) {
            AgentXmppChannel::ControllerSendMcastRouteDelete(bgp_xmpp_peer,
                                                             route);
            state->fabric_multicast_exported_ = false;
        }

        if ((state->ingress_replication_exported_ == true)) {
            uint32_t label = state->label_;
            if (route->vrf()->IsPbbVrf()) {
                label = state->isid_;
            }
            state->tunnel_type_ = TunnelType::INVALID;
            AgentXmppChannel::ControllerSendEvpnRouteDelete(bgp_xmpp_peer,
                                                            route,
                                                            state->vn_,
                                                            label,
                                                            state->destination_,
                                                            state->source_,
                                                            TunnelType::AllType());
            state->ingress_replication_exported_ = false;
        }

        route->ClearState(partition->parent(), id_);
        delete state;
        state = NULL;
        return;
    }

    if (marked_delete_) {
        //Ignore route updates on delete marked vrf
        return;
    }

    if (state == NULL) {
        state = new State();
        route->SetState(partition->parent(), id_, state);
    }

    if (route->vrf()->ShouldExportRoute()) {
        SubscribeFabricMulticast(agent, bgp_xmpp_peer, route, state);
    }
    SubscribeIngressReplication(agent, bgp_xmpp_peer, route, state);

    state->force_chg_ = false;
    return;
}

void RouteExport::SubscribeIngressReplication(Agent *agent,
                                              AgentXmppChannel *bgp_xmpp_peer,
                                              AgentRoute *route,
                                              RouteExport::State *state) {
    //Check if bridging mode is enabled for this VN by verifying bridge flag
    //in local peer path.
    bool bridging = false;
    if (route->vrf() && route->vrf()->vn() && route->vrf()->vn()->bridging())
        bridging = true;
 
    const AgentPath *active_path = GetMulticastExportablePath(agent, route);
    //Sending ff:ff:ff:ff:ff:ff for evpn replication
    TunnelType::Type old_tunnel_type = state->tunnel_type_;
    uint32_t old_label = state->label_;
    TunnelType::Type new_tunnel_type = active_path->tunnel_type();
    uint32_t new_label = active_path->GetActiveLabel();

    //Evaluate if ingress replication subscription needs to be withdrawn.
    //Conditions: Tunnel type changed (VXLAN to MPLS) in turn label change,
    //bridging is disabled/enabled on VN
    if ((state->ingress_replication_exported_ == true) ||
        (state->force_chg_ == true)) {
        bool withdraw = false;
        uint32_t withdraw_label = 0;

        if (old_tunnel_type == TunnelType::VXLAN) {
            if ((new_tunnel_type != TunnelType::VXLAN) ||
                (old_label != new_label)) {
                withdraw_label = old_label; 
                withdraw = true;
            }
        } else if (new_tunnel_type == TunnelType::VXLAN) {
            withdraw = true;
        }

        if (route->vrf()->IsPbbVrf()) {
            if (state->isid_ != active_path->vxlan_id()) {
                uint32_t old_isid = state->isid_;
                state->isid_ = active_path->vxlan_id();
                withdraw_label = old_isid;
                withdraw = true;
            } else {
                state->isid_ = active_path->vxlan_id();
            }
        }

        if (bridging == false)
            withdraw = true;

        if (withdraw) {
            AgentXmppChannel::ControllerSendEvpnRouteDelete
                (bgp_xmpp_peer, route, state->vn_, withdraw_label,
                 state->destination_, state->source_,
                 state->tunnel_type_);
            state->ingress_replication_exported_ = false;
        }
    }

    state->isid_ = active_path->vxlan_id();
    //Update state values with new values if there is any change.
    //Also force change same i.e. update.
    if (active_path->tunnel_type() != state->tunnel_type_) {
        state->force_chg_ = true;
        state->tunnel_type_ = active_path->tunnel_type();
    }

    if (active_path->GetActiveLabel() != state->label_) {
        state->force_chg_ = true;
        state->label_ = active_path->GetActiveLabel();
    }

    if (route->vrf()->IsPbbVrf()) {
        if (active_path->vxlan_id() == 0) {
            return;
        }
    }

    //Subcribe if:
    //- Bridging is enabled
    //- First time (ingress_replication_exported is false)
    //- Forced Change
    if (bridging &&
        ((state->ingress_replication_exported_ == false) ||
        (state->force_chg_ == true))) {
        state->label_ = active_path->GetActiveLabel();
        state->vn_ = route->dest_vn_name();
        const TunnelNH *tnh =
            dynamic_cast<const TunnelNH *>(active_path->nexthop());
        if (tnh) {
            state->destination_ = tnh->GetDip()->to_string();
            state->source_ = tnh->GetSip()->to_string();
        }
        SecurityGroupList sg;
        state->ingress_replication_exported_ =
            AgentXmppChannel::ControllerSendEvpnRouteAdd
            (bgp_xmpp_peer, route,
             active_path->NexthopIp(agent),
             route->dest_vn_name(), state->label_,
             TunnelType::GetTunnelBmap(state->tunnel_type_),
             &sg, NULL, state->destination_,
             state->source_, PathPreference());
    }
}

bool RouteExport::DeleteState(DBTablePartBase *partition,
                              DBEntryBase *entry) {
    State *state = static_cast<State *>
                       (entry->GetState(partition->parent(), id_));
    if (state) {
        entry->ClearState(partition->parent(), id_);
        delete state;
    }
    return true;
}

void RouteExport::Walkdone(DBTableBase *partition,
                           RouteExport *rt_export) {
    delete rt_export;
}

void RouteExport::Unregister() {
    //Start unregister process
    DBTableWalker *walker = Agent::GetInstance()->db()->GetWalker();
    walker->WalkTable(rt_table_, NULL, 
            boost::bind(&RouteExport::DeleteState, this, _1, _2),
            boost::bind(&RouteExport::Walkdone, _1, this));
}

RouteExport* RouteExport::Init(AgentRouteTable *table, 
                               AgentXmppChannel *bgp_xmpp_peer) {
    RouteExport *rt_export = new RouteExport(table);
    bool associate = true;
    rt_export->id_ = table->Register(boost::bind(&RouteExport::Notify,
                                     rt_export, table->agent(), bgp_xmpp_peer,
                                     associate, table->GetTableType(), _1, _2));
    return rt_export;
}

