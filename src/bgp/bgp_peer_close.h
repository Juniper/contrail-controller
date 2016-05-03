/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_CLOSE_H_
#define SRC_BGP_BGP_PEER_CLOSE_H_

#include "base/timer.h"
#include "base/util.h"
#include "base/queue_task.h"
#include "db/db_table_walker.h"
#include "bgp/ipeer.h"

class IPeerRib;
class BgpNeighborResp;
class BgpRoute;
class BgpTable;

// PeerCloseManager
//
// Manager close process of an IPeer (And hence should support both BgpPeers
// and XmppPeers)
//
// Among other things, RibIns and RibOuts of peers must be closed/deleted
// completely before a peer can be completely closed/deleted. This class
// provides this capability.
//
// RibIn and RibOut close are handled by invoking Unregister request with
// PeerRibMembershipManager class.
//
// Once RibIns and RibOuts are processed, notification callback function is
// invoked to signal the completion of close process
//
class PeerCloseManager {
public:
    enum State {
        BEGIN_STATE,
        NONE = BEGIN_STATE,
        STALE,
        GR_TIMER,
        LLGR_STALE,
        LLGR_TIMER,
        SWEEP,
        DELETE,
        END_STATE = DELETE,
    };

    // Use 5 minutes as the default GR timer expiry duration.
    static const int kDefaultGracefulRestartTimeSecs = 5 * 60;

    // Use 12 hours as the default LLGR timer expiry duration.
    static const int kDefaultLongLivedGracefulRestartTimeSecs = 12 * 60 * 60;

    explicit PeerCloseManager(IPeerClose *peer_close,
                              boost::asio::io_service &io_service);
    explicit PeerCloseManager(IPeerClose *peer_close);
    virtual ~PeerCloseManager();

    bool IsCloseInProgress() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return state_ != NONE;
    }

    bool IsInGracefulRestartTimerWait() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return state_ == GR_TIMER || state_ == LLGR_TIMER;
    }

    State state() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return state_;
    }

    void set_state(State state) { state_ = state; }
    void Close(bool non_graceful);
    void ProcessEORMarkerReceived(Address::Family family);

    bool RestartTimerCallback();
    void UnregisterPeerComplete(IPeer *ipeer, BgpTable *table);
    void FillCloseInfo(BgpNeighborResp *resp) const;

    struct Stats {
        Stats() { memset(this, 0, sizeof(Stats)); }

        uint64_t init;
        uint64_t close;
        uint64_t nested;
        uint64_t deletes;
        uint64_t stale;
        uint64_t llgr_stale;
        uint64_t sweep;
        uint64_t gr_timer;
        uint64_t llgr_timer;
    };
    const Stats &stats() const { return stats_; }

private:
    friend class PeerCloseManagerTest;

    void StartRestartTimer(int time);
    void ProcessClosure();
    void CloseComplete();
    bool ProcessSweepStateActions();
    void TriggerSweepStateActions();
    const std::string GetStateName(State state) const;
    void CloseInternal();

    IPeerClose *peer_close_;
    Timer *stale_timer_;
    Timer *sweep_timer_;
    State state_;
    bool close_again_;
    bool non_graceful_;
    int gr_elapsed_;
    int llgr_elapsed_;
    IPeerClose::Families families_;
    Stats stats_;
    mutable tbb::mutex mutex_;
};

#endif  // SRC_BGP_BGP_PEER_CLOSE_H_
