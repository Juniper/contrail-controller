/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_PEER_MEMBERSHIP_H__
#define __BGP_PEER_MEMBERSHIP_H__

#include <set>

#include "base/lifetime.h"
#include "base/util.h"
#include "base/queue_task.h"
#include "db/db_table_walker.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_table.h"

class IPeer;
class BgpServer;
class DBTableBase;
class PeerRibMembershipManager;
class RibOut;
class ShowRoutingInstanceTable;
class BgpNeighborResp;

struct MembershipRequest {
public:

    // Different actions to take when the table is walked. Not all combinations
    // are valid. Some are, such as RIBIN_PATH_DELETE and RIBOUT_PATH_DELETE
    enum Action {
        INVALID       = 0,
        RIBIN_ADD     = 1 << 0,
        RIBIN_DELETE  = 1 << 1,
        RIBIN_STALE   = 1 << 2,
        RIBIN_SWEEP   = 1 << 3,
        RIBOUT_ADD    = 1 << 4,
        RIBOUT_DELETE = 1 << 5,
    };

    MembershipRequest();
    static std::string ActionMaskToString(Action action_mask);

    Action              action_mask;
    IPeer              *ipeer;
    RibExportPolicy     policy;
    int                 instance_id;

    typedef boost::function<void(IPeer *ipeer, BgpTable *)> NotifyCompletionFn;
    NotifyCompletionFn  notify_completion_fn;

    typedef boost::function<int(IPeerRib *)> ActionGetFn;
    ActionGetFn         action_get_fn;
};

typedef std::vector<MembershipRequest> MembershipRequestList;

//
// This structure represents an event for an IPeerRib. It is used to post
// events for the BGP peer membership task.  Handling REGISTER/UNREGISTER
// and JOIN/LEAVE events from a dedicated task allows us to avoid issues
// that would otherwise arise if we were to handle them sychronously from
// the BGP state machine task.
//
struct IPeerRibEvent {
    enum EventType {
        REGISTER_RIB,
        REGISTER_RIB_COMPLETE,
        UNREGISTER_RIB,
        UNREGISTER_RIB_COMPLETE,
        UNREGISTER_PEER,
        UNREGISTER_PEER_COMPLETE,
    };

    IPeerRibEvent(EventType event_type, IPeer *ipeer, BgpTable *table)
            : event_type(event_type), ipeer(ipeer), table(table) {
    }

    IPeerRibEvent(EventType event_type, IPeer *ipeer, BgpTable *table,
                  RibExportPolicy policy)
            : event_type(event_type), ipeer(ipeer), table(table) {
        request.policy = policy;
    }
    IPeerRibEvent(EventType event_type, IPeer *ipeer, BgpTable *table,
                  MembershipRequestList *request_list)
            : event_type(event_type), ipeer(ipeer), table(table),
              request_list(request_list) {
    }
    static std::string EventTypeToString(EventType event_type);

    EventType              event_type;
    IPeer                 *ipeer;
    BgpTable              *table;
    MembershipRequestList *request_list;
    MembershipRequest      request;
    DISALLOW_COPY_AND_ASSIGN(IPeerRibEvent);
};

//
// This class represents the membership of an IPeer in a BgpTable. The
// result of this membership is a RibOut instance.  An instance of an
// IPeerRib is created when an IPeer registers with a BgPTable and gets
// deleted when the IPeer unregsisters from the table.
//
// An IPeerRib is part of a set that lives in the PeerMembershipMgr.
//
class IPeerRib {
public:
    IPeerRib(IPeer *ipeer, BgpTable *table,
             PeerRibMembershipManager *membership_mgr);
    ~IPeerRib();

    bool operator<(const IPeerRib &rhs) const;

    IPeer *ipeer() { return ipeer_; }
    BgpTable *table() { return table_; }

    void SetStale() { stale_ = true; }
    void ResetStale() { stale_ = false; }
    bool IsStale() { return stale_; }

    void RegisterRibIn();
    void UnregisterRibIn();
    bool IsRibInRegistered();
    void SetRibInRegistered(bool set);
    void RibInJoin(DBTablePartBase *root, DBEntryBase *db_entry,
                   BgpTable *table, MembershipRequest::Action action_mask);
    void RibInLeave(DBTablePartBase *root, DBEntryBase *db_entry,
                    BgpTable *table, MembershipRequest::Action action_mask);

    void RegisterRibOut(RibExportPolicy policy);
    void UnregisterRibOut();
    bool IsRibOutActive() const;
    void DeactivateRibOut();
    bool IsRibOutRegistered() const;
    void SetRibOutRegistered(bool set);

    void RibOutJoin(DBTablePartBase *root, DBEntryBase *db_entry,
                    BgpTable *table, MembershipRequest::Action action_mask);
    void RibOutLeave(DBTablePartBase *root, DBEntryBase *db_entry,
                     BgpTable *table, MembershipRequest::Action action_mask);
    
    void ManagedDelete();

    int instance_id() const { return instance_id_; }
    void set_instance_id(int instance_id) { instance_id_ = instance_id; }

private:

    IPeer *ipeer_;
    BgpTable *table_;
    PeerRibMembershipManager *membership_mgr_;
    RibOut *ribout_;
    LifetimeRef<IPeerRib> table_delete_ref_;
    bool ribin_registered_;
    bool ribout_registered_;
    bool stale_;
    int instance_id_;       // xmpp peer instance-id
    DISALLOW_COPY_AND_ASSIGN(IPeerRib);
};


//
// This struct represents the compare function that's used to impose the
// strict weak ordering in the PeerRibSet of IPeerRibs maintained by the
// PeerMembershipMgr.
//
struct IPeerRibCompare {
    bool operator()(const IPeerRib *lhs, const IPeerRib *rhs) {
        return lhs->operator<(*rhs);
    }
};

//
// This class represents the interface for peer memebership management in a
// BgpServer instance.

// It provides Register and Unregister methods for an IPeer to manage it's
// memebership in BgpTables.  These methods post (directly or indirectly)
// IPeerRibEvents to a WorkQueue that's part of the class. IPeerRibEvents
// are processed asynchronoulsy from the BGP peer membership task. This lets
// us finesse concurrency issues that would otherwise arise if we were to
// handle register/unregsiter events from the BGP state machine task.
//
// In the steady state an IPeerRib instance represents the membership of an
// IPeer in a BgpTable.  The PeerMembershipMgr maintains a set of IPeerRibs
// thus representing the global membership state across all BgpTables and
// IPeers.  Keeping all this state in a centralized location, as opposed to
// spreading it out over multiple IPeers or BgpTables, makes it possible to
// optimize regsiter/unregister processing in future.
//
class PeerRibMembershipManager {
public:
    typedef std::set<IPeerRib *, IPeerRibCompare> PeerRibSet;
    typedef std::map<BgpTable *, MembershipRequestList *>
                                     TableMembershipRequestMap;
    typedef MembershipRequest::NotifyCompletionFn NotifyCompletionFn;
    static const int kMembershipTaskInstanceId = 0;

    PeerRibMembershipManager(BgpServer *server);
    virtual ~PeerRibMembershipManager();

    virtual void Register(IPeer *ipeer, BgpTable *table,
                  const RibExportPolicy &policy, int instance_id,
                  NotifyCompletionFn notify_completion_fn = NULL);
    virtual void Unregister(IPeer *ipeer, BgpTable *table,
                    NotifyCompletionFn notify_completion_fn = NULL);
    void UnregisterPeer(IPeer *ipeer,
             MembershipRequest::ActionGetFn action_get_fn,
             MembershipRequest::NotifyCompletionFn notify_completion_fn);

    bool PeerRegistered(IPeer *ipeer, BgpTable *table) {
        if (IPeerRibFind(ipeer, table)) {
            return true;
        }
        return false;
    }

    void Enqueue(IPeerRibEvent *event) { event_queue_->Enqueue(event); }

    BgpServer *server() { return server_; }
    void FillRoutingInstanceInfo(ShowRoutingInstanceTable &inst,
                                 const BgpTable * table) const;

    void FillPeerMembershipInfo(const IPeer *peer, BgpNeighborResp &resp);
    IPeerRib *IPeerRibFind(IPeer *ipeer, BgpTable *table);
    bool IsQueueEmpty() const { return event_queue_->IsQueueEmpty(); }
    void FillRegisteredTable(IPeer *peer, std::vector<std::string> &list);

private:
    friend class BgpServerUnitTest;
    friend class BgpXmppUnitTest;
    friend class PeerMembershipMgrTest;
    friend class PeerRibMembershipManagerTest;

    typedef std::multimap<const BgpTable *, IPeer *> RibPeerMap;
    typedef std::multimap<const IPeer *, IPeerRib *> PeerRibMap;

    void Join(BgpTable *table, MembershipRequestList *request_list);
    bool RouteJoin(DBTablePartBase *root, DBEntryBase *db_entry,
                   BgpTable *table, MembershipRequestList *request_list);
    void JoinDone(DBTableBase *db, MembershipRequestList *request_list);

    void Leave(BgpTable *table, MembershipRequestList *request_list);
    bool RouteLeave(DBTablePartBase *root, DBEntryBase *db_entry,
                    BgpTable *table, MembershipRequestList *request_list);
    void LeaveDone(DBTableBase *db, MembershipRequestList *request_list);

    IPeerRibEvent *ProcessRequest(IPeerRibEvent::EventType event_type,
                                  BgpTable *table,
                                  const MembershipRequest &request);
    void UnregisterPeerCallback(IPeerRibEvent *event);
    void UnregisterPeerDone(IPeer *ipeer, BgpTable *table, int *count,
             MembershipRequest::NotifyCompletionFn notify_completion_fn);
    void UnregisterPeerCompleteCallback(IPeerRibEvent *event);
    void NotifyCompletion(BgpTable *table, MembershipRequestList *request_list);
    void ProcessRegisterRibEvent(BgpTable *table,
                                 MembershipRequestList *request_list);
    void ProcessRegisterRibCompleteEvent(IPeerRibEvent *event);
    void ProcessUnregisterRibEvent(BgpTable *table,
                                   MembershipRequestList *request_list);
    void ProcessUnregisterRibCompleteEvent(IPeerRibEvent *event);

    IPeerRib *IPeerRibInsert(IPeer *ipeer, BgpTable *table);
    void IPeerRibRemove(IPeerRib *peer_rib);

    bool IPeerRibEventCallback(IPeerRibEvent *event);
    void IPeerRibEventCallbackUnlocked(IPeerRibEvent *event);
    void MembershipRequestListDebug(const char *function, int line,
                                    BgpTable *table,
                                    MembershipRequestList *request_list);

    static int membership_task_id_;

    BgpServer *server_;
    WorkQueue<IPeerRibEvent *> *event_queue_;
    PeerRibSet peer_rib_set_;
    RibPeerMap rib_peer_map_;
    PeerRibMap peer_rib_map_;

    TableMembershipRequestMap register_request_map_;
    TableMembershipRequestMap unregister_request_map_;
    tbb::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(PeerRibMembershipManager);
};

#endif // __BGP_PEER_MEMBERSHIP_H__
