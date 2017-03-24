/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_MEMBERSHIP_H_
#define SRC_BGP_BGP_MEMBERSHIP_H_

#include <boost/dynamic_bitset.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/atomic.h>
#include <tbb/spin_rw_mutex.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/lifetime.h"
#include "base/queue_task.h"
#include "db/db_table.h"
#include "bgp/bgp_ribout.h"

class BgpNeighborResp;
class BgpServer;
class BgpTable;
class IPeer;
class RibOut;
class ShowMembershipPeerInfo;
class ShowRoutingInstanceTable;
class TaskTrigger;

//
// This class implements membership management for a BgpServer.

// It provides methods for an IPeer to manage it's membership in BgpTables.
// There are two kinds of memberships for a (IPeer, BgpTable) pair - RibIn
// and RibOut. RibIn membership is needed for an IPeer to add BgpPaths to a
// BgpTable. RibOut membership is needed to advertise routes from a BgpTable
// to an IPeer.
//
// APIs are provided to manage these 2 types of memberships together as well
// as separately. An API to walk all BgpPaths added by an IPeer to a BgpTable
// is also provided. This is used by clients to delete BgpPaths when a peer
// is going down, to mark BgpPaths as stale when handling graceful restart of
// a peer etc. The actual logic of deleting or modifying a BgpPath has to be
// in the client code.
//
// Many APIs require traversal of the entire BgpTable and so are asynchronous.
// An IPeer gets notified on completion of API via virtual function callback.
// Along similar lines, when using a walk API, an IPeer gets notified for each
// BgpPath added by it via a virtual function.  A client is allowed to have 1
// outstanding request for a given (IPeer, BgpTable) pair.
//
// There are scenarios where multiple IPeers subscribe to a given BgpTable at
// roughly the same time. Further, this can happen for many BgpTables at about
// the same time as well. The implementation handles this quite efficiently by
// accumulating multiple (IPeer, BgpTable) requests to reduce/minimize the
// number of table walks. The table walk functionality is delegated to the
// Walker class.
//
// Membership information corresponding to (IPeer, BgpTable) pairs is organized
// with the above goal in mind. An IPeer is represented using a PeerState and a
// BgpTable is represented using a RibState. A PeerStateMap and RibStateMap are
// used for efficient lookup and insertion. A (IPeer, BgpTable) is represented
// using a PeerRibState. Linkage between PeerState, RibState, and PeerRibState
// is described in more detail later.
//
// Updates to the PeerStateMap, RibStateMap and other maps/sets maintained in
// a PeerState and RibState can happen synchronously from from the API calls.
// This allows for easy detection of violations in API assumptions e.g. only a
// single outstanding request for a (IPeer, BgpTable) pair. Since multiple API
// calls can happen in parallel, access to internal state has to be serialized
// using a read-write mutex. A read-write mutex is used to allow parallel calls
// to GetRegistrationInfo from multiple db::DBTable tasks. All other API calls
// are relatively infrequent.
//
// The read-write mutex ensures consistency of BgpMembershipManager's internal
// state. However, the BgpMembershipManager also needs to call external APIs
// which have their own concurrency requirements.  A WorkQueue of Events is
// used to satisfy these requirements. The WorkQueue is processed in context
// of bgp::PeerMembership task and appropriate exclusion polices are specified
// at system initialization. Client callbacks for API complete notifications
// are also made from the bgp:PeerMembership task.
//
class BgpMembershipManager {
public:
    typedef boost::function<void(IPeer *, BgpTable *, bool)>
        PeerRegistrationCallback;

    explicit BgpMembershipManager(BgpServer *server);
    virtual ~BgpMembershipManager();

    int RegisterPeerRegistrationCallback(PeerRegistrationCallback callback);
    void UnregisterPeerRegistrationCallback(int id);

    virtual void Register(IPeer *peer, BgpTable *table,
        const RibExportPolicy &policy, int instance_id = -1);
    void RegisterRibIn(IPeer *peer, BgpTable *table);
    virtual void Unregister(IPeer *peer, BgpTable *table);
    void UnregisterRibIn(IPeer *peer, BgpTable *table);
    virtual void UnregisterRibOut(IPeer *peer, BgpTable *table);
    void WalkRibIn(IPeer *peer, BgpTable *table);

    bool GetRegistrationInfo(const IPeer *peer, const BgpTable *table,
        int *instance_id = NULL, uint64_t *subscription_gen_id = NULL) const;
    void SetRegistrationInfo(const IPeer *peer, const BgpTable *table,
        int instance_id, uint64_t subscription_gen_id);

    bool IsRegistered(const IPeer *peer, const BgpTable *table) const;
    bool IsRibInRegistered(const IPeer *peer, const BgpTable *table) const;
    bool IsRibOutRegistered(const IPeer *peer, const BgpTable *table) const;
    uint32_t GetRibOutQueueDepth(const IPeer *peer,
                                 const BgpTable *table) const;

    void GetRegisteredRibs(const IPeer *peer,
        std::list<BgpTable *> *table_list) const;

    void FillRoutingInstanceTableInfo(ShowRoutingInstanceTable *srit,
        const BgpTable *table) const;
    void FillPeerMembershipInfo(const IPeer *peer, BgpNeighborResp *resp) const;

    BgpServer *server() { return server_; }
    bool IsQueueEmpty() const;
    size_t GetMembershipCount() const;
    uint64_t current_jobs_count() const { return current_jobs_count_; }
    uint64_t total_jobs_count() const { return total_jobs_count_; }

protected:
    struct Event;

    virtual bool EventCallbackInternal(Event *event);

    mutable tbb::spin_rw_mutex rw_mutex_;

private:
    class PeerState;
    class PeerRibState;
    class RibState;
    class Walker;

    friend class BgpMembershipManager::PeerState;
    friend class BgpMembershipManager::RibState;
    friend class BgpMembershipManager::PeerRibState;
    friend class BgpMembershipManager::Walker;
    friend class BgpMembershipTest;
    friend class BgpServerUnitTest;
    friend class BgpXmppUnitTest;

    enum Action {
        NONE,
        RIBOUT_ADD,
        RIBIN_DELETE,
        RIBIN_WALK,
        RIBIN_WALK_RIBOUT_DELETE,
        RIBIN_DELETE_RIBOUT_DELETE
    };

    enum EventType {
        REGISTER_RIB,
        REGISTER_RIB_COMPLETE,
        UNREGISTER_RIB,
        UNREGISTER_RIB_COMPLETE,
        WALK_RIB_COMPLETE
    };

    typedef std::vector<PeerRegistrationCallback> PeerRegistrationListenerList;
    typedef std::map<const IPeer *, PeerState *> PeerStateMap;
    typedef std::map<const BgpTable *, RibState *> RibStateMap;
    typedef std::set<PeerRibState *> PeerRibList;

    void UnregisterRibInUnlocked(PeerRibState *prs);

    PeerState *LocatePeerState(IPeer *peer);
    PeerState *FindPeerState(const IPeer *peer);
    const PeerState *FindPeerState(const IPeer *peer) const;
    void DestroyPeerState(PeerState *ps);

    RibState *LocateRibState(BgpTable *table);
    RibState *FindRibState(const BgpTable *table);
    const RibState *FindRibState(const BgpTable *table) const;
    void DestroyRibState(RibState *ps);
    void EnqueueRibState(RibState *rs);

    PeerRibState *LocatePeerRibState(IPeer *peer, BgpTable *table);
    PeerRibState *FindPeerRibState(const IPeer *peer, const BgpTable *table);
    const PeerRibState *FindPeerRibState(const IPeer *peer,
        const BgpTable *table) const;
    void DestroyPeerRibState(PeerRibState *prs);

    void TriggerRegisterRibCompleteEvent(IPeer *peer, BgpTable *table);
    void TriggerUnregisterRibCompleteEvent(IPeer *peer, BgpTable *table);
    void TriggerWalkRibCompleteEvent(IPeer *peer, BgpTable *table);

    void ProcessRegisterRibEvent(Event *event);
    void ProcessRegisterRibCompleteEvent(Event *event);
    void ProcessUnregisterRibEvent(Event *event);
    void ProcessUnregisterRibCompleteEvent(Event *event);
    void ProcessWalkRibCompleteEvent(Event *event);

    void EnqueueEvent(Event *event) { event_queue_->Enqueue(event); }
    bool EventCallback(Event *event);

    void NotifyPeerRegistration(IPeer *peer, BgpTable *table, bool unregister);

    // Testing only.
    void SetQueueDisable(bool value) { event_queue_->set_disable(value); }
    Walker *walker() { return walker_.get(); }

    BgpServer *server_;
    tbb::atomic<uint64_t> current_jobs_count_;
    tbb::atomic<uint64_t> total_jobs_count_;
    RibStateMap rib_state_map_;
    PeerStateMap peer_state_map_;
    boost::scoped_ptr<Walker> walker_;
    boost::scoped_ptr<WorkQueue<Event *> > event_queue_;

    boost::dynamic_bitset<> registration_bmap_;
    PeerRegistrationListenerList registration_callbacks_;

    DISALLOW_COPY_AND_ASSIGN(BgpMembershipManager);
};

struct BgpMembershipManager::Event {
    friend class BgpMembershipManager;

    typedef BgpMembershipManager::EventType EventType;

    Event(EventType event_type, IPeer *peer, BgpTable *table);
    Event(EventType event_type, IPeer *peer, BgpTable *table,
        const RibExportPolicy &policy, int instance_id);

    EventType event_type;
    IPeer *peer;
    BgpTable *table;
    RibExportPolicy policy;
    int instance_id;
};

//
// This represents an IPeer within the BgpMembershipManager, which maintains a
// map of PeerStates keyed an IPeer pointer.
//
// The PeerRibStateMap allows efficient creation and lookup of PeerRibState.
// It can be accessed synchronously from the API calls to BgpMembershipManager
// or from the bgp::PeerMembership task. In the former case, the read-write
// mutex in the BgpMembershipManager is sufficient to serialize access to the
// PeerRibStateMap. In the latter case, task exclusion policies prevent any
// parallel access.
//
class BgpMembershipManager::PeerState {
public:
    typedef BgpMembershipManager::RibState RibState;
    typedef BgpMembershipManager::PeerRibState PeerRibState;
    typedef std::map<const RibState *, PeerRibState *> PeerRibStateMap;

    PeerState(BgpMembershipManager *manager, IPeer *peer);
    ~PeerState();

    PeerRibState *LocatePeerRibState(RibState *rs);
    PeerRibState *FindPeerRibState(const RibState *rs);
    const PeerRibState *FindPeerRibState(const RibState *rs) const;
    bool RemovePeerRibState(PeerRibState *prs);

    void GetRegisteredRibs(std::list<BgpTable *> *table_list) const;
    size_t GetMembershipCount() const { return rib_map_.size(); }
    void FillPeerMembershipInfo(BgpNeighborResp *resp) const;

    IPeer *peer() { return peer_; }
    const IPeer *peer() const { return peer_; }

private:
    BgpMembershipManager *manager_;
    IPeer *peer_;
    PeerRibStateMap rib_map_;

    DISALLOW_COPY_AND_ASSIGN(PeerState);
};

//
// This represents a BgpTable within the BgpMembershipManager, which maintains
// a map of RibStates keyed a BgpTable pointer.
//
// A RibState maintains two PeerRibLists.
// The pending list contains all the PeerRibStates for which some action needs
// to performed during the next walk of the associated BgpTable. This is used
// by the Walker when it's starting a walk of the BgpTable.
// The regular PeerRibList contains all the PeerRibStates for this RibState.
// It is used only for introspect.
//
class BgpMembershipManager::RibState {
public:
    typedef BgpMembershipManager::PeerRibState PeerRibState;
    typedef BgpMembershipManager::PeerRibList PeerRibList;
    typedef PeerRibList::iterator iterator;

    explicit RibState(BgpMembershipManager *manager, BgpTable *table);
    ~RibState();
    void ManagedDelete() {}

    iterator begin() { return pending_peer_rib_list_.begin(); }
    iterator end() { return pending_peer_rib_list_.end(); }

    void EnqueuePeerRibState(PeerRibState *prs);
    void ClearPeerRibStateList();

    void InsertPeerRibState(PeerRibState *prs);
    bool RemovePeerRibState(PeerRibState *prs);

    void FillRoutingInstanceTableInfo(ShowRoutingInstanceTable *srit) const;

    BgpTable *table() const { return table_; }
    void increment_walk_count() { walk_count_++; }

private:
    BgpMembershipManager *manager_;
    BgpTable *table_;
    uint32_t request_count_;
    uint32_t walk_count_;
    PeerRibList peer_rib_list_;
    PeerRibList pending_peer_rib_list_;
    LifetimeRef<RibState> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(RibState);
};

//
// This class represents the membership of an IPeer in a BgpTable. The result
// of this membership is a RibOut instance.  An instance of a PeerRibState is
// created when an IPeer registers with a BgpTable and gets deleted when the
// IPeer unregisters from the BgpTable.
//
// A PeerState has a map of PeerRibStates keyed by RibState pointer.
// A PeerRibState is on a list of PeerRibStates in it's RibState.
// If a PeerRibState has a pending action, it's also on the pending list in
// the RibState.
// The action is NONE in steady state.
//
class BgpMembershipManager::PeerRibState {
public:
    PeerRibState(BgpMembershipManager *manager, PeerState *ps, RibState *rs);
    ~PeerRibState();

    void RegisterRibOut(const RibExportPolicy &policy);
    void UnregisterRibOut();
    void DeactivateRibOut();
    void UnregisterRibIn();
    void WalkRibIn();

    void FillMembershipInfo(ShowMembershipPeerInfo *smpi) const;

    const IPeer *peer() const { return ps_->peer(); }
    PeerState *peer_state() { return ps_; }
    const PeerState *peer_state() const { return ps_; }
    RibState *rib_state() { return rs_; }
    RibOut *ribout() const { return ribout_; }
    int ribout_index() const { return ribout_index_; }
    const BgpTable *table() const { return rs_->table(); }

    BgpMembershipManager::Action action() const { return action_; }
    void set_action(BgpMembershipManager::Action action) { action_ = action; }
    void clear_action() { action_ = BgpMembershipManager::NONE; }
    bool ribin_registered() const { return ribin_registered_; }
    void set_ribin_registered(bool value) { ribin_registered_ = value; }
    bool ribout_registered() const { return ribout_registered_; }
    void set_ribout_registered(bool value) { ribout_registered_ = value; }
    int instance_id() const { return instance_id_; }
    void set_instance_id(int instance_id) { instance_id_ = instance_id; }
    uint64_t subscription_gen_id() const { return subscription_gen_id_; }
    void set_subscription_gen_id(uint64_t subscription_gen_id) {
        subscription_gen_id_ = subscription_gen_id;
    }

private:
    BgpMembershipManager *manager_;
    PeerState *ps_;
    RibState *rs_;
    RibOut *ribout_;
    int ribout_index_;
    BgpMembershipManager::Action action_;
    bool ribin_registered_;
    bool ribout_registered_;
    int instance_id_;
    uint64_t subscription_gen_id_;

    DISALLOW_COPY_AND_ASSIGN(PeerRibState);
};

//
// This class is responsible for efficient implementation of BgpTable walks
// for the BgpMembershipManager. It accepts walk requests for any number of
// RibStates and triggers table walks one at a time. It has a maximum of one
// ongoing table walk at any given time.
//
// The RibStateList contains all RibStates for which walks have not yet been
// started. The Walker removes the first RibState from the list and starts a
// table walk for it.
//
// The RibStateSet is used to prevent duplicates in the RibStateList. Using
// just the RibStateSet to maintain the pending RibStates would have caused
// problems if the same RibState is enqueued repeatedly. In that case, the
// Walker would walk the same BgpTable repeatedly and starve out all other
// RibStates. Using the RibStateSet and RibStateList together prevents this
// problem.
//
// Items are inserted into RibStateList either from the bgp::PeerMembership
// task or from other tasks that invoke the BgpMembershipManager public APIs.
// There's no issues with concurrent access in the former case. In the latter
// case, access is serialized because of the mutex in BgpMembershipManager.
//
// The Walker creates temporary internal state when it starts a table walk so
// that walk callbacks for each DBEntry can be handled with minimal processing
// overhead. Details on this temporary state are as follows:
//
// - walk_ref_ is the walker for the current walk
// - rs_ is the RibState for which the walk was started
// - peer_rib_list_ is the list of PeerRibStates for the current RibState
//   that have a pending action. The pending list in RibState is logically
//   moved to this field. This allows the RibState to accumulate a new set
//   of pending PeerRibStates that can be serviced in a subsequent walk.
//   The peer_rib_list_ is used to create and enqueue events when the table
//   walk finishes.
// - peer_list_ is the list of IPeers to be notified about BgpPaths added
//   by them for RibIn processing.
// - ribout_state_map_ is a map of RibOutStates that need to be processed
//   for each route.
// - ribout_state_list_ is a list of same RibOutStates as ribout_state_map_.
//   It allows simpler traversal compared to the ribout_state_map_ when each
//   DBEntry is processed.
//
// A RibOutState is created for each unique RibOut in the PeerRibStates in
// peer_rib_list_. It's join and leave bitsets are based on the action in
// the PeerRibStates.
//
// A TaskTrigger that runs in context of bgp::PeerMembership task is used to
// handle start and finish of table walks. This avoids concurrency issues in
// accessing/clearing the pending list in the RibState. Note that TaskTrigger
// in this class and the WorkQueue in BgpMembershipManager both use instance
// id of 0, so they can't run concurrently.
//
class BgpMembershipManager::Walker {
public:
    explicit Walker(BgpMembershipManager *manager);
    ~Walker();

    void Enqueue(RibState *rs);
    bool IsQueueEmpty() const;

private:
    friend class BgpMembershipTest;

    class RibOutState {
    public:
        explicit RibOutState(RibOut *ribout) : ribout_(ribout) { }
        ~RibOutState() { }

        RibOut *ribout() { return ribout_; }
        void JoinPeer(int index) { join_bitset_.set(index); }
        void LeavePeer(int index) { leave_bitset_.set(index); }
        const RibPeerSet &join_bitset() { return join_bitset_; }
        const RibPeerSet &leave_bitset() { return leave_bitset_; }

    private:
        RibOut *ribout_;
        RibPeerSet join_bitset_;
        RibPeerSet leave_bitset_;

        DISALLOW_COPY_AND_ASSIGN(RibOutState);
    };

    typedef BgpMembershipManager::Event Event;
    typedef BgpMembershipManager::RibState RibState;
    typedef BgpMembershipManager::PeerRibState PeerRibState;
    typedef BgpMembershipManager::PeerRibList PeerRibList;
    typedef std::set<RibState *> RibStateSet;
    typedef std::list<RibState *> RibStateList;
    typedef std::map<RibOut *, RibOutState *> RibOutStateMap;
    typedef std::list<RibOutState *> RibOutStateList;
    typedef std::set<const IPeer *> PeerList;

    RibOutState *LocateRibOutState(RibOut *ribout);
    bool WalkCallback(DBTablePartBase *tpart, DBEntryBase *db_entry);
    void WalkDoneCallback(DBTableBase *table);
    void WalkStart();
    void WalkFinish();
    bool WalkTrigger();

    // Testing only.
    void SetQueueDisable(bool value);
    size_t GetQueueSize() const { return rib_state_list_.size(); }
    size_t GetPeerListSize() const { return peer_list_.size(); }
    size_t GetPeerRibListSize() const { return peer_rib_list_.size(); }
    size_t GetRibOutStateListSize() const { return ribout_state_list_.size(); }
    void PostponeWalk();
    void ResumeWalk();

    BgpMembershipManager *manager_;
    RibStateSet rib_state_set_;
    RibStateList rib_state_list_;
    boost::scoped_ptr<TaskTrigger> trigger_;

    bool postpone_walk_;
    bool walk_started_;
    bool walk_completed_;
    DBTable::DBTableWalkRef walk_ref_;
    RibState *rs_;
    PeerRibList peer_rib_list_;
    PeerList peer_list_;
    RibOutStateMap ribout_state_map_;
    RibOutStateList ribout_state_list_;

    DISALLOW_COPY_AND_ASSIGN(Walker);
};

#endif  // SRC_BGP_BGP_MEMBERSHIP_H_
