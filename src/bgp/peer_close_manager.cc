/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/peer_close_manager.h"


#include <list>
#include <map>

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-instance/routing_instance.h"
#include "net/community_type.h"

#define PEER_CLOSE_MANAGER_LOG(msg) \
    BGP_LOG_PEER(Event, peer_close_->peer(), SandeshLevel::SYS_INFO,           \
        BGP_LOG_FLAG_ALL, BGP_PEER_DIR_NA,                                     \
        "PeerCloseManager: State " << GetStateName(state_) <<                  \
        ", MembershipState: " << GetMembershipStateName(membership_state_) <<  \
        ", MembershipReqPending: " << membership_req_pending_ <<               \
        ", CloseAgain?: " << (close_again_ ? "Yes" : "No") << ": " << msg);

#define PEER_CLOSE_MANAGER_TABLE_LOG(msg)                                      \
    BGP_LOG_PEER_TABLE(peer_close_->peer(), SandeshLevel::SYS_INFO,            \
        BGP_LOG_FLAG_ALL, table,                                               \
        "PeerCloseManager: State " << GetStateName(state_) <<                  \
        ", MembershipState: " << GetMembershipStateName(membership_state_) <<  \
        ", MembershipReqPending: " << membership_req_pending_ <<               \
        ", CloseAgain?: " << (close_again_ ? "Yes" : "No") << ": " << msg);

#define MOVE_TO_STATE(state)                                                   \
    do {                                                                       \
        assert(state_ != state);                                               \
        PEER_CLOSE_MANAGER_LOG("Move to state " << GetStateName(state));       \
        state_ = state;                                                        \
    } while (false)

// Create an instance of PeerCloseManager with back reference to parent IPeer
PeerCloseManager::PeerCloseManager(IPeerClose *peer_close,
                                   boost::asio::io_service *io_service) :
        peer_close_(peer_close), stale_timer_(NULL),
        event_queue_(new WorkQueue<Event *>(
                     TaskScheduler::GetInstance()->GetTaskId(
                         peer_close_->GetTaskName()),
                     peer_close_->GetTaskInstance(),
                     boost::bind(&PeerCloseManager::EventCallback, this, _1))),
        state_(NONE), close_again_(false), graceful_(true), gr_elapsed_(0),
        llgr_elapsed_(0), membership_state_(MEMBERSHIP_NONE) {
    stats_.init++;
    membership_req_pending_ = 0;
    stale_timer_ = TimerManager::CreateTimer(*io_service,
                                             "Graceful Restart StaleTimer");
}

// Create an instance of PeerCloseManager with back reference to parent IPeer
PeerCloseManager::PeerCloseManager(IPeerClose *peer_close) :
        peer_close_(peer_close), stale_timer_(NULL),
        event_queue_(new WorkQueue<Event *>(
                     TaskScheduler::GetInstance()->GetTaskId(
                         peer_close_->GetTaskName()),
                     peer_close_->GetTaskInstance(),
                     boost::bind(&PeerCloseManager::EventCallback, this, _1))),
        state_(NONE), close_again_(false), graceful_(true), gr_elapsed_(0),
        llgr_elapsed_(0), membership_state_(MEMBERSHIP_NONE) {
    stats_.init++;
    membership_req_pending_ = 0;
    if (peer_close->peer() && peer_close->peer()->server()) {
        stale_timer_ =
           TimerManager::CreateTimer(*peer_close->peer()->server()->ioservice(),
                                     "Graceful Restart StaleTimer");
    }
}

PeerCloseManager::~PeerCloseManager() {
    event_queue_->Shutdown();
    TimerManager::DeleteTimer(stale_timer_);
}

std::string PeerCloseManager::GetStateName(State state) const {
    switch (state) {
    case NONE:
        return "NONE";
    case GR_TIMER:
        return "GR_TIMER";
    case STALE:
        return "STALE";
    case LLGR_STALE:
        return "LLGR_STALE";
    case LLGR_TIMER:
        return "LLGR_TIMER";
    case SWEEP:
        return "SWEEP";
    case DELETE:
        return "DELETE";
    }
    assert(false);
    return "";
}

std::string PeerCloseManager::GetMembershipStateName(
        MembershipState state) const {
    switch (state) {
    case MEMBERSHIP_NONE:
        return "NONE";
    case MEMBERSHIP_IN_USE:
        return "IN_USE";
    case MEMBERSHIP_IN_WAIT:
        return "IN_WAIT";
    }
    assert(false);
    return "";
}

std::string PeerCloseManager::GetEventName(EventType eventType) const {
    switch (eventType) {
    case EVENT_NONE:
        return "NONE";
    case CLOSE:
        return "CLOSE";
    case EOR_RECEIVED:
        return "EOR_RECEIVED";
    case MEMBERSHIP_REQUEST:
        return "MEMBERSHIP_REQUEST";
    case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
        return "MEMBERSHIP_REQUEST_COMPLETE_CALLBACK";
    case TIMER_CALLBACK:
        return "TIMER_CALLBACK";
    }

    return "";
}

void PeerCloseManager::EnqueueEvent(Event *event) {
    PEER_CLOSE_MANAGER_LOG("Enqueued event " <<
            GetEventName(event->event_type) <<
            ", graceful " << event->graceful <<
            ", family " << Address::FamilyToString(event->family));
    event_queue_->Enqueue(event);
}

// Trigger closure of an IPeer
//
// Graceful                                 close_state_: NONE
// RibIn Stale Marking and Ribout deletion  close_state_: STALE
// StateMachine restart and GR timer start  close_state_: GR_TIMER
//
// Peer IsReady() in GR timer callback (or via reception of all EoRs)
// RibIn Sweep and Ribout Generation        close_state_: SWEEP
//   MembershipRequestCallback      close_state_: NONE
//
// Peer not IsReady() in GR timer callback
// If LLGR supported                     close_state_: LLGR_STALE
//   RibIn Stale marking with LLGR_STALE community close_state_: LLGR_TIMER
//
//     Peer not IsReady() in LLGR timer callback
//       RibIn Delete                       close_state_: DELETE
//       MembershipRequestCallback                 close_state_: NONE
//
//     Peer IsReady() in LLGR timer callback (or via reception of all EoRs)
//     RibIn Sweep                          close_state_: SWEEP
//       MembershipRequestCallback  close_state_: NONE
//
// If LLGR is not supported
//     RibIn Delete                         close_state_: DELETE
//     MembershipRequestCallback    close_state_: NONE
//
// Close() call during any state other than NONE and DELETE
//     Cancel GR timer and restart GR Closure all over again
//
// NonGraceful                              close_state_ = * (except DELETE)
// A. RibIn deletion and Ribout deletion    close_state_ = DELETE
// B. MembershipRequestCallback => Peers delete/StateMachine restart
//                                          close_state_ = NONE
//
// If Close is restarted, account for GR timer's elapsed time.
//
// Use graceful as true to close gracefully.
void PeerCloseManager::Close(bool graceful) {
    EnqueueEvent(new Event(CLOSE, graceful));
}

void PeerCloseManager::Close(Event *event) {
    // Note down non-graceful close trigger. Once non-graceful closure is
    // triggered, it should remain so until close process is complete. Further
    // graceful closure calls until then should remain non-graceful.
    graceful_ &= event->graceful;
    CloseInternal();
}

void PeerCloseManager::CloseInternal() {
    stats_.close++;

    // Ignore nested closures
    if (close_again_) {
        PEER_CLOSE_MANAGER_LOG("Nested close calls ignored");
        return;
    }

    switch (state_) {
    case NONE:
        stats_.ResetRouteStats();
        ProcessClosure();
        break;

    case GR_TIMER:
        PEER_CLOSE_MANAGER_LOG("Nested close: Restart GR");
        close_again_ = true;
        stats_.nested++;
        gr_elapsed_ += stale_timer_->GetElapsedTime();
        CloseComplete();
        break;

    case LLGR_TIMER:
        PEER_CLOSE_MANAGER_LOG("Nested close: Restart LLGR");
        close_again_ = true;
        stats_.nested++;
        llgr_elapsed_ += stale_timer_->GetElapsedTime();
        CloseComplete();
        break;

    case STALE:
    case LLGR_STALE:
    case SWEEP:
    case DELETE:
        PEER_CLOSE_MANAGER_LOG("Nested close");
        close_again_ = true;
        stats_.nested++;
        break;
    }
}

void PeerCloseManager::ProcessEORMarkerReceived(Address::Family family) {
    EnqueueEvent(new Event(EOR_RECEIVED, family));
}

void PeerCloseManager::ProcessEORMarkerReceived(Event *event) {
    if ((state_ == GR_TIMER || state_ == LLGR_TIMER) && !families_.empty()) {
        if (event->family == Address::UNSPEC) {
            families_.clear();
        } else {
            families_.erase(event->family);
        }

        // Start the timer if all EORs have been received.
        if (families_.empty())
            StartRestartTimer(0);
    }
}

// Process RibIn staling related activities during peer closure
// Return true if at least ome time is started, false otherwise
void PeerCloseManager::StartRestartTimer(int time) {
    stale_timer_->Cancel();
    PEER_CLOSE_MANAGER_LOG("GR Timer started to fire after " << time <<
                           " milliseconds");
    stale_timer_->Start(time,
        boost::bind(&PeerCloseManager::RestartTimerCallback, this));
}

bool PeerCloseManager::RestartTimerCallback() {
    CHECK_CONCURRENCY("timer::TimerTask");
    EnqueueEvent(new Event(TIMER_CALLBACK));
    return false;
}

void PeerCloseManager::RestartTimerCallback(Event *event) {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());

    PEER_CLOSE_MANAGER_LOG("GR Timer callback started");
    if (state_ != GR_TIMER && state_ != LLGR_TIMER)
        return;

    if (peer_close_->IsReady() && !families_.empty()) {
        // Fake reception of all EORs.
        for (IPeerClose::Families::iterator i = families_.begin(), next = i;
                i != families_.end(); i = next) {
            next++;
            PEER_CLOSE_MANAGER_LOG("Simulate EoR reception for family " << *i);
            peer_close_->ReceiveEndOfRIB(*i);
        }
    } else {
        ProcessClosure();
    }
}

// Route stale timer callback. If the peer has come back up, sweep routes for
// those address families that are still active. Delete the rest
void PeerCloseManager::ProcessClosure() {
    // If the peer is back up and this address family is still supported,
    // sweep old paths which may not have come back in the new session
    switch (state_) {
        case NONE:
            if (!graceful_ || !peer_close_->IsCloseGraceful()) {
                MOVE_TO_STATE(DELETE);
                stats_.deletes++;
            } else {
                MOVE_TO_STATE(STALE);
                stats_.stale++;
                StaleNotify();
                return;
            }
            break;
        case GR_TIMER:
            if (peer_close_->IsReady()) {
                MOVE_TO_STATE(SWEEP);
                gr_elapsed_ = 0;
                llgr_elapsed_ = 0;
                stats_.sweep++;
                break;
            }
            if (peer_close_->IsCloseLongLivedGraceful()) {
                MOVE_TO_STATE(LLGR_STALE);
                stats_.llgr_stale++;
                peer_close_->LongLivedGracefulRestartStale();
                break;
            }
            MOVE_TO_STATE(DELETE);
            stats_.deletes++;
            break;

        case LLGR_TIMER:
            if (peer_close_->IsReady()) {
                MOVE_TO_STATE(SWEEP);
                gr_elapsed_ = 0;
                llgr_elapsed_ = 0;
                stats_.sweep++;
                break;
            }
            MOVE_TO_STATE(DELETE);
            stats_.deletes++;
            break;

        case STALE:
        case LLGR_STALE:
        case SWEEP:
        case DELETE:
            assert(false);
            return;
    }

    if (state_ == DELETE)
        peer_close_->CustomClose();
    MembershipRequest();
}

void PeerCloseManager::CloseComplete() {
    MOVE_TO_STATE(NONE);
    stale_timer_->Cancel();
    families_.clear();
    stats_.init++;

    // Nested closures trigger fresh GR
    if (close_again_) {
        close_again_ = false;
        CloseInternal();
    }
}

bool PeerCloseManager::AssertSweepState(bool do_assert) {
    bool check = (state_ == SWEEP);
    if (do_assert)
        assert(check);
    return check;
}

bool PeerCloseManager::AssertMembershipManagerInUse(bool do_assert) {
    bool check = false;
    check |= (state_ == STALE || LLGR_STALE || state_ == SWEEP ||
              state_ == DELETE);
    check |= (membership_state_ == MEMBERSHIP_IN_USE);
    check |= (membership_req_pending_ > 0);
    if (do_assert)
        assert(check);
    return check;
}

bool PeerCloseManager::AssertMembershipState(bool do_assert) {
    bool check = (membership_state_ != MEMBERSHIP_IN_USE);
    if (do_assert)
        assert(check);
    return check;
}

bool PeerCloseManager::AssertMembershipReqCount(bool do_assert) {
    bool check = !membership_req_pending_;
    if (do_assert)
        assert(check);
    return check;
}

void PeerCloseManager::TriggerSweepStateActions() {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());
    if (!AssertSweepState())
        return;

    // Notify clients to trigger sweep as appropriate.
    peer_close_->GracefulRestartSweep();

    // Reset MembershipUse state after client has been notified above.
    set_membership_state(MEMBERSHIP_NONE);
    CloseComplete();
}

// Notify clients about entering Stale event.
void PeerCloseManager::StaleNotify() {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());

    peer_close_->GracefulRestartStale();
    if (!AssertMembershipState())
        return;
    MembershipRequest(NULL);
}

bool PeerCloseManager::CanUseMembershipManager() const {
    return peer_close_->peer()->CanUseMembershipManager();
}

void PeerCloseManager::GetRegisteredRibs(std::list<BgpTable *> *tables) {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    mgr->GetRegisteredRibs(peer_close_->peer(), tables);
}

bool PeerCloseManager::IsRegistered(BgpTable *table) const {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    return mgr->IsRegistered(peer_close_->peer(), table);
}

void PeerCloseManager::Unregister(BgpTable *table) {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    mgr->Unregister(peer_close_->peer(), table);
}

void PeerCloseManager::WalkRibIn(BgpTable *table) {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    mgr->WalkRibIn(peer_close_->peer(), table);
}

void PeerCloseManager::UnregisterRibOut(BgpTable *table) {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    mgr->UnregisterRibOut(peer_close_->peer(), table);
}

bool PeerCloseManager::IsRibInRegistered(BgpTable *table) const {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    return mgr->IsRibInRegistered(peer_close_->peer(), table);
}

void PeerCloseManager::UnregisterRibIn(BgpTable *table) {
    BgpMembershipManager *mgr = peer_close_->peer()->server()->membership_mgr();
    mgr->UnregisterRibIn(peer_close_->peer(), table);
}

void PeerCloseManager::MembershipRequest() {
    if (!AssertMembershipState())
        return;

    // Pause if membership manager is not ready for usage.
    if (!CanUseMembershipManager()) {
        set_membership_state(MEMBERSHIP_IN_WAIT);
        PEER_CLOSE_MANAGER_LOG("Wait for membership manager availability");
        return;
    }
    set_membership_state(MEMBERSHIP_IN_USE);
    EnqueueEvent(new Event(MEMBERSHIP_REQUEST));
}

void PeerCloseManager::MembershipRequest(Event *evnet) {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());

    set_membership_state(MEMBERSHIP_IN_USE);
    if (!AssertMembershipReqCount())
        return;
    membership_req_pending_++;
    std::list<BgpTable *> tables;
    GetRegisteredRibs(&tables);

    if (tables.empty()) {
        assert(MembershipRequestCallback(NULL));
        return;
    }

    // Account for extra increment above.
    membership_req_pending_--;
    BOOST_FOREACH(BgpTable *table, tables) {
        membership_req_pending_++;
        if (IsRegistered(table)) {
            if (state_ == PeerCloseManager::DELETE) {
                PEER_CLOSE_MANAGER_TABLE_LOG(
                    "MembershipManager::Unregister");
                Unregister(table);
            } else if (state_ == PeerCloseManager::SWEEP) {
                PEER_CLOSE_MANAGER_TABLE_LOG("MembershipManager::WalkRibIn");
                WalkRibIn(table);
            } else {
                PEER_CLOSE_MANAGER_TABLE_LOG(
                    "MembershipManager::UnregisterRibOut");
                UnregisterRibOut(table);
            }
        } else {
            assert(IsRibInRegistered(table));
            if (state_ == PeerCloseManager::DELETE) {
                PEER_CLOSE_MANAGER_TABLE_LOG(
                    "MembershipManager::UnregisterRibIn");
                UnregisterRibIn(table);
            } else {
                PEER_CLOSE_MANAGER_TABLE_LOG("MembershipManager::WalkRibIn");
                WalkRibIn(table);
            }
        }
    }
}

void PeerCloseManager::MembershipRequestCallback() {
    EnqueueEvent(new Event(MEMBERSHIP_REQUEST_COMPLETE_CALLBACK));
}

// Close process for this peer in terms of walking RibIns and RibOuts are
// complete. Do the final cleanups necessary and notify interested party
//
// Retrun true if we are done using membership manager, false otherwise.
bool PeerCloseManager::MembershipRequestCallback(Event *event) {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());

    bool result = false;
    PEER_CLOSE_MANAGER_LOG("MembershipRequestCallback");

    if (!AssertMembershipManagerInUse())
        return result;
    if (--membership_req_pending_)
        return result;

    // Indicate to the caller that we are done using the membership manager.
    result = true;

    if (state_ == DELETE) {
        MOVE_TO_STATE(NONE);
        peer_close_->Delete();
        gr_elapsed_ = 0;
        llgr_elapsed_ = 0;
        stats_.init++;
        close_again_ = false;
        graceful_ = true;
        set_membership_state(MEMBERSHIP_NONE);
        return result;
    }

    // Process nested closures.
    if (close_again_) {
        set_membership_state(MEMBERSHIP_NONE);
        CloseComplete();
        return result;
    }

    // If any GR stale timer has to be launched, then to wait for some time
    // hoping for the peer (and the paths) to come back up.
    if (state_ == STALE) {
        peer_close_->CloseComplete();
        MOVE_TO_STATE(GR_TIMER);
        peer_close_->GetGracefulRestartFamilies(&families_);

        // Offset restart time with elapsed time during nested closures.
        int time = peer_close_->GetGracefulRestartTime() * 1000;
        time -= gr_elapsed_;
        if (time < 0)
            time = 0;
        StartRestartTimer(time);
        stats_.gr_timer++;
        set_membership_state(MEMBERSHIP_NONE);
        return result;
    }

    // From LLGR_STALE state, switch to LLGR_TIMER state. Typically this would
    // be a very long timer, and we expect to receive EORs before this timer
    // expires.
    if (state_ == LLGR_STALE) {
        MOVE_TO_STATE(LLGR_TIMER);

        // Offset restart time with elapsed time during nested closures.
        int time = peer_close_->GetLongLivedGracefulRestartTime() * 1000;
        time -= llgr_elapsed_;
        if (time < 0)
            time = 0;
        StartRestartTimer(time);
        stats_.llgr_timer++;
        set_membership_state(MEMBERSHIP_NONE);
        return result;
    }

    TriggerSweepStateActions();
    return result;
}

void PeerCloseManager::FillRouteCloseInfo(PeerCloseInfo *close_info) const {
    std::map<std::string, PeerCloseRouteInfo> route_stats;

    for (int i = 0; i < Address::NUM_FAMILIES; i++) {
        if (!stats_.route_stats[i].IsSet())
            continue;
        PeerCloseRouteInfo route_info;
        route_info.set_staled(stats_.route_stats[i].staled);
        route_info.set_llgr_staled(stats_.route_stats[i].llgr_staled);
        route_info.set_refreshed(stats_.route_stats[i].refreshed);
        route_info.set_fresh(stats_.route_stats[i].fresh);
        route_info.set_deleted(stats_.route_stats[i].deleted);
        route_stats[Address::FamilyToString(static_cast<Address::Family>(i))] =
            route_info;
    }

    if (!route_stats.empty())
        close_info->set_route_stats(route_stats);
}

BgpNeighborResp *PeerCloseManager::FillCloseInfo(BgpNeighborResp *resp) const {
    PeerCloseInfo peer_close_info;
    peer_close_info.set_state(GetStateName(state_));
    peer_close_info.set_membership_state(
        GetMembershipStateName(membership_state_));
    peer_close_info.set_close_again(close_again_);
    peer_close_info.set_graceful(graceful_);
    peer_close_info.set_init(stats_.init);
    peer_close_info.set_close(stats_.close);
    peer_close_info.set_nested(stats_.nested);
    peer_close_info.set_deletes(stats_.deletes);
    peer_close_info.set_stale(stats_.stale);
    peer_close_info.set_llgr_stale(stats_.llgr_stale);
    peer_close_info.set_sweep(stats_.sweep);
    peer_close_info.set_gr_timer(stats_.gr_timer);
    peer_close_info.set_llgr_timer(stats_.llgr_timer);
    FillRouteCloseInfo(&peer_close_info);

    resp->set_peer_close_info(peer_close_info);

    return resp;
}

void PeerCloseManager::UpdateRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    if (state_ == NONE)
        return;

    if (!old_path)
        stats_.route_stats[family].fresh++;
    else if (old_path->IsStale() && !(path_flags & BgpPath::Stale))
        stats_.route_stats[family].refreshed++;
}

bool PeerCloseManager::MembershipPathCallback(DBTablePartBase *root,
                                              BgpRoute *rt, BgpPath *path) {
    CHECK_CONCURRENCY("db::DBTable");
    DBRequest::DBOperation oper;
    BgpAttrPtr attrs;

    BgpTable *table = static_cast<BgpTable *>(root->parent());
    assert(table);

    uint32_t stale = 0;

    switch (state_) {
        case NONE:
        case GR_TIMER:
        case LLGR_TIMER:
            return false;

        case SWEEP:

            // Stale paths must be deleted.
            if (!path->IsStale() && !path->IsLlgrStale())
                return false;
            path->ResetStale();
            path->ResetLlgrStale();
            oper = DBRequest::DB_ENTRY_DELETE;
            attrs = NULL;
            stats_.route_stats[table->family()].deleted++;
            break;

        case DELETE:

            // This path must be deleted. Hence attr is not required.
            oper = DBRequest::DB_ENTRY_DELETE;
            attrs = NULL;
            stats_.route_stats[table->family()].deleted++;
            break;

        case STALE:

            // We do not support GR for multicast routes (yet).
            if (table->family() == Address::ERMVPN) {
                oper = DBRequest::DB_ENTRY_DELETE;
                attrs = NULL;
                stats_.route_stats[table->family()].deleted++;
                break;
            }

            // If path is already marked as stale, then there is no need to
            // process again. This can happen if the session flips while in
            // GR_TIMER state.
            if (path->IsStale())
                return false;

            // This path must be marked for staling. Update the local
            // preference and update the route accordingly.
            oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            attrs = path->GetAttr();
            stale = BgpPath::Stale;
            stats_.route_stats[table->family()].staled++;
            break;

        case LLGR_STALE:

            // If the path has NO_LLGR community, DELETE it.
            if (path->GetAttr()->community() &&
                path->GetAttr()->community()->ContainsValue(
                    CommunityType::NoLlgr)) {
                oper = DBRequest::DB_ENTRY_DELETE;
                attrs = NULL;
                stats_.route_stats[table->family()].deleted++;
                break;
            }

            // If path is already marked as llgr_stale, then there is no
            // need to process again. This can happen if the session flips
            // while in LLGR_TIMER state.
            if (path->IsLlgrStale())
                return false;

            attrs = path->GetAttr();
            stale = BgpPath::LlgrStale;
            oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            stats_.route_stats[table->family()].llgr_staled++;
            break;
    }

    // Feed the route modify/delete request to the table input process.
    return table->InputCommon(root, rt, path, peer_close_->peer(), NULL, oper,
        attrs, path->GetPathId(), path->GetFlags() | stale, path->GetLabel(),
        path->GetL3Label());
}

//
// Handler for an Event.
//
bool PeerCloseManager::EventCallback(Event *event) {
    CHECK_CONCURRENCY(peer_close_->GetTaskName());
    bool result;

    switch (event->event_type) {
    case EVENT_NONE:
        break;
    case CLOSE:
        Close(event);
        break;
    case EOR_RECEIVED:
        ProcessEORMarkerReceived(event);
        break;
    case MEMBERSHIP_REQUEST:
        MembershipRequest(event);
        break;
    case MEMBERSHIP_REQUEST_COMPLETE_CALLBACK:
        result = MembershipRequestCallback(event);

        // Notify clients if we are no longer using the membership mgr.
        if (result)
            peer_close_->MembershipRequestCallbackComplete();
        break;
    case TIMER_CALLBACK:
        RestartTimerCallback(event);
        break;
    }

    delete event;
    return true;
}
