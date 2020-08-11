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
#include <controller/controller_init.h>

Peer::Peer(Type type, const std::string &name, bool export_to_controller) :
    type_(type), name_(name), export_to_controller_(export_to_controller) {
    refcount_ = 0;
    sequence_number_ = 0;
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

BgpPeer::BgpPeer(AgentXmppChannel *channel, const Ip4Address &server_ip,
                 const std::string &name, DBTableBase::ListenerId id,
                 Peer::Type bgp_peer_type) :
    DynamicPeer(channel->agent(), bgp_peer_type, name, false),
    channel_(channel), server_ip_(server_ip), id_(id),
    delete_stale_walker_(NULL), route_walker_cb_(NULL),
    delete_stale_walker_cb_(NULL) {
        AllocPeerNotifyWalker();
        AllocDeleteStaleWalker();
        AllocDeletePeerWalker();
        setup_time_ = UTCTimestampUsec();
}

BgpPeer::~BgpPeer() {
    const Agent *agent = route_walker()->agent();
    // TODO verify if this unregister can be done in walkdone callback
    // for delpeer
    if ((id_ != -1) && agent->vrf_table()) {
        agent->vrf_table()->Unregister(id_);
    }
    ReleaseDeleteStaleWalker();
    ReleaseDeletePeerWalker();
    ReleasePeerNotifyWalker();
}

// Route notify walker routines
void BgpPeer::AllocPeerNotifyWalker() {
    if (!route_walker()) {
        Agent *agent = channel_->agent();
        route_walker_ = new ControllerRouteWalker(server_ip_.to_string(),
                                                  this);
        agent->oper_db()->agent_route_walk_manager()->
            RegisterWalker(static_cast<AgentRouteWalker *>
                           (route_walker_.get()));
    }
}

void BgpPeer::ReleasePeerNotifyWalker() {
    if (!route_walker()) {
        return;
    }

    Agent *agent = Agent::GetInstance();
    agent->oper_db()->agent_route_walk_manager()->ReleaseWalker(route_walker());
    route_walker_.reset();
}

void BgpPeer::PeerNotifyRoutes(WalkDoneCb cb) {
    route_walker_cb_ = cb;
    route_walker()->Start(ControllerRouteWalker::NOTIFYALL, true,
                          route_walker_cb_);
}

void BgpPeer::StopPeerNotifyRoutes() {
    //No implementation of stop, to stop a walk release walker. Re-allocate for
    //further use.
    ReleasePeerNotifyWalker();
    AllocPeerNotifyWalker();
}

void BgpPeer::PeerNotifyMulticastRoutes(bool associate) {
    route_walker()->Start(ControllerRouteWalker::NOTIFYMULTICAST, associate,
                          NULL);
}

// Delete stale walker routines
void BgpPeer::AllocDeleteStaleWalker() {
    if (!delete_stale_walker()) {
        Agent *agent = channel_->agent();
        delete_stale_walker_ = new ControllerRouteWalker(server_ip_.to_string(),
                                                         this);
        agent->oper_db()->agent_route_walk_manager()->
            RegisterWalker(static_cast<AgentRouteWalker *>
                           (delete_stale_walker_.get()));
    }
}

void BgpPeer::ReleaseDeleteStaleWalker() {
    if (!delete_stale_walker()) {
        return;
    }

    Agent *agent = Agent::GetInstance();
    agent->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(delete_stale_walker());
    delete_stale_walker_.reset();
}

// Delete stale walker routines
void BgpPeer::AllocDeletePeerWalker() {
    if (!delete_peer_walker()) {
        Agent *agent = channel_->agent();
        delete_peer_walker_ = new ControllerRouteWalker(server_ip_.to_string(),
                                                         this);
        agent->oper_db()->agent_route_walk_manager()->
            RegisterWalker(static_cast<AgentRouteWalker *>
                           (delete_peer_walker_.get()));
    }
}

void BgpPeer::ReleaseDeletePeerWalker() {
    if (!delete_peer_walker()) {
        return;
    }

    Agent *agent = Agent::GetInstance();
    agent->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(delete_peer_walker());
    delete_peer_walker_.reset();
}

void BgpPeer::DelPeerRoutes(WalkDoneCb walk_done_cb,
                            uint64_t sequence_number) {
    //Since peer is getting deleted no need of seperate walk to delete stale or
    //non stale paths.
    ReleaseDeleteStaleWalker();
    delete_peer_walker_cb_ = walk_done_cb;
    delete_peer_walker()->set_sequence_number(sequence_number);
    delete_peer_walker()->Start(ControllerRouteWalker::DELPEER, false,
                          delete_peer_walker_cb_);
}

void BgpPeer::DeleteStale() {
    //If peer is marked for deletion skip. Deletion should take care of removing
    //routes.
    if (SkipAddChangeRequest())
        return;

    delete_stale_walker()->set_sequence_number(sequence_number());
    delete_stale_walker()->Start(ControllerRouteWalker::DELSTALE, false,
                                delete_stale_walker_cb_);
}

void BgpPeer::StopDeleteStale() {
    //No implementation of stop, to stop a walk release walker. Re-allocate for
    //further use.
    ReleaseDeleteStaleWalker();
    AllocDeleteStaleWalker();
}

ControllerRouteWalker *BgpPeer::route_walker() const {
    return static_cast<ControllerRouteWalker *>(route_walker_.get());
}

ControllerRouteWalker *BgpPeer::delete_stale_walker() const {
    return static_cast<ControllerRouteWalker *>(delete_stale_walker_.get());
}

ControllerRouteWalker *BgpPeer::delete_peer_walker() const {
    return static_cast<ControllerRouteWalker *>(delete_peer_walker_.get());
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

    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        if (vrf_state->rt_export_[table_type])
            vrf_state->rt_export_[table_type]->Unregister();
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
    return channel_->agent();
}

AgentXmppChannel *BgpPeer::GetAgentXmppChannel() const {
    return channel_;
}

uint64_t BgpPeer::ChannelSequenceNumber() const {
    return GetAgentXmppChannel()->sequence_number();
}

void BgpPeer::set_route_walker_cb(WalkDoneCb cb) {
    route_walker_cb_ = cb;
}

void BgpPeer::set_delete_stale_walker_cb(WalkDoneCb cb) {
    delete_stale_walker_cb_ = cb;
}
void BgpPeer::set_delete_peer_walker_cb(WalkDoneCb cb) {
    delete_peer_walker_cb_ = cb;
}
