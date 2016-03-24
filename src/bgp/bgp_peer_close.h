/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_CLOSE_H_
#define SRC_BGP_BGP_PEER_CLOSE_H_

#include <tbb/recursive_mutex.h>

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
    enum State { NONE, STALE, GR_TIMER, SWEEP, DELETE };

    static const int kDefaultGracefulRestartTimeMsecs = 60*1000;

    // thread: bgp::StateMachine
    explicit PeerCloseManager(IPeer *peer);
    virtual ~PeerCloseManager();

    IPeer *peer() { return peer_; }

    void Close();
    bool RestartTimerCallback();
    void UnregisterPeerComplete(IPeer *ipeer, BgpTable *table);
    int GetCloseAction(IPeerRib *peer_rib, State state);
    void ProcessRibIn(DBTablePartBase *root, BgpRoute *rt, BgpTable *table,
                      int action_mask);
    bool IsCloseInProgress();
    void ProcessEORMarkerReceived(Address::Family family);
    void FillCloseInfo(BgpNeighborResp *resp);
    const State state() const { return state_; }

    struct Stats {
        Stats() { memset(this, 0, sizeof(Stats)); }

        uint64_t init;
        uint64_t close;
        uint64_t nested;
        uint64_t deletes;
        uint64_t stale;
        uint64_t sweep;
        uint64_t gr_timer;
        uint64_t deleted_state_paths;
        uint64_t deleted_paths;
        uint64_t marked_state_paths;
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

    IPeer *peer_;
    Timer *stale_timer_;
    Timer *sweep_timer_;
    State state_;
    bool close_again_;
    IPeerClose::Families families_;
    Stats stats_;
    tbb::recursive_mutex mutex_;
};

#endif  // SRC_BGP_BGP_PEER_CLOSE_H_
