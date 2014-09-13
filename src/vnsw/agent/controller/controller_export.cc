/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>
#include <oper/peer.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/mirror_table.h>

#include <controller/controller_vrf_export.h>
#include <controller/controller_init.h>
#include <controller/controller_export.h> 
#include <controller/controller_peer.h>
#include <controller/controller_types.h>

RouteExport::State::State() : 
    DBState(), exported_(false), evpn_exported_(false), force_chg_(false), 
    server_(0), label_(MplsTable::kInvalidLabel), vn_(""), sg_list_() {
}

bool RouteExport::State::Changed(const AgentPath *path) const {
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

    if (path_preference_ != path->path_preference())
        return true;

    return false;
}

void RouteExport::State::Update(const AgentPath *path) {
    force_chg_ = false;
    label_ = path->GetActiveLabel();
    vn_ = path->dest_vn_name();
    sg_list_ = path->sg_list();
    tunnel_type_ = path->tunnel_type();
    path_preference_ = path->path_preference();
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
void RouteExport::Notify(AgentXmppChannel *bgp_xmpp_peer, 
                         bool associate, Agent::RouteTableType type, 
                         DBTablePartBase *partition, DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);

    // Primitive checks for non-delete notification
    if (!route->IsDeleted()) {
        // If there is no active BGP peer attached to channel, ignore 
        // non-delete notification for this channel
        if (!AgentXmppChannel::IsBgpPeerActive(bgp_xmpp_peer))
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
        if (vs) {
            DBTableBase::ListenerId id = vs->rt_export_[route->GetTableType()]->
                GetListenerId();
            if (id != id_)
                return;
        }
    }

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
    State *state = static_cast<State *>(route->GetState(partition->parent(),
                                                        id_));
    AgentPath *path = route->FindLocalVmPortPath();

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

    if (path) {
        if (state->Changed(path)) {
            if (type == Agent::LAYER2) {
                //In case of layer2 routes any change in vxlan id or
                //movement of tunnelt type from vxlan to mpls should result in
                //withdraw and re add of route.
                bool withdraw = false;
                uint32_t withdraw_label = 0;
                TunnelType::TypeBmap tunnel_type_to_be_withdrawn =
                    TunnelType::AllType();
                if (state->tunnel_type_ == TunnelType::VXLAN) {
                    //Vxlan ID changed or tunnel type is no more VXLAN
                    if ((path->GetActiveLabel() != state->label_) ||
                        (TunnelType::ComputeType(path->GetTunnelBmap()) !=
                         TunnelType::VXLAN)) {
                        withdraw = true;
                        withdraw_label = state->label_;
                        tunnel_type_to_be_withdrawn = TunnelType::VxlanType();
                    }
                } else {
                    if (TunnelType::ComputeType(path->GetTunnelBmap()) ==
                        TunnelType::VXLAN) {
                        withdraw = true;
                        withdraw_label = 0;
                        tunnel_type_to_be_withdrawn = TunnelType::MplsType();
                    }
                }

                if (withdraw) {
                    state->exported_ =
                        AgentXmppChannel::ControllerSendRouteDelete(bgp_xmpp_peer,
                             static_cast<AgentRoute * >(route), state->vn_,
                             withdraw_label, tunnel_type_to_be_withdrawn,
                             &path->sg_list(), type,
                             state->path_preference_);
                }
            }
            state->Update(path);
            state->exported_ = 
                AgentXmppChannel::ControllerSendRouteAdd(bgp_xmpp_peer, 
                        static_cast<AgentRoute * >(route), state->vn_, 
                        state->label_, path->GetTunnelBmap(),
                        &path->sg_list(), type, state->path_preference_);
        }
    } else {
        if (state->exported_ == true) {
            AgentXmppChannel::ControllerSendRouteDelete(bgp_xmpp_peer, 
                    static_cast<AgentRoute *>(route), state->vn_, 
                    (state->tunnel_type_ == TunnelType::VXLAN ?
                     state->label_ : 0),
                    TunnelType::AllType(), NULL,
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

static bool RouteCanDissociate(const AgentRoute *route) {
    bool can_dissociate = route->IsDeleted();
    if (route->is_multicast()) {
        Agent *agent = static_cast<AgentRouteTable*>(route->get_table())->
            agent();
        const AgentPath *active_path = route->FindPath(agent->local_vm_peer());
        if (active_path == NULL)
            return true;
        const NextHop *nh = active_path ? active_path->nexthop(agent) : NULL;
        const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
        if (cnh && cnh->ComponentNHCount() == 0)
            return true;
    }
    return can_dissociate;
}

void RouteExport::MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                                  bool associate, 
                                  DBTablePartBase *partition, 
                                  DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    State *state = static_cast<State *>(route->GetState(partition->parent(), id_));
    bool route_can_be_dissociated = RouteCanDissociate(route);
    const Agent *agent = bgp_xmpp_peer->agent();

    if (route_can_be_dissociated && (state != NULL)) {
        if ((state->exported_ == true) && !(agent->simulate_evpn_tor())) {
            AgentXmppChannel::ControllerSendMcastRouteDelete(bgp_xmpp_peer,
                                                             route);
            state->exported_ = false;
        }

        if ((route->GetTableType() == Agent::LAYER2) &&
            (state->evpn_exported_ == true)) {
            state->tunnel_type_ = TunnelType::INVALID;
            AgentXmppChannel::ControllerSendEvpnRouteDelete(bgp_xmpp_peer,
                                                      route,
                                                      state->vn_,
                                                      state->label_,
                                                      TunnelType::AllType());
            state->evpn_exported_ = false;
        }

        route->ClearState(partition->parent(), id_);
        delete state;
        state = NULL;
        return;
    }

    if (route->IsDeleted()) {
        if (state) {
            route->ClearState(partition->parent(), id_);
            delete state;
        }
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

    if (!route_can_be_dissociated) {

        if (!(agent->simulate_evpn_tor()) && ((state->exported_ == false) ||
                                              (state->force_chg_ == true))) {
            //Sending 255.255.255.255 for fabric tree
            if (associate) {
            state->exported_ =
                AgentXmppChannel::ControllerSendMcastRouteAdd(bgp_xmpp_peer,
                                                              route);
            } else {
                AgentXmppChannel::ControllerSendMcastRouteDelete(bgp_xmpp_peer,
                                                                 route);
            }
        }

        //Sending ff:ff:ff:ff:ff:ff for evpn replication
        const AgentPath *active_path = route->FindPath(agent->local_vm_peer());
        //const AgentPath *active_path = route->GetActivePath();
        TunnelType::Type old_tunnel_type = state->tunnel_type_;
        uint32_t old_label = state->label_;
        TunnelType::Type new_tunnel_type = active_path->tunnel_type();
        uint32_t new_label = active_path->GetActiveLabel();

        if (route->GetTableType() == Agent::LAYER2) {
            if ((state->evpn_exported_ == true) || (state->force_chg_ ==
                                                    true)) {
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
                if (withdraw) {
                    AgentXmppChannel::ControllerSendEvpnRouteDelete(bgp_xmpp_peer,
                                                           route,
                                                           state->vn_,
                                                           withdraw_label,
                                                           state->tunnel_type_);
                    state->evpn_exported_ = false;
                }
            }

            if (active_path) {
                if (active_path->tunnel_type() != state->tunnel_type_) {
                    state->force_chg_ = true;
                    state->tunnel_type_ = active_path->tunnel_type();
                }

                if (active_path->GetActiveLabel() != state->label_) {
                    state->force_chg_ = true;
                    state->label_ = active_path->GetActiveLabel();
                }
            }

            if ((state->evpn_exported_ == false) || (state->force_chg_
                                                     == true)) {
                state->label_ = active_path->GetActiveLabel();
                state->vn_ = route->dest_vn_name();
                if (associate) {
                    state->evpn_exported_ =
                        AgentXmppChannel::ControllerSendEvpnRouteAdd(bgp_xmpp_peer,
                                                        route,
                                                        route->dest_vn_name(),
                                                        state->label_,
                                                        TunnelType::AllType());
                } else {
                    state->evpn_exported_ =
                        AgentXmppChannel::ControllerSendEvpnRouteDelete(bgp_xmpp_peer,
                                                        route,
                                                        route->dest_vn_name(),
                                                        state->label_,
                                                        TunnelType::AllType());
                }
            }
        }

        state->force_chg_ = false;
        return;
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
                                     rt_export, bgp_xmpp_peer, associate, 
                                     table->GetTableType(), _1, _2));
    return rt_export;
}

