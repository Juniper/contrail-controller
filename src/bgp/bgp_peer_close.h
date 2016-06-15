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

class BgpMembershipManager;
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
// BgpMembershipManager class.
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

    enum MembershipState {
        MEMBERSHIP_NONE,
        MEMBERSHIP_IN_USE,
        MEMBERSHIP_IN_WAIT
    };

    explicit PeerCloseManager(IPeerClose *peer_close,
                              boost::asio::io_service &io_service);
    explicit PeerCloseManager(IPeerClose *peer_close);
    virtual ~PeerCloseManager();

    MembershipState membership_state() const { return membership_state_; }
    void set_membership_state(MembershipState state) {
        membership_state_ = state;
    }

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
    void MembershipRequest();

    bool RestartTimerCallback();
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
    bool MembershipRequestCallback();
    bool MembershipPathCallback(DBTablePartBase *root, BgpRoute *rt,
                                BgpPath *path);
    IPeerClose *peer_close() const { return peer_close_; }
    bool close_again() const { return close_again_; }
    IPeerClose::Families *families() { return &families_; }
    void set_membership_req_pending(int count) {
        membership_req_pending_ = count;
    }

protected:
    tbb::atomic<int> membership_req_pending_;

private:
    friend class PeerCloseManagerTest;

    virtual void StartRestartTimer(int time);
    void ProcessClosure();
    void CloseComplete();
    bool ProcessSweepStateActions();
    virtual void TriggerSweepStateActions();
    std::string GetStateName(State state) const;
    std::string GetMembershipStateName(MembershipState state) const;
    void CloseInternal();
    bool MembershipRequestCompleteCallbackInternal();
    void MembershipRequestInternal();
    virtual bool CanUseMembershipManager() const;
    virtual BgpMembershipManager *membership_mgr() const;
    virtual bool GRTimerFired() const;
    virtual void StaleNotify();
    bool NotifyStaleEvent();

    IPeerClose *peer_close_;
    Timer *stale_timer_;
    Timer *sweep_timer_;
    Timer *stale_notify_timer_;
    State state_;
    bool close_again_;
    bool non_graceful_;
    int gr_elapsed_;
    int llgr_elapsed_;
    MembershipState membership_state_;
    IPeerClose::Families families_;
    Stats stats_;
    mutable tbb::mutex mutex_;
};

#endif  // SRC_BGP_BGP_PEER_CLOSE_H_
