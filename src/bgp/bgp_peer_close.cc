/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer_close.h"


#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "net/community_type.h"

#define PEER_CLOSE_MANAGER_LOG(msg) \
    BGP_LOG_PEER(Event, peer_, SandeshLevel::SYS_INFO, BGP_LOG_FLAG_ALL,       \
        BGP_PEER_DIR_NA, "PeerCloseManager: State " << GetStateName(state_) << \
        ", CloseAgain? " << (close_again_ ? "Yes" : "No") << ": " << msg);

#define MOVE_TO_STATE(state)                                                   \
    do {                                                                       \
        assert(state_ != state);                                               \
        PEER_CLOSE_MANAGER_LOG("Move to state " << GetStateName(state));       \
        state_ = state;                                                        \
    } while (false)

// Create an instance of PeerCloseManager with back reference to parent IPeer
PeerCloseManager::PeerCloseManager(IPeer *peer) :
        peer_(peer), stale_timer_(NULL), sweep_timer_(NULL), state_(NONE),
        close_again_(false), gr_elapsed_(0), llgr_elapsed_(0) {
    stats_.init++;
    if (peer->server()) {
        stale_timer_ = TimerManager::CreateTimer(*peer->server()->ioservice(),
            "Graceful Restart StaleTimer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0);
        sweep_timer_ = TimerManager::CreateTimer(*peer->server()->ioservice(),
            "Graceful Restart SweepTimer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0);
    }
}

PeerCloseManager::~PeerCloseManager() {
    TimerManager::DeleteTimer(stale_timer_);
    TimerManager::DeleteTimer(sweep_timer_);
}

const std::string PeerCloseManager::GetStateName(State state) const {
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

// Trigger closure of an IPeer
//
// Graceful                                 close_state_: NONE
// RibIn Stale Marking and Ribout deletion  close_state_: STALE
// StateMachine restart and GR timer start  close_state_: GR_TIMER
//
// Peer IsReady() in GR timer callback (or via reception of all EoRs)
// RibIn Sweep and Ribout Generation        close_state_: SWEEP
//   UnregisterPeerComplete                 close_state_: NONE
//
// Peer not IsReady() in GR timer callback
// If LLGR supported                     close_state_: LLGR_STALE
//   RibIn Stale marking with LLGR_STALE community close_state_: LLGR_TIMER
//
//     Peer not IsReady() in LLGR timer callback
//       RibIn Delete                           close_state_: DELETE
//       UnregisterPeerComplete                 close_state_: NONE
//
//     Peer IsReady() in LLGR timer callback (or via reception of all EoRs)
//     RibIn Sweep                              close_state_: SWEEP
//       UnregisterPeerComplete                 close_state_: NONE
//
// If LLGR is not supported
//     RibIn Delete                           close_state_: DELETE
//     UnregisterPeerComplete                 close_state_: NONE
//
// Close() call during any state other than NONE and DELETE
//     Cancel GR timer and restart GR Closure all over again
//
// NonGraceful                              close_state_ = * (except DELETE)
// A. RibIn deletion and Ribout deletion    close_state_ = DELETE
// B. UnregisterPeerComplete => Peers delete/StateMachine restart
//                                          close_state_ = NONE
//
// TODO: If Close is restarted, account for elapsed time and skip GR (jump
//       directly into LLGR) as apropraite
void PeerCloseManager::Close() {
    tbb::mutex::scoped_lock lock(mutex_);
    CloseInternal();
}

void PeerCloseManager::CloseInternal() {
    stats_.close++;

    // Ignore nested closures
    if (close_again_) {
        stats_.nested++;
        PEER_CLOSE_MANAGER_LOG("Nested close calls ignored");
        return;
    }

    switch (state_) {
    case NONE:
        ProcessClosure();
        break;

    case GR_TIMER:
        PEER_CLOSE_MANAGER_LOG("Nested close: Restart GR");
        close_again_ = true;
        gr_elapsed_ += stale_timer_->GetElapsedTime();
        CloseComplete();
        break;

    case LLGR_TIMER:
        PEER_CLOSE_MANAGER_LOG("Nested close: Restart LLGR");
        close_again_ = true;
        llgr_elapsed_ += stale_timer_->GetElapsedTime();
        CloseComplete();
        break;

    case STALE:
    case LLGR_STALE:
    case SWEEP:
    case DELETE:
        PEER_CLOSE_MANAGER_LOG("Nested close");
        close_again_ = true;
        break;
    }
}

void PeerCloseManager::ProcessEORMarkerReceived(Address::Family family) {
    tbb::mutex::scoped_lock lock(mutex_);
    if ((state_ == GR_TIMER || state_ == LLGR_TIMER) && !families_.empty()) {
        if (family == Address::UNSPEC) {
            families_.clear();
        } else {
            families_.erase(family);
        }
        if (families_.empty())
            StartRestartTimer(0);
    }
}

// Process RibIn staling related activities during peer closure
// Return true if at least ome time is started, false otherwise
void PeerCloseManager::StartRestartTimer(int time) {
    stale_timer_->Cancel();
    PEER_CLOSE_MANAGER_LOG("GR Timer started to fire after " << time <<
                           " seconds");
    stale_timer_->Start(time,
        boost::bind(&PeerCloseManager::RestartTimerCallback, this));
}

bool PeerCloseManager::RestartTimerCallback() {
    tbb::mutex::scoped_lock lock(mutex_);

    PEER_CLOSE_MANAGER_LOG("GR Timer callback started");
    if (state_ == GR_TIMER || state_ == LLGR_TIMER)
        ProcessClosure();
    return false;
}

// Route stale timer callback. If the peer has come back up, sweep routes for
// those address families that are still active. Delete the rest
void PeerCloseManager::ProcessClosure() {

    // If the peer is back up and this address family is still supported,
    // sweep old paths which may not have come back in the new session
    switch (state_) {
        case NONE:
            if (!peer_->peer_close()->IsCloseGraceful()) {
                MOVE_TO_STATE(DELETE);
                stats_.deletes++;
            } else {
                MOVE_TO_STATE(STALE);
                stats_.stale++;
                peer_->peer_close()->GracefulRestartStale();
            }
            break;
        case GR_TIMER:
            if (peer_->IsReady()) {
                MOVE_TO_STATE(SWEEP);
                stats_.sweep++;
                break;
            }
            if (peer_->peer_close()->IsCloseLongLivedGraceful()) {
                MOVE_TO_STATE(LLGR_STALE);
                stats_.llgr_stale++;
                break;
            }
            MOVE_TO_STATE(DELETE);
            stats_.deletes++;
            break;

        case LLGR_TIMER:
            if (peer_->IsReady()) {
                MOVE_TO_STATE(SWEEP);
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
        peer_->peer_close()->CustomClose();
    peer_->server()->membership_mgr()->UnregisterPeer(peer_,
        boost::bind(&PeerCloseManager::GetCloseAction, this, _1, state_),
        boost::bind(&PeerCloseManager::UnregisterPeerComplete, this, _1, _2));
}

bool PeerCloseManager::IsCloseInProgress() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return state_ != NONE;
}

bool PeerCloseManager::IsInGracefulRestartTimerWait() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return state_ == GR_TIMER || state_ == LLGR_TIMER;
}

void PeerCloseManager::CloseComplete() {
    MOVE_TO_STATE(NONE);
    stale_timer_->Cancel();
    sweep_timer_->Cancel();
    families_.clear();
    stats_.init++;


    // Nested closures trigger fresh GR
    if (close_again_) {
        close_again_ = false;
        CloseInternal();
    }
}

bool PeerCloseManager::ProcessSweepStateActions() {
    assert(state_ == SWEEP);

    // Notify clients to trigger sweep as appropriate.
    peer_->peer_close()->GracefulRestartSweep();
    gr_elapsed_ = 0;
    llgr_elapsed_ = 0;
    CloseComplete();
    return false;
}

void PeerCloseManager::TriggerSweepStateActions() {
    PEER_CLOSE_MANAGER_LOG("Sweep Timer started to fire right away");
    sweep_timer_->Cancel();
    sweep_timer_->Start(0,
        boost::bind(&PeerCloseManager::ProcessSweepStateActions, this));
}

// Concurrency: Runs in the context of the BGP peer rib membership task.
//
// Close process for this peer in terms of walking RibIns and RibOuts are
// complete. Do the final cleanups necessary and notify interested party
void PeerCloseManager::UnregisterPeerComplete(IPeer *ipeer, BgpTable *table) {
    tbb::mutex::scoped_lock lock(mutex_);

    assert(state_ == STALE || LLGR_STALE || state_ == SWEEP ||
           state_ == DELETE);
    PEER_CLOSE_MANAGER_LOG("RibWalk completed");

    if (state_ == DELETE) {
        peer_->peer_close()->Delete();
        gr_elapsed_ = 0;
        llgr_elapsed_ = 0;
        MOVE_TO_STATE(NONE);
        stats_.init++;
        close_again_ = false;
        return;
    }

    // If any GR stale timer has to be launched, then to wait for some time
    // hoping for the peer (and the paths) to come back up.
    if (state_ == STALE) {
        peer_->peer_close()->CloseComplete();
        MOVE_TO_STATE(GR_TIMER);
        peer_->peer_close()->GetGracefulRestartFamilies(&families_);

        // Offset restart time with elapsed time during nested closures.
        int time = peer_->peer_close()->GetGracefulRestartTime() * 1000;
        time -= gr_elapsed_;
        if (time < 0)
            time = 0;
        StartRestartTimer(time);
        stats_.gr_timer++;
        return;
    }

    // From LLGR_STALE state, switch to LLGR_TIMER state. Typically this would
    // be a very long timer, and we expect to receive EORs before this timer
    // expires.
    if (state_ == LLGR_STALE) {
        MOVE_TO_STATE(LLGR_TIMER);
        peer_->peer_close()->GetGracefulRestartFamilies(&families_);
        StartRestartTimer(1000 *
                peer_->peer_close()->GetLongLivedGracefulRestartTime());

        // Offset restart time with elapsed time during nested closures.
        int time = peer_->peer_close()->GetLongLivedGracefulRestartTime() *1000;
        time -= llgr_elapsed_;
        if (time < 0)
            time = 0;
        StartRestartTimer(time);
        stats_.llgr_timer++;
        return;
    }

    TriggerSweepStateActions();
}

// Get the type of RibIn close action at start (Not during graceful restart
// timer callback, where in we walk the Rib again to sweep the routes)
int PeerCloseManager::GetCloseAction(IPeerRib *peer_rib, State state) {
    int action = MembershipRequest::INVALID;

    if ((state == STALE || state == LLGR_STALE || state == DELETE) &&
            peer_rib->IsRibOutRegistered())
        action |= static_cast<int>(MembershipRequest::RIBOUT_DELETE);

    if (!peer_rib->IsRibInRegistered())
        return action;

    // If graceful restart timer is already running, then this is a second
    // close before previous restart has completed. Abort graceful restart
    // and delete the routes instead
    switch (state) {
    case NONE:
        break;
    case STALE:
        action |= static_cast<int>(MembershipRequest::RIBIN_STALE);
        break;
    case LLGR_STALE:
        action |= static_cast<int>(MembershipRequest::RIBIN_LLGR_STALE);
        break;
    case GR_TIMER:
        break;
    case LLGR_TIMER:
        break;
    case SWEEP:
        action |= static_cast<int>(MembershipRequest::RIBIN_SWEEP);
        break;
    case DELETE:
        action |= static_cast<int>(MembershipRequest::RIBIN_DELETE);
        break;
    }
    return (action);
}

// For graceful-restart, we take mark-and-sweep approach instead of directly
// deleting the paths. In the first walk, local-preference is lowered so that
// the paths are least preferred and they are marked stale. After some time, if
// the peer session does not come back up, we delete all the paths and the peer
// itself. If the session did come back up, we flush only those paths that were
// not learned again in the new session.
//
// Concurrency: Runs in the context of the DB Walker task launched by peer rib
// membership manager
//
// DBWalker callback routine for each of the RibIn prefix.
void PeerCloseManager::ProcessRibIn(DBTablePartBase *root, BgpRoute *rt,
                                    BgpTable *table, int action_mask) {
    DBRequest::DBOperation oper;
    BgpAttrPtr attrs;
    MembershipRequest::Action  action;

    // Look for the flags that we care about
    action = static_cast<MembershipRequest::Action>(action_mask &
                (MembershipRequest::RIBIN_STALE |
                 MembershipRequest::RIBIN_LLGR_STALE |
                 MembershipRequest::RIBIN_SWEEP |
                 MembershipRequest::RIBIN_DELETE));

    if (action == MembershipRequest::INVALID)
        return;

    bool notify_rt = false;

    // Process all paths sourced from this peer_. Multiple paths could exist
    // in ecmp cases.
    for (Route::PathList::iterator it = rt->GetPathList().begin(), next = it;
         it != rt->GetPathList().end(); it = next) {
        next++;

        BgpPath *path = static_cast<BgpPath *>(it.operator->());

        // Skip paths from other peers.
        if (path->GetPeer() != peer_)
            continue;

        // Skip resolved paths - PathResolver is responsible for them.
        if (path->IsResolved())
            continue;

        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(path))
            continue;

        uint32_t stale = 0;
        switch (action) {
            case MembershipRequest::RIBIN_SWEEP:

                // Stale paths must be deleted.
                if (!path->IsStale() && !path->IsLlgrStale())
                    return;
                path->ResetStale();
                path->ResetLlgrStale();
                stats_.deleted_state_paths++;
                oper = DBRequest::DB_ENTRY_DELETE;
                attrs = NULL;
                break;

            case MembershipRequest::RIBIN_DELETE:

                // This path must be deleted. Hence attr is not required.
                stats_.deleted_paths++;
                oper = DBRequest::DB_ENTRY_DELETE;
                attrs = NULL;
                break;

            case MembershipRequest::RIBIN_STALE:

                // If path is already marked as stale, then there is no need to
                // process again. This can happen if the session flips while in
                // GR_TIMER state.
                if (path->IsStale())
                    continue;

                // This path must be marked for staling. Update the local
                // preference and update the route accordingly.
                oper = DBRequest::DB_ENTRY_ADD_CHANGE;
                stats_.marked_stale_paths++;
                attrs = path->GetAttr();
                stale = BgpPath::Stale;
                break;

            case MembershipRequest::RIBIN_LLGR_STALE:

                // If the path has NO_LLGR community, DELETE it.
                if (path->GetAttr()->community() &&
                    path->GetAttr()->community()->ContainsValue(
                        CommunityType::NoLlgr)) {
                    stats_.deleted_paths++;
                    oper = DBRequest::DB_ENTRY_DELETE;
                    attrs = NULL;
                    break;
                }

                // If path is already marked as llgr_stale, then there is no
                // need to process again. This can happen if the session flips
                // while in LLGR_TIMER state.
                if (path->IsLlgrStale())
                    continue;

                attrs = path->GetAttr();
                stale = BgpPath::LlgrStale;
                oper = DBRequest::DB_ENTRY_ADD_CHANGE;
                stats_.marked_llgr_stale_paths++;
                break;

            default:
                assert(false);
                break;
        }

        // Feed the route modify/delete request to the table input process.
        notify_rt |= table->InputCommon(root, rt, path, peer_, NULL, oper,
                                        attrs, path->GetPathId(),
                                        path->GetFlags() | stale,
                                        path->GetLabel());
    }

    table->InputCommonPostProcess(root, rt, notify_rt);
}

void PeerCloseManager::FillCloseInfo(BgpNeighborResp *resp) {
    tbb::mutex::scoped_lock lock(mutex_);

    PeerCloseInfo peer_close_info;
    peer_close_info.state = GetStateName(state_);
    peer_close_info.close_again = close_again_;
    peer_close_info.init = stats_.init;
    peer_close_info.close = stats_.close;
    peer_close_info.nested = stats_.nested;
    peer_close_info.deletes = stats_.deletes;
    peer_close_info.stale = stats_.stale;
    peer_close_info.sweep = stats_.sweep;
    peer_close_info.gr_timer = stats_.gr_timer;
    peer_close_info.deleted_state_paths = stats_.deleted_state_paths;
    peer_close_info.deleted_paths = stats_.deleted_paths;
    peer_close_info.marked_stale_paths = stats_.marked_stale_paths;
    peer_close_info.marked_llgr_stale_paths = stats_.marked_llgr_stale_paths;

    resp->set_peer_close_info(peer_close_info);
}
