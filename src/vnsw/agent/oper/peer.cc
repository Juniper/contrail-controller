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

Peer::Peer(Type type, const std::string &name, bool export_to_controller) :
    type_(type), name_(name), export_to_controller_(export_to_controller) {
    refcount_ = 0;
}

Peer::~Peer() {
}

bool Peer::DeleteOnZeroRefcount() const {
    return false;
}

void intrusive_ptr_add_ref(const Peer *p) {
    p->refcount_.fetch_and_increment();
    // validate that reference is not taken while delete is in progress
    assert(!p->IsDeleted());
}

void intrusive_ptr_release(const Peer *p) {
    if (p->refcount_.fetch_and_decrement() == 1 && p->DeleteOnZeroRefcount()) {
        delete p;
    }
}

DynamicPeer::DynamicPeer(Agent *agent, Type type, const std::string &name,
                         bool export_to_controller) :
    Peer(type, name, export_to_controller) {
    delete_timeout_timer_ = TimerManager::CreateTimer(
                                *(agent->event_manager())->io_service(),
                                "Dynamic Peer Delete Timer",
                                agent->task_scheduler()->\
                                GetTaskId("db::DBTable"), 0);
    deleted_ = false;
    skip_add_change_ = false;
}

DynamicPeer::~DynamicPeer() {
    // Dynamic Peer should be marked deleted and will free
    // automatically once all the references go away
    assert(deleted_);
    assert(refcount() == 0);
    TimerManager::DeleteTimer(delete_timeout_timer_);
}

void DynamicPeer::ProcessDelete(DynamicPeer *p) {
    p->StopRouteExports();

    if (p->deleted_.fetch_and_store(true)) {
        return;
    }

    if (p->refcount() != 0) {
        // still pending references are there start delete timeout timer
        p->delete_timeout_timer_->Start(kDeleteTimeout,
                                     boost::bind(&DynamicPeer::DeleteTimeout,
                                     p));
        return;
    }

    // no pending references delete the peer inline and return
    delete p;
}

bool DynamicPeer::DeleteTimeout() {
    assert(0);
    return false;
}

bool DynamicPeer::DeleteOnZeroRefcount() const {
    if (!deleted_) {
        return false;
    }

    // last reference has gone, cancel the timer and delete peer
    delete_timeout_timer_->Cancel();

    return true;
}

const Ip4Address *Peer::NexthopIp(Agent *agent, const AgentPath *path) const {
    return agent->router_ip_ptr();
}

BgpPeer::BgpPeer(const Ip4Address &server_ip, const std::string &name,
                 Agent *agent, DBTableBase::ListenerId id,
                 Peer::Type bgp_peer_type) :
    DynamicPeer(agent, bgp_peer_type, name, false),
    server_ip_(server_ip), id_(id),
    route_walker_(new ControllerRouteWalker(agent, this)) {
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
        // If skip_add_change is set for this dynamic peer, then dont export.
        if (SkipAddChangeRequest() == false) {
            AgentXmppChannel::ControllerSendSubscribe(GetAgentXmppChannel(),
                                                      vrf,
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

AgentXmppChannel *BgpPeer::GetAgentXmppChannel() const {
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppChannel *channel =
            agent()->controller_xmpp_channel(count);
        if (channel && (channel->bgp_peer_id() == this)) {
            return channel;
        }
    }
    return NULL;
}
