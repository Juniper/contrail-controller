/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_PEER_CLOSE_H__
#define __BGP_PEER_CLOSE_H__

#include <tbb/recursive_mutex.h>

#include "base/timer.h"
#include "base/util.h"
#include "base/queue_task.h"
#include "db/db_table_walker.h"
#include "bgp/ipeer.h"

class IPeerRib;
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
    static const int kDefaultGracefulRestartTime = 60; // Seconds

    // thread: bgp::StateMachine
    PeerCloseManager(IPeer *peer);
    virtual ~PeerCloseManager();

    IPeer *peer() { return peer_; }
    bool IsConfigDeleted() const { return config_deleted_; }
    void SetConfigDeleted(bool deleted) { config_deleted_ = deleted; }

    void Close();
    bool StaleTimerCallback();
    void CloseComplete(IPeer *ipeer, BgpTable *table, bool from_timer,
                       bool gr_cancelled);
    void SweepComplete(IPeer *ipeer, BgpTable *table);
    int GetCloseTypeForTimerCallback(IPeerRib *peer_rib);
    int GetActionAtStart(IPeerRib *peer_rib);
    void ProcessRibIn(DBTablePartBase *root, BgpRoute *rt, BgpTable *table,
                      int action_mask);
    bool IsCloseInProgress();

private:
    friend class PeerCloseManagerTest;

    virtual void StartStaleTimer();

    IPeer *peer_;
    bool close_in_progress_;
    bool close_request_pending_;
    bool config_deleted_;
    Timer *stale_timer_;
    bool stale_timer_running_;
    bool start_stale_timer_;
    tbb::recursive_mutex mutex_;
};

#endif // __BGP_PEER_CLOSE_H__
