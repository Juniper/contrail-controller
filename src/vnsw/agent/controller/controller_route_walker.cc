/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/shared_ptr.hpp>

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "controller/controller_route_walker.h"
#include "controller/controller_init.h"
#include "controller/controller_types.h"
#include "controller/controller_vrf_export.h"
#include "controller/controller_export.h"
#include "oper/route_common.h"
#include "oper/peer.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

ControllerRouteWalker::ControllerRouteWalker(Agent *agent, Peer *peer) : 
    AgentRouteWalker(agent, AgentRouteWalker::ALL), peer_(peer), 
    associate_(false), type_(NOTIFYALL) {
}

// Takes action based on context of walk. These walks are not parallel.
// At a time peer can be only in one state. 
bool ControllerRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                          DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    // Notification from deleted VRF should have taken care of all operations
    // w.r.t. peer, see VrfExport::Notify 
    if (vrf->IsDeleted()) 
        return true;

    switch (type_) {
    case NOTIFYALL:
        return VrfNotifyAll(partition, entry);
    case NOTIFYMULTICAST:
        return VrfNotifyMulticast(partition, entry);
    case DELPEER:
        return VrfDelPeer(partition, entry);
    case STALE:
        return VrfNotifyStale(partition, entry);
    default:
        return false;
    }
    return false;
}

/*
 * Notification for vrf entry - Creates states (VRF and route) and 
 * send subscription to control node
 * This will be called for active bgp peer only.
 */ 
bool ControllerRouteWalker::VrfNotifyAll(DBTablePartBase *partition, 
                                         DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        if (state) {
            /* state for __default__ instance will not be created if the 
             * xmpp channel is up the first time as export code registers to 
             * vrf-table after entry for __default__ instance is created */
            state->force_chg_ = true;
        }

        //Pass this object pointer so that VrfExport::Notify can start the route
        //walk if required on this VRF. Also it adds state if none is found.
        VrfExport::Notify(bgp_peer->GetBgpXmppPeer(), partition, entry);
        CONTROLLER_TRACE(Walker, "Vrf Notify all", vrf->GetName(),
                         bgp_peer->GetName());
        return true;
    }
    return false;
}

/*
 * Delete peer notifications for VRF.
 */
bool ControllerRouteWalker::VrfDelPeer(DBTablePartBase *partition,
                                       DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        // Register Callback for deletion of VRF state on completion of route
        // walks 
        RouteWalkDoneForVrfCallback(boost::bind(
                                    &ControllerRouteWalker::RouteWalkDoneForVrf,
                                    this, _1));
        StartRouteWalk(vrf);
        CONTROLLER_TRACE(Walker, "Vrf DelPeer", vrf->GetName(), 
                         peer_->GetName());
        return true;
    }
    return false;
}

bool ControllerRouteWalker::VrfNotifyMulticast(DBTablePartBase *partition, 
                                               DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    CONTROLLER_TRACE(Walker, "Vrf Multicast", vrf->GetName(), peer_->GetName());
    return VrfNotifyInternal(partition, entry);
}

bool ControllerRouteWalker::VrfNotifyStale(DBTablePartBase *partition, 
                                           DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    CONTROLLER_TRACE(Walker, "Vrf Stale", vrf->GetName(), peer_->GetName());
    return VrfNotifyInternal(partition, entry);
}

//Common routeine if basic vrf and peer check is required for the walk
bool ControllerRouteWalker::VrfNotifyInternal(DBTablePartBase *partition, 
                                              DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    if (peer_->GetType() == Peer::BGP_PEER) {
        BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

        DBTableBase::ListenerId id = bgp_peer->GetVrfExportListenerId();
        VrfExport::State *state = 
            static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)); 
        //TODO check if state is not added for default vrf
        if (state && (vrf->GetName().compare(agent()->fabric_vrf_name()) != 0)) {
            StartRouteWalk(vrf);
        }

        return true;
    }
    return false;
}

// Takes action based on context of walk. These walks are not parallel.
// At a time peer can be only in one state. 
bool ControllerRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                      DBEntryBase *entry) {
    switch (type_) {
    case NOTIFYALL:
        return RouteNotifyAll(partition, entry);
    case NOTIFYMULTICAST:
        return RouteNotifyMulticast(partition, entry);
    case DELPEER:
        return RouteDelPeer(partition, entry);
    case STALE:
        return RouteStaleMarker(partition, entry);
    default:
        return false;
    }
    return false;
}

bool ControllerRouteWalker::RouteNotifyInternal(DBTablePartBase *partition, 
                                                DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);

    if ((type_ == NOTIFYMULTICAST) && !route->is_multicast())
        return true;

    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    Agent::RouteTableType table_type = route->GetTableType();

    //Get the route state
    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (bgp_peer->GetRouteExportState(partition, entry));
    if (route_state) {
        // Forcibly send the notification to peer.
        route_state->force_chg_ = true;
    }

    VrfEntry *vrf = route->vrf();
    DBTablePartBase *vrf_partition = agent()->vrf_table()->
        GetTablePartition(vrf);
    VrfExport::State *vs = static_cast<VrfExport::State *>
        (bgp_peer->GetVrfExportState(vrf_partition, vrf));

    vs->rt_export_[table_type]->
      Notify(bgp_peer->GetBgpXmppPeer(), associate_, table_type, partition, 
             entry);
    return true;
}

bool ControllerRouteWalker::RouteNotifyAll(DBTablePartBase *partition, 
                                           DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    CONTROLLER_TRACE(Walker, "Route NotifyAll", route->ToString(), 
                     peer_->GetName());
    return RouteNotifyInternal(partition, entry);
}

bool ControllerRouteWalker::RouteNotifyMulticast(DBTablePartBase *partition, 
                                                 DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    CONTROLLER_TRACE(Walker, "Route Multicast Notify", route->ToString(), 
                     peer_->GetName());
    return RouteNotifyInternal(partition, entry);
}

// Deletes the peer and corresponding state in route
bool ControllerRouteWalker::RouteDelPeer(DBTablePartBase *partition,
                                         DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);

    if (!route)
        return true;

    CONTROLLER_TRACE(Walker, "Route Delpeer", route->ToString(), 
                     peer_->GetName());

    VrfEntry *vrf = route->vrf();
    DBTablePartBase *vrf_partition = agent()->vrf_table()->
        GetTablePartition(vrf);
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>
        (bgp_peer->GetVrfExportState(vrf_partition, vrf));
    RouteExport::State *state = static_cast<RouteExport::State *>
        (bgp_peer->GetRouteExportState(partition, entry));
    if (vrf_state && state && vrf_state->rt_export_[route->GetTableType()]) {
        route->ClearState(partition->parent(), vrf_state->rt_export_[route->
                          GetTableType()]->GetListenerId());
        delete state;
        CONTROLLER_TRACE(Walker, "DelPeer route walk, delete state", 
                         route->ToString(), peer_->GetName());
    }

    vrf->GetRouteTable(route->GetTableType())->DeletePathFromPeer(partition,
                                                                  route, peer_);
    return true;
}

bool ControllerRouteWalker::RouteStaleMarker(DBTablePartBase *partition, 
                                             DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route) {
        route->vrf()->GetRouteTable(route->GetTableType())->
            StalePathFromPeer(partition, route, peer_);
    }

    CONTROLLER_TRACE(Walker, "Route Stale", route->ToString(), 
                     peer_->GetName());
    return true;
}

// Called when for a VRF all route table walks are complete.
// Deletes the VRF state of that peer.
void ControllerRouteWalker::RouteWalkDoneForVrf(VrfEntry *vrf) {
    // Currently used only for delete peer handling
    // Deletes the state and release the listener id
    if (type_ != DELPEER)
        return;

    CONTROLLER_TRACE(Walker, "Route Walk done", vrf->GetName(), 
                     peer_->GetName());
    BgpPeer *bgp_peer = static_cast<BgpPeer *>(peer_);
    DBEntryBase *entry = static_cast<DBEntryBase *>(vrf);
    DBTablePartBase *partition = agent()->vrf_table()->GetTablePartition(vrf);
    bgp_peer->DeleteVrfState(partition, entry);
}

// walk_done_cb - Called back when all walk i.e. VRF and route are done.
void ControllerRouteWalker::Start(Type type, bool associate, 
                            AgentRouteWalker::WalkDone walk_done_cb) {
    associate_ = associate;
    type_ = type;
    WalkDoneCallback(walk_done_cb);

    StartVrfWalk(); 
}

void ControllerRouteWalker::Cancel() {
    CONTROLLER_TRACE(Walker, "Cancel Vrf Walk", "", peer_->GetName());
    CancelVrfWalk(); 
}
