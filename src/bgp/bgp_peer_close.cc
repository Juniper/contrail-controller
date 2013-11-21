/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_peer_close.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <tbb/recursive_mutex.h>

#include "bgp/bgp_export.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_update_queue.h"
#include "db/db.h"

using boost::system::error_code;
using namespace std;

// PeerCloseManager constructor
//
// Create an instance of PeerCloseManager with back reference to the parent
// IPeer
//
PeerCloseManager::PeerCloseManager(IPeer *peer) :
        peer_(peer),
        close_in_progress_(false),
        close_request_pending_(false),
        config_deleted_(false),
        stale_timer_(NULL),
        stale_timer_running_(false),
        start_stale_timer_(false) {

    if (peer->server()) {
        stale_timer_ = TimerManager::CreateTimer(*peer->server()->ioservice(),
                                                 "Graceful Restart StaleTimer");
    }
}

PeerCloseManager::~PeerCloseManager() {
    TimerManager::DeleteTimer(stale_timer_);
}

// StartStaleTimer
//
// Process RibIn staling related activities during peer closure
//
// Return true if at least ome time is started, false otherwise
//
void PeerCloseManager::StartStaleTimer() {

    //
    // Launch a timer to flush either the peer or the stale routes
    // TODO: Use timer value from configuration
    //
    stale_timer_->Start(PeerCloseManager::kDefaultGracefulRestartTime * 1000,
        boost::bind(&PeerCloseManager::StaleTimerCallback, this));
}

// GetCloseTypeForTimerCallback
//
// Concurrency: Runs in the context of the BGP peer rib membership task.
//
// Callback provided to bgp peer rib membership manager to indicate the action
// to perform during RibIn close
//
int PeerCloseManager::GetCloseTypeForTimerCallback(IPeerRib *peer_rib) {

    //
    // If peer_rib is still stale, the peer did not come back up or did not
    // register for this table after coming back up. In either case, delete
    // the rib in
    //
    if (peer_rib->IsStale()) {
        return MembershipRequest::RIBIN_DELETE;
    }

    //
    // Peer has come back up and registered with this table again. Sweep all
    // the stale paths and remove those that did not reappear in the new session
    //
    return MembershipRequest::RIBIN_SWEEP;
}

// SweepComplete
//
// Concurrency: Runs in the context of the BGP peer rib membership task.
//
// Callback called from membership manager indicating that RibIn sweep process
// for a table is complete. We don't have do any thing other than logging a 
// debug message here
//
void PeerCloseManager::SweepComplete(IPeer *ipeer, BgpTable *table) {
}

// StaleTimerCallback
//
// Route stale timer callback. If the peer has come back up, sweep routes for
// those address families that are still active. Delete the rest
//
bool PeerCloseManager::StaleTimerCallback() {

    // Protect this method from possible parallel new close request
    tbb::recursive_mutex::scoped_lock lock(mutex_);

    // If the peer is back up and this address family is still supported,
    // sweep old paths which may not have come back in the new session
    if (peer_->IsReady()) {
        peer_->server()->membership_mgr()->UnregisterPeer(peer_, 
            boost::bind(&PeerCloseManager::GetCloseTypeForTimerCallback, this,
                        _1),
            boost::bind(&PeerCloseManager::SweepComplete, this, _1, _2));
    } else {
        peer_->server()->membership_mgr()->UnregisterPeer(peer_, 
            boost::bind(&PeerCloseManager::GetCloseTypeForTimerCallback, this,
                        _1),
            boost::bind(&PeerCloseManager::CloseComplete, this, _1, _2, true,
                        false));
    }

    // Timer callback is complete. Reset the appropriate flags
    stale_timer_running_ = false;
    start_stale_timer_ = false;
    error_code ec;
    stale_timer_->Cancel();

    return false;
}

bool PeerCloseManager::IsCloseInProgress() {
    tbb::recursive_mutex::scoped_lock lock(mutex_);

    return close_in_progress_;
}

// CloseComplete
//
// Concurrency: Runs in the context of the BGP peer rib membership task.
//
// Close process for this peer in terms of walking RibIns and RibOuts are
// complete. Do the final cleanups necessary and notify interested party
//
void PeerCloseManager::CloseComplete(IPeer *ipeer, BgpTable *table,
                                     bool from_timer, bool gr_cancelled) {
    tbb::recursive_mutex::scoped_lock lock(mutex_);

    BGP_LOG_PEER(peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                 BGP_PEER_DIR_NA, "Close process is complete");

    close_in_progress_ = false;
    bool close_request_pending = close_request_pending_;
    bool is_xmpp = ipeer->IsXmppPeer();


    // Do any peer specific close actions
    IPeerClose *peer_close = peer_->peer_close();
    if (!peer_close->CloseComplete(from_timer, gr_cancelled)) {
        if (start_stale_timer_) {

            // If any stale timer has to be launched, then to wait for some
            // time hoping for the peer (and the paths) to come back up
            StartStaleTimer();
            stale_timer_running_ = true;
        }
        return;
    }

    //
    // Peer is deleted. But it is possible that delete request came while
    // we were in the midst of cleaning up. Restart close process again
    // if required. Xmpp peers are not created and deleted off configuration
    //
    if (close_request_pending && !is_xmpp) {

        close_request_pending_ = false;

        //
        // New close request was posted in the midst of previous close.
        // Post a close again, as this peer has been deleted.
        //
        Close();
    }
}

// GetActionAtStart
//
// Get the type of RibIn close action at start (Not during graceful restart
// timer callback, where in we walk the Rib again to sweep the routes)
int PeerCloseManager::GetActionAtStart(IPeerRib *peer_rib) {
    int action = MembershipRequest::INVALID;

    if (peer_rib->IsRibOutRegistered()) {
        action |= static_cast<int>(MembershipRequest::RIBOUT_DELETE);
    }

    // If graceful restart timer is already running, then this is a second
    // close before previous restart has completed. Abort graceful restart
    // and delete the routes instead
    if (stale_timer_running_) {
        action |= static_cast<int>(MembershipRequest::RIBIN_DELETE);
        stale_timer_running_ = false;
        return action;
    }

    //
    // Check if the close is graceful or or not. If the peer is deleted,
    // no need to retain the ribin
    //
    if (peer_rib->IsRibInRegistered()) {
        if (peer_->peer_close()->IsCloseGraceful()) {
            action |= MembershipRequest::RIBIN_STALE;
            peer_rib->SetStale();

            //
            // Note down that a timer must be started after this close process
            // is complete
            //
            start_stale_timer_ = true;
        } else {
            action |= MembershipRequest::RIBIN_DELETE;
        }
    }
    return (action);
}

// Close
//
// Delete all Ribs of this peer. To be called during peer close process of
// both BgpPeer ad XmppPeers
void PeerCloseManager::Close() {
    tbb::recursive_mutex::scoped_lock lock(mutex_);

    // Call IPeer specific close()
    IPeerClose *peer_close = peer_->peer_close();

    // If the close is already in progress, ignore this duplicate request
    if (close_in_progress_) {
        if (peer_close->IsCloseGraceful()) {
            close_request_pending_ = true;
        }
        BGP_LOG_PEER(peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_NA, "Close process is already in progress");
        return;
    } else {
        BGP_LOG_PEER(peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                    BGP_PEER_DIR_NA, "Initiating close process");
    }

    close_in_progress_ = true;

    peer_close->CustomClose();

    bool gr_cancelled = false;

    // If stale timer is already running, cancel the timer and do hard close
    if (stale_timer_running_) {
        error_code ec;
        stale_timer_->Cancel();
        gr_cancelled = true;
    }

    // Start process to delete this peer's RibIns and RibOuts. Peer can be
    // deleted only after these (asynchronous) activities are complete
    peer_->server()->membership_mgr()->UnregisterPeer(peer_, 
        boost::bind(&PeerCloseManager::GetActionAtStart, this, _1),
        boost::bind(&PeerCloseManager::CloseComplete, this, _1, _2, false,
                    gr_cancelled));
}

// For graceful-restart, we take mark-and-sweep approach instead of directly
// deleting the paths. In the first walk, local-preference is lowered so that
// the paths are least preferred and they are marked stale. After some time, if
// the peer session does not come back up, we delete all the paths and the peer
// itself. If the session did come back up, we flush only those paths that were
// not learned again in the new session.

// ProcessRibIn
//
// Concurrency: Runs in the context of the DB Walker task launched by 
// peer rib membership manager
//
// DBWalker callback routine for each of the RibIn prefix.
//
void PeerCloseManager::ProcessRibIn(DBTablePartBase *root, BgpRoute *rt,
                                    BgpTable *table, int action_mask) {
    DBRequest::DBOperation oper;
    BgpAttrPtr attrs;
    MembershipRequest::Action  action;

    // Look for the flags that we care about
    action = static_cast<MembershipRequest::Action>(action_mask &
                (MembershipRequest::RIBIN_STALE |
                 MembershipRequest::RIBIN_SWEEP |
                 MembershipRequest::RIBIN_DELETE));

    if (action == MembershipRequest::INVALID) return;

    // Process all paths sourced from this peer_. Multiple paths could exist
    // in ecmp cases.
    for (Route::PathList::iterator it = rt->GetPathList().begin(), next = it;
         it != rt->GetPathList().end(); it = next) {
        next++;

        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) continue;
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetPeer() != peer_) continue;

        switch (action) {
            case MembershipRequest::RIBIN_SWEEP:

                // Stale paths must be deleted
                if (!path->IsStale()) {
                    return;
                }

                // Fall through to delete case as the path is still stale
                // and we are sweeping such paths from the table
            case MembershipRequest::RIBIN_DELETE:

                // This path must be deleted. Hence attr is not required
                oper = DBRequest::DB_ENTRY_DELETE;
                attrs = NULL;
                break;

            case MembershipRequest::RIBIN_STALE:

                // This path must be marked for staling. Update the local
                // preference and update the route accordingly
                oper = DBRequest::DB_ENTRY_ADD_CHANGE;

                // Update attrs with maximum local preference so that this path
                // is least preferred
                // TODO: Check for the right local-pref value to use
                attrs = peer_->server()->attr_db()->\
                        ReplaceLocalPreferenceAndLocate(path->GetAttr(), 1);
                path->SetStale();
                break;

            default:
                return;
        }

        // Feed the route modify/delete request to the table input process
        table->InputCommon(root, rt, path, peer_, NULL, oper, attrs,
                        path->GetPathId(), path->GetFlags(), path->GetLabel());
    }

    return;
}

