/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/route_common.h>
#include <oper/peer.h>
#include <oper/agent_route_walker.h>
#include <oper/mirror_table.h>

#include <controller/controller_route_walker.h>
#include <controller/controller_peer.h>
#include <controller/controller_vrf_export.h>
#include <controller/controller_export.h>

Peer::Peer(Type type, const std::string &name) : 
    type_(type), name_(name){ 
}

Peer::~Peer() {
}

BgpPeer::BgpPeer(const Ip4Address &server_ip, const std::string &name,
                 AgentXmppChannel *bgp_xmpp_peer, DBTableBase::ListenerId id)
    : Peer(Peer::BGP_PEER, name), server_ip_(server_ip), id_(id), 
    bgp_xmpp_peer_(bgp_xmpp_peer), 
    route_walker_(new ControllerRouteWalker(bgp_xmpp_peer_->agent(), this)) {
        is_disconnect_walk_ = false;
        setup_time_ = UTCTimestampUsec();
}

BgpPeer::~BgpPeer() {
    // TODO verify if this unregister can be done in walkdone callback 
    // for delpeer
    if ((id_ != -1) && route_walker_->agent()->vrf_table()) {
        route_walker_->agent()->vrf_table()->Unregister(id_);
    }
}

void BgpPeer::DelPeerRoutes(DelPeerDone walk_done_cb) {
    route_walker_->Start(ControllerRouteWalker::DELPEER, false, walk_done_cb);
}

void BgpPeer::PeerNotifyRoutes() {
    route_walker_->Start(ControllerRouteWalker::NOTIFYALL, true, NULL);
}

void BgpPeer::PeerNotifyMulticastRoutes(bool associate) {
    route_walker_->Start(ControllerRouteWalker::NOTIFYMULTICAST, associate, 
                         NULL);
}

void BgpPeer::StalePeerRoutes() {
    route_walker_->Start(ControllerRouteWalker::STALE, true, NULL);
}

/*
 * Get the VRF state and unregister from all route table using
 * rt_export listener id. This will be called for active and non active bgp
 * peers. In case of active bgp peers send unsubscribe to control node(request 
 * came via vrf delete).
 */
void BgpPeer::DeleteVrfState(DBTablePartBase *partition,
                             DBEntryBase *entry) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);

    DBTableBase::ListenerId id = GetVrfExportListenerId();
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>
        (GetVrfExportState(partition, entry));

    if (vrf_state == NULL)
        return;
    
    if (vrf->GetName().compare(agent()->fabric_vrf_name()) != 0) {
        for (uint8_t table_type = (Agent::INVALID + 1);
                table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
            if (vrf_state->rt_export_[table_type]) 
                vrf_state->rt_export_[table_type]->Unregister();
        }
    }

    if (vrf_state->exported_ == true) {
        // Check if the notification is for active bgp peer or not.
        // Send unsubscribe only for active bgp peer.
        // Note that decommisioned bgp_peer_id can have reference to parent 
        // agentxmppchannel, however agentzmppchannel wud have moved to some
        // other new peer.
        if (bgp_xmpp_peer_ && (bgp_xmpp_peer_->bgp_peer_id() == this) && 
            AgentXmppChannel::IsBgpPeerActive(bgp_xmpp_peer_)) {
            AgentXmppChannel::ControllerSendSubscribe(bgp_xmpp_peer_, vrf, 
                                                      false); 
        }
    }

    vrf->ClearState(partition->parent(), id);
    delete vrf_state;

    return;
}

// For given peer return the dbstate for given VRF and partition
DBState *BgpPeer::GetVrfExportState(DBTablePartBase *partition, 
                                    DBEntryBase *entry) {
    DBTableBase::ListenerId id = GetVrfExportListenerId();
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);
    return (static_cast<VrfExport::State *>(vrf->GetState(partition->parent(), 
                                                          id)));
}

// For given route return the dbstate for given partiton
DBState *BgpPeer::GetRouteExportState(DBTablePartBase *partition, 
                                      DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    VrfEntry *vrf = route->vrf();

    DBTablePartBase *vrf_partition = agent()->vrf_table()->
        GetTablePartition(vrf);

    VrfExport::State *vs = static_cast<VrfExport::State *>
        (GetVrfExportState(vrf_partition, vrf)); 

    if (vs == NULL)
        return NULL;

    Agent::RouteTableType table_type = route->GetTableType();
    RouteExport::State *state = NULL;
    if (vs->rt_export_[table_type]) {
        state = static_cast<RouteExport::State *>(route->GetState(partition->
                                                                  parent(),
                            vs->rt_export_[table_type]->GetListenerId()));
    }
    return state;
}

Agent *BgpPeer::agent() const {
    return route_walker_.get()->agent();
}
