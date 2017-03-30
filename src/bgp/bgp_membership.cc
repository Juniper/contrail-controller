/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_membership.h"

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_export.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/routing-instance/routing_instance.h"

using std::list;
using std::make_pair;
using std::string;
using std::vector;

//
// Constructor for BgpMembershipManager.
//
BgpMembershipManager::BgpMembershipManager(BgpServer *server)
    : server_(server),
      walker_(new Walker(this)),
      event_queue_(new WorkQueue<Event *>(
          TaskScheduler::GetInstance()->GetTaskId("bgp::PeerMembership"), 0,
          boost::bind(&BgpMembershipManager::EventCallback, this, _1))) {
    current_jobs_count_ = 0;
    total_jobs_count_ = 0;
}

//
// Destructor for BgpMembershipManager.
//
BgpMembershipManager::~BgpMembershipManager() {
    assert(current_jobs_count_ == 0);
    assert(rib_state_map_.empty());
    assert(peer_state_map_.empty());
}

int BgpMembershipManager::RegisterPeerRegistrationCallback(
    PeerRegistrationCallback callback) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);

    size_t id = registration_bmap_.find_first();
    if (id == registration_bmap_.npos) {
        id = registration_callbacks_.size();
        registration_callbacks_.push_back(callback);
    } else {
        registration_bmap_.reset(id);
        if (registration_bmap_.none()) {
            registration_bmap_.clear();
        }
        registration_callbacks_[id] = callback;
    }
    return id;
}

void BgpMembershipManager::UnregisterPeerRegistrationCallback(int id) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);

    registration_callbacks_[id] = NULL;
    if ((size_t) id == registration_callbacks_.size() - 1) {
        while (!registration_callbacks_.empty() &&
               registration_callbacks_.back() == NULL) {
            registration_callbacks_.pop_back();
        }
        if (registration_bmap_.size() > registration_callbacks_.size()) {
            registration_bmap_.resize(registration_callbacks_.size());
        }
    } else {
        if ((size_t) id >= registration_bmap_.size()) {
            registration_bmap_.resize(id + 1);
        }
        registration_bmap_.set(id);
    }
}

void BgpMembershipManager::NotifyPeerRegistration(IPeer *peer, BgpTable *table,
    bool unregister) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    if (!peer->IsXmppPeer())
        return;

    for (PeerRegistrationListenerList::iterator iter =
         registration_callbacks_.begin();
         iter != registration_callbacks_.end(); ++iter) {
        if (*iter != NULL) {
            PeerRegistrationCallback callback = *iter;
            (callback)(peer, table, unregister);
        }
    }
}

//
// Register the IPeer to the BgpTable.
// Post a REGISTER_RIB event to deal with concurrency issues with RibOut.
//
void BgpMembershipManager::Register(IPeer *peer, BgpTable *table,
    const RibExportPolicy &policy, int instance_id) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper",
        "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    current_jobs_count_++;
    total_jobs_count_++;
    PeerRibState *prs = LocatePeerRibState(peer, table);
    assert(prs->action() == NONE);
    assert(!prs->ribout_registered());
    prs->set_ribin_registered(true);
    prs->set_action(RIBOUT_ADD);
    Event *event = new Event(REGISTER_RIB, peer, table, policy, instance_id);
    EnqueueEvent(event);
}

//
// Synchronously register the IPeer to the BgpTable for RIBIN.
//
void BgpMembershipManager::RegisterRibIn(IPeer *peer, BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper",
        "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    PeerRibState *prs = LocatePeerRibState(peer, table);
    assert(prs->action() == NONE);
    assert(!prs->ribin_registered() || peer->IsInGRTimerWaitState());
    assert(!prs->ribout_registered());
    prs->set_ribin_registered(true);
}

//
// Unregister the IPeer from the BgpTable.
// Post an UNREGISTER_RIB event to deal with concurrency issues with RibOut.
//
void BgpMembershipManager::Unregister(IPeer *peer, BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    current_jobs_count_++;
    total_jobs_count_++;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == NONE);
    assert(prs->ribin_registered());

    if (!prs->ribout_registered()) {
        UnregisterRibInUnlocked(prs);
        return;
    }

    prs->set_action(RIBIN_DELETE_RIBOUT_DELETE);
    prs->set_ribin_registered(false);
    prs->set_instance_id(-1);
    prs->set_subscription_gen_id(0);
    Event *event = new Event(UNREGISTER_RIB, peer, table);
    EnqueueEvent(event);
}

//
// Unregister the IPeer from the BgpTable for RIBIN.
//
void BgpMembershipManager::UnregisterRibIn(IPeer *peer, BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    current_jobs_count_++;
    total_jobs_count_++;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == NONE);
    assert(prs->ribin_registered() && !prs->ribout_registered());
    UnregisterRibInUnlocked(prs);
}

//
// Common routine to handle unregister of IPeer from Table for RIBIN.
//
void BgpMembershipManager::UnregisterRibInUnlocked(PeerRibState *prs) {
    prs->set_ribin_registered(false);
    prs->set_instance_id(-1);
    prs->set_subscription_gen_id(0);
    prs->set_action(RIBIN_DELETE);
    prs->UnregisterRibIn();
    BGP_LOG_PEER_TABLE(prs->peer(), SandeshLevel::SYS_DEBUG,
        BGP_LOG_FLAG_SYSLOG, prs->table(),
        "Unregister table requested for action " << prs->action());
}

//
// Unregister the IPeer from the BgpTable.
// Post an UNREGISTER_RIB event to deal with concurrency issues with RibOut.
// The action is set to RIBIN_WALK_RIBOUT_DELETE.
// This API is to be used when handling graceful restart of the peer.
//
void BgpMembershipManager::UnregisterRibOut(IPeer *peer, BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    current_jobs_count_++;
    total_jobs_count_++;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == NONE);
    assert(prs->ribin_registered());
    assert(prs->ribout_registered());
    prs->set_instance_id(-1);
    prs->set_subscription_gen_id(0);
    prs->set_action(RIBIN_WALK_RIBOUT_DELETE);
    Event *event = new Event(UNREGISTER_RIB, peer, table);
    EnqueueEvent(event);
}

//
// Trigger a walk of IPeer's RIBIN for the BgpTable.
// This API can be used when sweeping paths as part of graceful restart.
// It can also be used in future when re-evaluating import policy for a peer.
//
void BgpMembershipManager::WalkRibIn(IPeer *peer, BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::StateMachine", "xmpp::StateMachine");

    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    current_jobs_count_++;
    total_jobs_count_++;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == NONE);
    assert(prs->ribin_registered());
    prs->set_action(RIBIN_WALK);
    prs->WalkRibIn();
    BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
        table, "Walk table requested for action " << prs->action());
}

//
// Fill in the registration info of the IPeer for the BgpTable.
// Return true if the IPeer is registered with the BgpTable, false otherwise.
//
bool BgpMembershipManager::GetRegistrationInfo(
    const IPeer *peer, const BgpTable *table,
    int *instance_id, uint64_t *subscription_gen_id) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    const PeerRibState *prs = FindPeerRibState(peer, table);
    if (!prs)
        return false;
    if (instance_id)
        *instance_id = prs->instance_id();
    if (subscription_gen_id)
        *subscription_gen_id = prs->subscription_gen_id();
    return true;
}

//
// Update the registration info of the IPeer for the BgpTable.
//
void BgpMembershipManager::SetRegistrationInfo(
    const IPeer *peer, const BgpTable *table,
    int instance_id, uint64_t subscription_gen_id) {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    PeerRibState *prs = FindPeerRibState(peer, table);
    if (!prs)
        return;
    prs->set_instance_id(instance_id);
    prs->set_subscription_gen_id(subscription_gen_id);
}

//
// Return true if the IPeer is registered to the BgpTable.
//
bool BgpMembershipManager::IsRegistered(const IPeer *peer,
    const BgpTable *table) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    const PeerRibState *prs = FindPeerRibState(peer, table);
    return (prs && prs->ribin_registered() && prs->ribout_registered());
}

//
// Return true if the IPeer is registered to the BgpTable for RibIn.
//
bool BgpMembershipManager::IsRibInRegistered(const IPeer *peer,
    const BgpTable *table) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    const PeerRibState *prs = FindPeerRibState(peer, table);
    return (prs && prs->ribin_registered());
}

//
// Return true if the IPeer is registered to the BgpTable for RibOut.
//
bool BgpMembershipManager::IsRibOutRegistered(const IPeer *peer,
    const BgpTable *table) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    const PeerRibState *prs = FindPeerRibState(peer, table);
    return (prs && prs->ribout_registered());
}

//
// Return RibOut's output queue depth.
//
uint32_t BgpMembershipManager::GetRibOutQueueDepth(const IPeer *peer,
    const BgpTable *table) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    const PeerRibState *prs = FindPeerRibState(peer, table);
    if (!prs || !prs->ribout_registered())
        return 0;
    RibOut *ribout = prs->ribout();
    if (!ribout)
        return 0;
    return ribout->GetQueueSize();
}

//
// Fill in the list of registered BgpTables for given IPeer.
//
void BgpMembershipManager::GetRegisteredRibs(const IPeer *peer,
    list<BgpTable *> *table_list) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    table_list->clear();
    const PeerState *ps = FindPeerState(peer);
    if (ps)
        ps->GetRegisteredRibs(table_list);
}

//
//
// Fill membership introspect information for a BgpTable.
//
void BgpMembershipManager::FillRoutingInstanceTableInfo(
    ShowRoutingInstanceTable *srit, const BgpTable *table) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    BgpTable *nc_table = const_cast<BgpTable *>(table);
    const RibState *rs = FindRibState(nc_table);
    if (rs)
        rs->FillRoutingInstanceTableInfo(srit);
}

//
// Fill membership introspect information for an IPeer.
//
void BgpMembershipManager::FillPeerMembershipInfo(const IPeer *peer,
        BgpNeighborResp *resp) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    assert(resp->get_routing_tables().empty());
    IPeer *nc_peer = const_cast<IPeer *>(peer);

    BgpUpdateSender *sender = server_->update_sender();
    if (sender->PeerIsRegistered(nc_peer)) {
        resp->set_send_state(
            sender->PeerInSync(nc_peer) ? "in sync" : "not in sync");
    } else {
        resp->set_send_state("not advertising");
    }

    const PeerState *ps = FindPeerState(nc_peer);
    if (ps)
        ps->FillPeerMembershipInfo(resp);
}

//
// Return true if no pending work in the BgpMembershipManager itself and
// in the Walker.
//
bool BgpMembershipManager::IsQueueEmpty() const {
    return (event_queue_->IsQueueEmpty() && walker_->IsQueueEmpty());
}

//
// Return number of PeerRibStates.
//
size_t BgpMembershipManager::GetMembershipCount() const {
    size_t count = 0;
    for (PeerStateMap::const_iterator loc = peer_state_map_.begin();
         loc != peer_state_map_.end(); ++loc) {
        const PeerState *ps = loc->second;
        count += ps->GetMembershipCount();
    }
    return count;
}

//
// Find or create the PeerState for given IPeer.
//
BgpMembershipManager::PeerState *BgpMembershipManager::LocatePeerState(
    IPeer *peer) {
    PeerStateMap::iterator loc = peer_state_map_.find(peer);
    if (loc == peer_state_map_.end()) {
        PeerState *ps = new PeerState(this, peer);
        peer_state_map_.insert(make_pair(peer, ps));
        return ps;
    } else {
        return loc->second;
    }
}

//
// Find the PeerState for given IPeer.
//
BgpMembershipManager::PeerState *BgpMembershipManager::FindPeerState(
    const IPeer *peer) {
    PeerStateMap::iterator loc = peer_state_map_.find(peer);
    return (loc != peer_state_map_.end() ? loc->second : NULL);
}

//
// Find the PeerState for given IPeer.
// Const version.
//
const BgpMembershipManager::PeerState *BgpMembershipManager::FindPeerState(
    const IPeer *peer) const {
    PeerStateMap::const_iterator loc = peer_state_map_.find(peer);
    return (loc != peer_state_map_.end() ? loc->second : NULL);
}

//
// Destroy the given PeerState.
//
void BgpMembershipManager::DestroyPeerState(PeerState *ps) {
    peer_state_map_.erase(ps->peer());
    delete ps;
}

//
// Find or create the RibState for given BgpTable.
//
BgpMembershipManager::RibState *BgpMembershipManager::LocateRibState(
    BgpTable *table) {
    RibStateMap::iterator loc = rib_state_map_.find(table);
    if (loc == rib_state_map_.end()) {
        RibState *rs = new RibState(this, table);
        rib_state_map_.insert(make_pair(table, rs));
        return rs;
    } else {
        return loc->second;
    }
}

//
// Find the RibState for given BgpTable.
//
BgpMembershipManager::RibState *BgpMembershipManager::FindRibState(
    const BgpTable *table) {
    RibStateMap::iterator loc = rib_state_map_.find(table);
    return (loc != rib_state_map_.end() ? loc->second : NULL);
}

//
// Find the RibState for given BgpTable.
// Const version.
//
const BgpMembershipManager::RibState *BgpMembershipManager::FindRibState(
    const BgpTable *table) const {
    RibStateMap::const_iterator loc = rib_state_map_.find(table);
    return (loc != rib_state_map_.end() ? loc->second : NULL);
}

//
// Destroy the given RibState.
//
void BgpMembershipManager::DestroyRibState(RibState *rs) {
    rib_state_map_.erase(rs->table());
    delete rs;
}

//
// Request the Walker to schedule a table walk for the given RibState.
// Note that the Walker accumulates requests and starts walks asynchronously.
//
void BgpMembershipManager::EnqueueRibState(RibState *rs) {
    walker_->Enqueue(rs);
}

//
// Find or create the PeerRibState for given (IPeer, BgpTable).
//
BgpMembershipManager::PeerRibState *BgpMembershipManager::LocatePeerRibState(
    IPeer *peer, BgpTable *table) {
    PeerState *ps = LocatePeerState(peer);
    RibState *rs = LocateRibState(table);
    PeerRibState *prs = ps->LocatePeerRibState(rs);
    rs->InsertPeerRibState(prs);
    return prs;
}

//
// Find the PeerRibState for given (IPeer, BgpTable).
//
BgpMembershipManager::PeerRibState *BgpMembershipManager::FindPeerRibState(
    const IPeer *peer, const BgpTable *table) {
    PeerState *ps = FindPeerState(peer);
    RibState *rs = FindRibState(table);
    return (ps && rs ? ps->FindPeerRibState(rs) : NULL);
}

//
// Find the PeerRibState for given (IPeer, BgpTable).
// Const version.
//
const BgpMembershipManager::PeerRibState *
BgpMembershipManager::FindPeerRibState(
    const IPeer *peer, const BgpTable *table) const {
    const PeerState *ps = FindPeerState(peer);
    const RibState *rs = FindRibState(table);
    return (ps && rs ? ps->FindPeerRibState(rs) : NULL);
}

//
// Destroy the given PeerRibState.
// Also destroy the PeerState and/or RibState if they are no longer required.
//
void BgpMembershipManager::DestroyPeerRibState(PeerRibState *prs) {
    PeerState *ps = prs->peer_state();
    RibState *rs = prs->rib_state();
    if (ps->RemovePeerRibState(prs))
        DestroyPeerState(ps);
    if (rs->RemovePeerRibState(prs))
        DestroyRibState(rs);
    delete prs;
}

//
// Trigger REGISTER_RIB_COMPLETE event.
//
void BgpMembershipManager::TriggerRegisterRibCompleteEvent(IPeer *peer,
    BgpTable *table) {
    Event *event = new Event(REGISTER_RIB_COMPLETE, peer, table);
    EnqueueEvent(event);
}

//
// Trigger UNREGISTER_RIB_COMPLETE event.
//
void BgpMembershipManager::TriggerUnregisterRibCompleteEvent(IPeer *peer,
    BgpTable *table) {
    Event *event = new Event(UNREGISTER_RIB_COMPLETE, peer, table);
    EnqueueEvent(event);
}

//
// Trigger WALK_RIB_COMPLETE event.
//
void BgpMembershipManager::TriggerWalkRibCompleteEvent(IPeer *peer,
    BgpTable *table) {
    Event *event = new Event(WALK_RIB_COMPLETE, peer, table);
    EnqueueEvent(event);
}

//
// Process REGISTER_RIB event.
//
void BgpMembershipManager::ProcessRegisterRibEvent(Event *event) {
    IPeer *peer = event->peer;
    BgpTable *table = event->table;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == RIBOUT_ADD);
    assert(prs->ribin_registered());
    prs->set_instance_id(event->instance_id);

    // Notify completion right away if the table is marked for deletion.
    // Mark the ribout as registered even though no RibOut gets created.
    // The unregister code path handles a PeerRibState without a RibOut.
    if (table->IsDeleted()) {
        prs->set_ribout_registered(true);
        prs->clear_action();
        peer->MembershipRequestCallback(table);
        current_jobs_count_--;
        return;
    }

    prs->RegisterRibOut(event->policy);
    BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
        table, "Register table requested for action " << prs->action());
}


//
// Process REGISTER_RIB_COMPLETE event.
//
void BgpMembershipManager::ProcessRegisterRibCompleteEvent(Event *event) {
    IPeer *peer = event->peer;
    BgpTable *table = event->table;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs && prs->action() == RIBOUT_ADD);
    assert(prs->ribin_registered());
    assert(prs->ribout_registered());
    BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
        table, "Register table completed for action " << prs->action());
    prs->clear_action();
    peer->MembershipRequestCallback(table);
    NotifyPeerRegistration(peer, table, false);
    current_jobs_count_--;
}

//
// Process UNREGISTER_RIB event.
//
void BgpMembershipManager::ProcessUnregisterRibEvent(Event *event) {
    IPeer *peer = event->peer;
    BgpTable *table = event->table;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs);
    assert(prs->action() == RIBIN_DELETE_RIBOUT_DELETE ||
        prs->action() == RIBIN_WALK_RIBOUT_DELETE);
    if (prs->action() == RIBIN_DELETE_RIBOUT_DELETE)
        assert(!prs->ribin_registered());
    if (prs->action() == RIBIN_WALK_RIBOUT_DELETE)
        assert(prs->ribin_registered());
    assert(prs->ribout_registered());

    prs->DeactivateRibOut();
    BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
        table, "Unregister table requested for action " << prs->action());
}

//
// Process UNREGISTER_RIB_COMPLETE event.
//
void BgpMembershipManager::ProcessUnregisterRibCompleteEvent(Event *event) {
    IPeer *peer = event->peer;
    BgpTable *table = event->table;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs);
    assert(prs->action() == RIBIN_DELETE_RIBOUT_DELETE ||
        prs->action() == RIBIN_WALK_RIBOUT_DELETE);
    if (prs->action() == RIBIN_DELETE_RIBOUT_DELETE)
        assert(!prs->ribin_registered());
    if (prs->action() == RIBIN_WALK_RIBOUT_DELETE)
        assert(prs->ribin_registered());

    prs->UnregisterRibOut();
    BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
        table, "Unregister table completed for action " << prs->action());
    prs->clear_action();
    if (!prs->ribin_registered() && !prs->ribout_registered())
        DestroyPeerRibState(prs);

    peer->MembershipRequestCallback(table);
    NotifyPeerRegistration(peer, table, true);
    current_jobs_count_--;
}

//
// Process WALK_RIB_COMPLETE event.
//
void BgpMembershipManager::ProcessWalkRibCompleteEvent(Event *event) {
    IPeer *peer = event->peer;
    BgpTable *table = event->table;
    PeerRibState *prs = FindPeerRibState(peer, table);
    assert(prs);
    assert(prs->action() == RIBIN_WALK || prs->action() == RIBIN_DELETE);
    if (prs->action() == RIBIN_WALK) {
        assert(prs->ribin_registered());
        BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
            table, "Walk table completed for action " << prs->action());
    } else {
        assert(!prs->ribin_registered());
        BGP_LOG_PEER_TABLE(peer, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
            table, "Unregister table completed for action " << prs->action());
    }
    prs->clear_action();
    if (!prs->ribin_registered() && !prs->ribout_registered())
        DestroyPeerRibState(prs);
    peer->MembershipRequestCallback(table);
    current_jobs_count_--;
}

//
// Internal handler for an Event.
// Exists so that test code can override it.
//
bool BgpMembershipManager::EventCallbackInternal(Event *event) {
    switch (event->event_type) {
    case REGISTER_RIB:
        ProcessRegisterRibEvent(event);
        break;
    case REGISTER_RIB_COMPLETE:
        ProcessRegisterRibCompleteEvent(event);
        break;
    case UNREGISTER_RIB:
        ProcessUnregisterRibEvent(event);
        break;
    case UNREGISTER_RIB_COMPLETE:
        ProcessUnregisterRibCompleteEvent(event);
        break;
    case WALK_RIB_COMPLETE:
        ProcessWalkRibCompleteEvent(event);
        break;
    default:
        assert(false);
        break;
    }

    delete event;
    return true;
}

//
// Handler for an Event.
//
bool BgpMembershipManager::EventCallback(Event *event) {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    return EventCallbackInternal(event);
}

//
// Constructor.
//`
BgpMembershipManager::Event::Event(EventType event_type, IPeer *peer,
    BgpTable *table)
    : event_type(event_type),
      peer(peer),
      table(table),
      instance_id(-1) {
}

//
// Constructor.
//`
BgpMembershipManager::Event::Event(EventType event_type, IPeer *peer,
    BgpTable *table, const RibExportPolicy &policy, int instance_id)
    : event_type(event_type),
      peer(peer),
      table(table),
      policy(policy),
      instance_id(instance_id) {
}

//
// Constructor.
//`
BgpMembershipManager::PeerState::PeerState(BgpMembershipManager *manager,
    IPeer *peer)
    : manager_(manager),
      peer_(peer) {
}

//
// Destructor.
//`
BgpMembershipManager::PeerState::~PeerState() {
    assert(rib_map_.empty());
}

//
// Find or create the PeerRibState for given RibState.
//
BgpMembershipManager::PeerRibState *
BgpMembershipManager::PeerState::LocatePeerRibState(RibState *rs) {
    PeerRibStateMap::iterator loc = rib_map_.find(rs);
    if (loc == rib_map_.end()) {
        PeerRibState *prs = new PeerRibState(manager_, this, rs);
        rib_map_.insert(make_pair(rs, prs));
        return prs;
    } else {
        return loc->second;
    }
}

//
// Find the PeerRibState for given RibState.
//
BgpMembershipManager::PeerRibState *
BgpMembershipManager::PeerState::FindPeerRibState(const RibState *rs) {
    PeerRibStateMap::iterator loc = rib_map_.find(rs);
    return (loc != rib_map_.end() ? loc->second : NULL);
}

//
// Find the PeerRibState for given RibState.
// Const version.
//
const BgpMembershipManager::PeerRibState *
BgpMembershipManager::PeerState::FindPeerRibState(const RibState *rs) const {
    PeerRibStateMap::const_iterator loc = rib_map_.find(rs);
    return (loc != rib_map_.end() ? loc->second : NULL);
}

//
// Remove given PeerRibState from PeerRibStateMap.
// Return true if the PeerState itself can we deleted.
//
bool BgpMembershipManager::PeerState::RemovePeerRibState(PeerRibState *prs) {
    PeerRibStateMap::iterator loc = rib_map_.find(prs->rib_state());
    if (loc != rib_map_.end())
        rib_map_.erase(loc);
    return rib_map_.empty();
}

//
// Fill in the list of registered BgpTables.
//
void BgpMembershipManager::PeerState::GetRegisteredRibs(
    list<BgpTable *> *table_list) const {
    for (PeerRibStateMap::const_iterator loc = rib_map_.begin();
         loc != rib_map_.end(); ++loc) {
        const RibState *rs = loc->first;
        table_list->push_back(rs->table());
    }
}

//
// Fill introspect information.
//
void BgpMembershipManager::PeerState::FillPeerMembershipInfo(
    BgpNeighborResp *resp) const {
    vector<BgpNeighborRoutingTable> table_list;
    for (PeerRibStateMap::const_iterator loc = rib_map_.begin();
         loc != rib_map_.end(); ++loc) {
        const RibState *rs = loc->first;
        BgpNeighborRoutingTable table;
        table.set_name(rs->table()->name());
        table.set_current_state("subscribed");
        table_list.push_back(table);
    }
    resp->set_routing_tables(table_list);
}

//
// Constructor.
//
BgpMembershipManager::RibState::RibState(BgpMembershipManager *manager,
    BgpTable *table)
    : manager_(manager),
      table_(table),
      request_count_(0),
      walk_count_(0),
      table_delete_ref_(this, table->deleter()) {
}

//
// Destructor.
//
BgpMembershipManager::RibState::~RibState() {
    assert(peer_rib_list_.empty());
    assert(pending_peer_rib_list_.empty());
}

//
// Enqueue given PeerRibState into the pending PeerRibStateList.
//
void BgpMembershipManager::RibState::EnqueuePeerRibState(PeerRibState *prs) {
    request_count_++;
    pending_peer_rib_list_.insert(prs);
    manager_->EnqueueRibState(this);
}

//
// Clear the pending PeerRibStateList.
//
void BgpMembershipManager::RibState::ClearPeerRibStateList() {
    pending_peer_rib_list_.clear();
}

//
// Insert given PeerRibState into the regular PeerRibStateList.
//
void BgpMembershipManager::RibState::InsertPeerRibState(PeerRibState *prs) {
    peer_rib_list_.insert(prs);
}

//
// Remove given PeerRibState from the regular PeerRibStateList.
//
bool BgpMembershipManager::RibState::RemovePeerRibState(PeerRibState *prs) {
    peer_rib_list_.erase(prs);
    return peer_rib_list_.empty();
}

//
// Fill introspect information.
//
void BgpMembershipManager::RibState::FillRoutingInstanceTableInfo(
    ShowRoutingInstanceTable *srit) const {
    ShowTableMembershipInfo stmi;
    stmi.set_requests(request_count_);
    stmi.set_walks(walk_count_);
    vector<ShowMembershipPeerInfo> peers;
    for (PeerRibList::const_iterator it = peer_rib_list_.begin();
         it != peer_rib_list_.end(); ++it) {
        const PeerRibState *prs = *it;
        ShowMembershipPeerInfo smpi;
        prs->FillMembershipInfo(&smpi);
        peers.push_back(smpi);
    }
    stmi.set_peers(peers);
    srit->set_membership(stmi);
}

//
// Constructor.
//
BgpMembershipManager::PeerRibState::PeerRibState(BgpMembershipManager *manager,
    PeerState *ps, RibState *rs)
    : manager_(manager),
      ps_(ps),
      rs_(rs),
      ribout_(NULL),
      ribout_index_(-1),
      action_(BgpMembershipManager::NONE),
      ribin_registered_(false),
      ribout_registered_(false),
      instance_id_(-1),
      subscription_gen_id_(0) {
}

//
// Destructor.
//
BgpMembershipManager::PeerRibState::~PeerRibState() {
    assert(!ribout_);
    assert(ribout_index_ == -1);
    assert(action_ == BgpMembershipManager::NONE);
    assert(!ribin_registered_);
    assert(!ribout_registered_);
    assert(instance_id_ == -1);
    assert(subscription_gen_id_ == 0);
}

//
// Create RibOut for this PeerRibState and registers the RibOut as a listener
// for the BgpTable.
//
// Register the IPeer to the RibOut.
// This PeerRibState is added to the pending PeerRibStateList of RibState
// so that Join processing is handled when walking the BgpTable.
//
void BgpMembershipManager::PeerRibState::RegisterRibOut(
    const RibExportPolicy &policy) {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    BgpUpdateSender *sender = manager_->server()->update_sender();
    ribout_ = rs_->table()->RibOutLocate(sender, policy);
    ribout_->RegisterListener();
    ribout_->Register(ps_->peer());
    ribout_index_ = ribout_->GetPeerIndex(ps_->peer());
    ribout_registered_ = true;
    rs_->EnqueuePeerRibState(this);
}

//
// Deactivate the IPeer in the RibOut.
// This ensures that the IPeer will stop exporting routes from now onwards.
//
// Note that this is called before Leave processing for the IPeer is started.
//
// Bypass the Walker and directly post an UNREGISTER_RIB_COMPLETE event if
// there's no RibOut. This happens if the table was marked deleted when the
// register was processed.
//
void BgpMembershipManager::PeerRibState::DeactivateRibOut() {
    CHECK_CONCURRENCY("bgp::PeerMembership");
    if (ribout_) {
        ribout_->Deactivate(ps_->peer());
        rs_->EnqueuePeerRibState(this);
    } else {
        assert(ribout_index_ == -1);
        ribout_registered_ = false;
        manager_->TriggerUnregisterRibCompleteEvent(ps_->peer(), rs_->table());
    }
}

//
// Unregister the IPeer from the BgpTable.
// Unregister the IPeer from the RibOut, which may result in deletion of the
// RibOut itself.
//
// Note that this is called only after Leave processing for the IPeer has been
// completed.
//
void BgpMembershipManager::PeerRibState::UnregisterRibOut() {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    if (!ribout_)
        return;
    assert(ribout_index_ != -1);
    ribout_->Unregister(ps_->peer());
    ribout_ = NULL;
    ribout_index_ = -1;
    ribout_registered_ = false;
}

//
// Unregister the RibIn for the IPeer.
//
void BgpMembershipManager::PeerRibState::UnregisterRibIn() {
    rs_->EnqueuePeerRibState(this);
}

//
// Walk the RibIn for the IPeer.
//
void BgpMembershipManager::PeerRibState::WalkRibIn() {
    rs_->EnqueuePeerRibState(this);
}

//
// Fill introspect information.
//
void BgpMembershipManager::PeerRibState::FillMembershipInfo(
    ShowMembershipPeerInfo *smpi) const {
    smpi->set_peer(ps_->peer()->ToString());
    smpi->set_ribin_registered(ribin_registered_);
    smpi->set_ribout_registered(ribout_registered_);
    smpi->set_instance_id(instance_id_);
    smpi->set_generation_id(subscription_gen_id_);
}

//
// Constructor.
//
BgpMembershipManager::Walker::Walker(BgpMembershipManager *manager)
    : manager_(manager),
      trigger_(new TaskTrigger(
          boost::bind(&BgpMembershipManager::Walker::WalkTrigger, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::PeerMembership"), 0)),
      postpone_walk_(false),
      walk_started_(false),
      walk_completed_(false),
      rs_(NULL) {
}

//
// Destructor.
//
BgpMembershipManager::Walker::~Walker() {
    assert(rib_state_set_.empty());
    assert(rib_state_list_.empty());
    assert(!postpone_walk_);
    assert(!rs_);
    assert(walk_ref_ == NULL);
    assert(peer_rib_list_.empty());
    assert(peer_list_.empty());
    assert(ribout_state_map_.empty());
    assert(ribout_state_list_.empty());
}

//
// Add the given RibState to the RibStateList if it's not already present.
// Trigger processing of the RibStateList if a walk is not already in progress.
//
void BgpMembershipManager::Walker::Enqueue(RibState *rs) {
    if (rib_state_set_.find(rs) != rib_state_set_.end())
        return;
    rib_state_set_.insert(rs);
    rib_state_list_.push_back(rs);
    if (!walk_started_)
        trigger_->Set();
}

//
// Return true if the Walk does not have any pending items.
//
bool BgpMembershipManager::Walker::IsQueueEmpty() const {
    return (rib_state_list_.empty() && !trigger_->IsSet() && !rs_);
}

//
// Find or create the RibOutState for given RibOut.
//
BgpMembershipManager::Walker::RibOutState *
BgpMembershipManager::Walker::LocateRibOutState(RibOut *ribout) {
    RibOutStateMap::iterator loc = ribout_state_map_.find(ribout);
    if (loc == ribout_state_map_.end()) {
        RibOutState *ros = new RibOutState(ribout);
        ribout_state_map_.insert(make_pair(ribout, ros));
        ribout_state_list_.push_back(ros);
        return ros;
    } else {
        return loc->second;
    }
}

//
// Process table walk callback from DB infrastructure.
//
bool BgpMembershipManager::Walker::WalkCallback(DBTablePartBase *tpart,
    DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("db::DBTable");

    // Walk all RibOutStates and handle join/leave processing.
    for (RibOutStateList::iterator it = ribout_state_list_.begin();
         it != ribout_state_list_.end(); ++it) {
        RibOutState *ros = *it;
        RibOut *ribout = ros->ribout();
        ribout->bgp_export()->Join(tpart, ros->join_bitset(), db_entry);
        ribout->bgp_export()->Leave(tpart, ros->leave_bitset(), db_entry);
    }

    // Bail if there's no peers that need RibIn processing.
    if (peer_list_.empty())
        return true;

    // Walk through all eligible paths and notify the source peer if needed.
    bool notify = false;
    BgpRoute *route = static_cast<BgpRoute *>(db_entry);
    for (Route::PathList::iterator it = route->GetPathList().begin(), next = it;
         it != route->GetPathList().end(); it = next) {
        next++;

        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        IPeer *peer = path->GetPeer();

        // Skip resolved paths - PathResolver is responsible for them.
        if (path->IsResolved())
            continue;

        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(path))
            continue;

        // Skip if there's no walk requested for this IPeer.
        if (!peer || peer_list_.find(peer) == peer_list_.end())
            continue;

        notify |= peer->MembershipPathCallback(tpart, route, path);
    }

    rs_->table()->InputCommonPostProcess(tpart, route, notify);
    return true;
}

//
// Process table walk done callback from DB infrastructure.
// Just note that the walk has completed and trigger processing from the
// bgp::PeerMembership task.
//
void BgpMembershipManager::Walker::WalkDoneCallback(DBTableBase *table_base) {
    CHECK_CONCURRENCY("db::Walker");
    assert(rs_->table() == table_base);
    walk_completed_ = true;
    trigger_->Set();
}

//
// Start a walk for the BgpTable corresponding to the next RibState in the
// RibStateList.
//
void BgpMembershipManager::Walker::WalkStart() {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    assert(walk_ref_ == NULL);
    assert(!rs_);
    assert(peer_rib_list_.empty());
    assert(peer_list_.empty());
    assert(ribout_state_map_.empty());
    assert(ribout_state_list_.empty());
    assert(rib_state_list_.size() == rib_state_set_.size());

    // Bail if the list if empty.
    if (rib_state_list_.empty())
        return;

    // Get and remove the first RibState from the RibStateList.
    rs_ = rib_state_list_.front();
    rib_state_list_.pop_front();
    assert(rib_state_set_.erase(rs_) == 1);

    // Process all pending PeerRibStates for chosen RibState.
    // Insert the PeerRibStates into PeerRibList for post processing when
    // table walk is complete.
    for (RibState::iterator it = rs_->begin(); it != rs_->end(); ++it) {
        PeerRibState *prs = *it;
        peer_rib_list_.insert(prs);

        // Update PeerList for RIBIN actions and RibOutStateMap for RIBOUT
        // actions.
        switch (prs->action()) {
        case RIBOUT_ADD: {
            RibOutState *ros = LocateRibOutState(prs->ribout());
            ros->JoinPeer(prs->ribout_index());
            break;
        }
        case RIBIN_DELETE:
        case RIBIN_WALK: {
            IPeer *peer = prs->peer_state()->peer();
            peer_list_.insert(peer);
            break;
        }
        case RIBIN_WALK_RIBOUT_DELETE:
        case RIBIN_DELETE_RIBOUT_DELETE: {
            IPeer *peer = prs->peer_state()->peer();
            peer_list_.insert(peer);
            RibOutState *ros = LocateRibOutState(prs->ribout());
            ros->LeavePeer(prs->ribout_index());
            break;
        }
        default: {
            assert(false);
            break;
        }
        }
    }

    // Clear the pending PeerRibStates in the RibState.
    // This allows the RibState to accumulate new PeerRibStates for a future
    // walk of it's BgpTable.
    rs_->ClearPeerRibStateList();

    // Start the walk.
    rs_->increment_walk_count();
    BgpTable *table = rs_->table();
    walk_ref_ = table->AllocWalker(
        boost::bind(&BgpMembershipManager::Walker::WalkCallback, this, _1, _2),
        boost::bind(&BgpMembershipManager::Walker::WalkDoneCallback, this, _2));
    walk_started_ = true;
    if (!postpone_walk_)
        table->WalkTable(walk_ref_);
}

//
// Finish processing of the walk of BgpTable for current RibState.
//
// The walk complete notification is handled by WalkDoneCallback but all the
// book-keeping and triggering of Events is handled by this method since it
// needs to happen in bgp::PeerMembership task.
//
void BgpMembershipManager::Walker::WalkFinish() {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    assert(walk_ref_ != NULL);
    assert(rs_);
    assert(!peer_rib_list_.empty());
    assert(!peer_list_.empty() || !ribout_state_map_.empty());
    assert(rib_state_list_.size() == rib_state_set_.size());
    assert(ribout_state_list_.size() == ribout_state_map_.size());

    BgpTable *table = rs_->table();
    for (PeerRibList::iterator it = peer_rib_list_.begin();
         it != peer_rib_list_.end(); ++it) {
        PeerRibState *prs = *it;
        IPeer *peer = prs->peer_state()->peer();

        switch (prs->action()) {
        case RIBOUT_ADD:
            manager_->TriggerRegisterRibCompleteEvent(peer, table);
            break;
        case RIBIN_DELETE:
        case RIBIN_WALK:
            manager_->TriggerWalkRibCompleteEvent(peer, table);
            break;
        case RIBIN_WALK_RIBOUT_DELETE:
        case RIBIN_DELETE_RIBOUT_DELETE:
            manager_->TriggerUnregisterRibCompleteEvent(peer, table);
            break;
        default:
            assert(false);
            break;
        }
    }

    table->ReleaseWalker(walk_ref_);
    rs_ = NULL;
    peer_rib_list_.clear();
    peer_list_.clear();
    ribout_state_list_.clear();
    STLDeleteElements(&ribout_state_map_);

    walk_started_ = false;
    walk_completed_ = false;
}

//
// Handler for TaskTrigger.
// Start a new walk or finish processing for the current walk and start a new
// one.
//
bool BgpMembershipManager::Walker::WalkTrigger() {
    CHECK_CONCURRENCY("bgp::PeerMembership");

    if (!walk_started_) {
        assert(!walk_completed_);
        WalkStart();
    } else if (walk_completed_) {
        WalkFinish();
        WalkStart();
    }
    return true;
}

//
// Disable the TaskTrigger so that the Walker can accumulate RibStates in the
// RibStateList.
// Testing only.
//
void BgpMembershipManager::Walker::SetQueueDisable(bool value) {
    if (value) {
        trigger_->set_disable();
    } else {
        trigger_->set_enable();
    }
}

//
// Force the Walker to trigger walks that are postponed.
// Testing only.
//
void BgpMembershipManager::Walker::PostponeWalk() {
    assert(!walk_started_);
    assert(walk_ref_ == NULL);
    postpone_walk_ = true;
}

//
// Tell the DBTableWalkMgr to resume walk that was postponed previously.
// Testing only.
//
void BgpMembershipManager::Walker::ResumeWalk() {
    assert(walk_started_);
    assert(!walk_completed_);
    assert(walk_ref_ != NULL);
    postpone_walk_ = false;
    BgpTable *table = rs_->table();
    table->WalkTable(walk_ref_);
}
