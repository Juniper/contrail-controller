/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PEER_CLOSE_H_
#define SRC_BGP_BGP_PEER_CLOSE_H_

#include <string>

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
    PeerCloseManager(IPeerClose *peer_close,
                     boost::asio::io_service &io_service);
    explicit PeerCloseManager(IPeerClose *peer_close);
    virtual ~PeerCloseManager();

    bool IsMembershipInUse() const {
        return membership_state_ == MEMBERSHIP_IN_USE;
    }
    bool IsMembershipInWait() const {
        return membership_state_ == MEMBERSHIP_IN_WAIT;
    }
    IPeerClose *peer_close() const { return peer_close_; }
    IPeerClose::Families *families() { return &families_; }

    bool IsCloseInProgress() const { return state_ != NONE; }
    bool IsInGracefulRestartTimerWait() const {
        return state_ == GR_TIMER || state_ == LLGR_TIMER;
    }
    bool IsQueueEmpty() const { return event_queue_->IsQueueEmpty(); }

    void Close(bool non_graceful);
    void ProcessEORMarkerReceived(Address::Family family);
    void MembershipRequest();
    void MembershipRequestCallback();
    void FillCloseInfo(BgpNeighborResp *resp) const;
    bool MembershipPathCallback(DBTablePartBase *root, BgpRoute *rt,
                                BgpPath *path);

private:
    friend class PeerCloseTest;
    friend class PeerCloseManagerTest;
    friend class GracefulRestartTest;

    enum State {
        BEGIN_STATE,
        NONE = BEGIN_STATE,
        STALE,
        GR_TIMER,
        LLGR_STALE,
        LLGR_TIMER,
        SWEEP,
        DELETE,
        END_STATE = DELETE
    };

    enum MembershipState {
        BEGIN_MEMBERSHIP_STATE,
        MEMBERSHIP_NONE = BEGIN_MEMBERSHIP_STATE,
        MEMBERSHIP_IN_USE,
        MEMBERSHIP_IN_WAIT,
        END_MEMBERSHIP_STATE = MEMBERSHIP_IN_WAIT
    };

    enum EventType {
        BEGIN_EVENT,
        EVENT_NONE = BEGIN_EVENT,
        CLOSE,
        EOR_RECEIVED,
        MEMBERSHIP_REQUEST,
        MEMBERSHIP_REQUEST_COMPLETE_CALLBACK,
        TIMER_CALLBACK,
        END_EVENT = TIMER_CALLBACK
    };

    struct Event {
        Event(EventType event_type, bool non_graceful) :
                event_type(event_type), non_graceful(non_graceful),
                family(Address::UNSPEC) { }
        Event(EventType event_type, Address::Family family) :
                event_type(event_type), non_graceful(false),
                family(family) { }
        Event(EventType event_type) : event_type(event_type),
                                      non_graceful(false),
                                      family(Address::UNSPEC) { }
        Event() : event_type(EVENT_NONE), non_graceful(false),
                             family(Address::UNSPEC) { }

        EventType event_type;
        bool non_graceful;
        Address::Family family;
    };

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

    State state() const { return state_; }
    const Stats &stats() const { return stats_; }
    void Close(Event *event);
    void ProcessEORMarkerReceived(Event *event);
    virtual void StartRestartTimer(int time);
    bool RestartTimerCallback();
    void RestartTimerCallback(Event *event);
    void ProcessClosure();
    void CloseComplete();
    void TriggerSweepStateActions();
    std::string GetStateName(State state) const;
    std::string GetMembershipStateName(MembershipState state) const;
    void CloseInternal();
    void MembershipRequest(Event *event);
    bool MembershipRequestCallback(Event *event);
    void StaleNotify();
    bool EventCallback(Event *event);
    void EnqueueEvent(Event *event) { event_queue_->Enqueue(event); }
    bool close_again() const { return close_again_; }
    virtual bool AssertMembershipState(bool do_assert = true);
    virtual bool AssertMembershipReqCount(bool do_assert = true);
    virtual bool AssertSweepState(bool do_assert = true);
    virtual bool AssertMembershipManagerInUse(bool do_assert = true);
    void set_membership_state(MembershipState state) {
        membership_state_ = state;
    }

    virtual bool CanUseMembershipManager() const;
    virtual void GetRegisteredRibs(std::list<BgpTable *> *tables);
    virtual bool IsRegistered(BgpTable *table) const;
    virtual void Unregister(BgpTable *table);
    virtual void WalkRibIn(BgpTable *table);
    virtual void UnregisterRibOut(BgpTable *table);
    virtual bool IsRibInRegistered(BgpTable *table) const;
    virtual void UnregisterRibIn(BgpTable *table);

    IPeerClose *peer_close_;
    Timer *stale_timer_;
    boost::scoped_ptr<WorkQueue<Event *> > event_queue_;
    State state_;
    bool close_again_;
    bool non_graceful_;
    int gr_elapsed_;
    int llgr_elapsed_;
    MembershipState membership_state_;
    IPeerClose::Families families_;
    Stats stats_;
    tbb::atomic<int> membership_req_pending_;
};

#endif  // SRC_BGP_BGP_PEER_CLOSE_H_
