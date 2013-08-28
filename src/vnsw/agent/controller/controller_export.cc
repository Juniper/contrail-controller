/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <oper/peer.h>
#include <oper/vm_path.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/vm_path.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
#include <controller/controller_export.h> 
#include <controller/controller_peer.h>
#include <controller/controller_types.h>

Inet4RouteExport::State::State() : 
    DBState(), exported_(false), force_chg_(false), server_(0),
    label_(MplsTable::kInvalidLabel), vn_(""), sg_list_() {
}

bool Inet4RouteExport::State::Changed(const AgentPath *path) const {
    if (exported_ == false)
        return true;

    if (force_chg_ == true)
        return true;

    if (label_ != path->GetLabel())
        return true;

    if (vn_ != path->GetDestVnName())
        return true;

    if (sg_list_ != path->GetSecurityGroupList())
        return true;

    return false;
}

void Inet4RouteExport::State::Update(const AgentPath *path) {
    force_chg_ = false;
    label_ = path->GetLabel();
    vn_ = path->GetDestVnName();
    sg_list_ = path->GetSecurityGroupList();
}

Inet4RouteExport::Inet4RouteExport(Inet4RouteTable *rt_table):
    rt_table_(rt_table), marked_delete_(false), 
    table_delete_ref_(this, rt_table->deleter()) {
}

Inet4RouteExport::Inet4RouteExport(Inet4McRouteTable *rt_table):
    rt_table_(static_cast<Inet4RouteTable *>(rt_table)), marked_delete_(false), 
    table_delete_ref_(this, rt_table->deleter()) {
}

Inet4RouteExport::~Inet4RouteExport() {
    if (rt_table_) {
        rt_table_->Unregister(id_);
    }
    table_delete_ref_.Reset(NULL);
}

void Inet4RouteExport::ManagedDelete() {
    marked_delete_ = true;
}

void Inet4RouteExport::Notify(AgentXmppChannel *bgp_xmpp_peer, 
                              bool associate, 
                              DBTablePartBase *partition, DBEntryBase *e) {
    Inet4Route *route = static_cast<Inet4Route *>(e);

    //If multicast or subnetbroadcast digress to multicast
    if (route->IsMcast() || route->IsSbcast()) {
    	return MulticastNotify(bgp_xmpp_peer, associate, partition, e);
    }
    return UnicastNotify(bgp_xmpp_peer, partition, e);

}

void Inet4RouteExport::UnicastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                              DBTablePartBase *partition, DBEntryBase *e) {
    Inet4UcRoute *route = static_cast<Inet4UcRoute *>(e);
    State *state = static_cast<State *>(route->GetState(partition->parent(),
                                                        id_));
    AgentPath *path = route->FindPath(Agent::GetLocalVmPeer());

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
                             bgp_xmpp_peer->GetBgpPeer()->GetName(),
                             route->GetVrfEntry()->GetName(),
                             route->GetIpAddress().to_string(),
                             route->GetPlen(), false, path->GetLabel());
            state->exported_ = 
                AgentXmppChannel::ControllerSendRoute(bgp_xmpp_peer, 
                        static_cast<Inet4UcRoute* >(route), state->vn_, 
                        state->label_, &path->GetSecurityGroupList(), true);
        }
    } else {
        if (state->exported_ == true) {
            CONTROLLER_TRACE(RouteExport, 
                    bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                    route->GetVrfEntry()->GetName(), 
                    route->GetIpAddress().to_string(), 
                    route->GetPlen(), true, 0);

            AgentXmppChannel::ControllerSendRoute(bgp_xmpp_peer, 
                    static_cast<Inet4UcRoute* >(route), state->vn_, 
                    state->label_, NULL, false);
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

void Inet4RouteExport::MulticastNotify(AgentXmppChannel *bgp_xmpp_peer, 
                                       bool associate,
                                       DBTablePartBase *partition, 
                                       DBEntryBase *e) {

    Inet4McRoute *route = static_cast<Inet4McRoute *>(e);
    State *state = static_cast<State *>(route->GetState(partition->parent(), id_));
    const NextHop *nh;
    const CompositeNH *cnh;

    nh = route->GetActiveNextHop();
    cnh = static_cast<const CompositeNH *>(nh);
    if ((route->IsDeleted() || cnh->ComponentNHCount() == 0) && 
        (state != NULL) && (state->exported_ == true)) {
        CONTROLLER_TRACE(RouteExport, bgp_xmpp_peer->GetBgpPeer()->GetName(),
                route->GetVrfEntry()->GetName(), 
                route->GetIpAddress().to_string(), 32, true, 0);

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

    if (state == NULL) {
        state = new State();
        route->SetState(partition->parent(), id_, state);
        //We should never add a state on a VRF marked 
        //for delete
        assert(marked_delete_ == false);
    }

    if ((cnh->ComponentNHCount() != 0) &&
        ((state->exported_ == false) || (state->force_chg_ == true))) {

        CONTROLLER_TRACE(RouteExport, bgp_xmpp_peer->GetBgpPeer()->GetName(),
                route->GetVrfEntry()->GetName(), 
                route->GetIpAddress().to_string(), 32, associate, 0);

        state->exported_ = 
            AgentXmppChannel::ControllerSendMcastRoute(bgp_xmpp_peer, 
                                                       route, associate);
        state->force_chg_ = false;

        return;
    }
}

bool Inet4RouteExport::DeleteState(DBTablePartBase *partition,
                                   DBEntryBase *entry) {
    State *state = static_cast<State *>
                       (entry->GetState(partition->parent(), id_));
    if (state) {
        entry->ClearState(partition->parent(), id_);
        delete state;
    }
    return true;
}

void Inet4RouteExport::Walkdone(DBTableBase *partition,
                                Inet4RouteExport *rt_export) {
    delete rt_export;
}

void Inet4RouteExport::Unregister() {
    //Start unregister process
    DBTableWalker *walker = Agent::GetDB()->GetWalker();
    walker->WalkTable(rt_table_, NULL, 
            boost::bind(&Inet4RouteExport::DeleteState, this, _1, _2),
            boost::bind(&Inet4RouteExport::Walkdone, _1, this));
}

Inet4RouteExport* Inet4RouteExport::UnicastInit(Inet4UcRouteTable *table, 
                                         AgentXmppChannel *bgp_xmpp_peer) {
    Inet4RouteExport *rt_export = new Inet4RouteExport(table);
    bool associate = true;
    rt_export->id_ = table->Register(boost::bind(&Inet4RouteExport::Notify,
                                     rt_export, bgp_xmpp_peer, associate, _1, _2));
    return rt_export;
}

Inet4RouteExport* Inet4RouteExport::MulticastInit(Inet4McRouteTable *table, 
                                         AgentXmppChannel *bgp_xmpp_peer) {
    Inet4RouteExport *rt_export = new Inet4RouteExport(table);
    bool associate = true;
    rt_export->id_ = table->Register(boost::bind(&Inet4RouteExport::Notify,
                                     rt_export, bgp_xmpp_peer, associate, _1, _2));
    return rt_export;
}

