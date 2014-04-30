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
#include <controller/controller_init.h>
#include <controller/controller_export.h> 
#include <controller/controller_peer.h>
#include <controller/controller_types.h>

RouteExport::State::State() : 
    DBState(), exported_(false), force_chg_(false), server_(0),
    label_(MplsTable::kInvalidLabel), vn_(""), sg_list_() {
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

    return false;
}

void RouteExport::State::Update(const AgentPath *path) {
    force_chg_ = false;
    label_ = path->GetActiveLabel();
    vn_ = path->dest_vn_name();
    sg_list_ = path->sg_list();
    tunnel_type_ = path->tunnel_type();
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
            state->Update(path);
            CONTROLLER_TRACE(RouteExport,
                             bgp_xmpp_peer->GetBgpPeerName(),
                             route->vrf()->GetName(),
                             route->ToString(),
                             false, path->GetActiveLabel());
            state->exported_ = 
                AgentXmppChannel::ControllerSendRoute(bgp_xmpp_peer, 
                        static_cast<AgentRoute * >(route), state->vn_, 
                        state->label_, path->GetTunnelBmap(),
                        &path->sg_list(), true, type);
        }
    } else {
        if (state->exported_ == true) {
            CONTROLLER_TRACE(RouteExport, 
                    bgp_xmpp_peer->GetBgpPeerName(), 
                    route->vrf()->GetName(), 
                    route->ToString(), 
                    true, 0);

            AgentXmppChannel::ControllerSendRoute(bgp_xmpp_peer, 
                    static_cast<AgentRoute *>(route), state->vn_, 
                    state->label_, TunnelType::AllType(), NULL, false, type);
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

void RouteExport::MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                                  bool associate, 
                                  DBTablePartBase *partition, 
                                  DBEntryBase *e) {
    AgentRoute *route = static_cast<AgentRoute *>(e);
    State *state = static_cast<State *>(route->GetState(partition->parent(), id_));
    bool route_can_be_dissociated = route->CanDissociate();

    if (route_can_be_dissociated && (state != NULL) && (state->exported_ == true)) {
        CONTROLLER_TRACE(RouteExport, bgp_xmpp_peer->GetBgpPeerName(),
                route->vrf()->GetName(), 
                route->ToString(), true, 0);

        AgentXmppChannel::ControllerSendMcastRoute(bgp_xmpp_peer, 
                                                   route, false);
        state->exported_ = false;
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

    if (!route_can_be_dissociated && ((state->exported_ == false) || 
                                  (state->force_chg_ == true))) {

        CONTROLLER_TRACE(RouteExport, bgp_xmpp_peer->GetBgpPeerName(),
                route->vrf()->GetName(), 
                route->ToString(), associate, 0);

        state->exported_ = 
            AgentXmppChannel::ControllerSendMcastRoute(bgp_xmpp_peer, 
                                                           route, associate);
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
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
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

