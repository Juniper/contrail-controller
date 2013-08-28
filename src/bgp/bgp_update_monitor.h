/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_update_monitor_h
#define ctrlplane_bgp_update_monitor_h

#include <vector>
#include <boost/function.hpp>

#include <tbb/mutex.h>
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_update.h"
#ifndef _LIBCPP_VERSION
#include <tbb/compat/condition_variable>
#endif
#include "base/util.h"

class DBEntryBase;
struct DBState;
class UpdateQueue;

//
// This class implements an encapsulator for a RouteUpdate which grants the
// user the right to modify the update without holding a lock on the monitor.
// The user must go through the RouteUpdateMonitor and the UpdateQueue to
// remove the RouteUpdate from the UpdateQueue.
//
// A RouteUpdatePtr with a NULL RouteUpdate indicates either the end of an
// UpdateQueue or the presence of an UpdateMarker.
//
// A RouteUpdatePtr is typically used by the task doing update dequeue from
// the UpdateQueue.  The task is associated with the scheduling group that
// covers the RibOut in question.
//
// Movable semantics implemented in c++03.
//
class RouteUpdatePtr {
public:
    struct Proxy {
        Proxy()
	        : entry_mutexp(NULL), rt_update(NULL),
	          monitor_mutexp(NULL), cond_var(NULL) {
        }
        tbb::mutex *entry_mutexp;
        RouteUpdate *rt_update;
        tbb::mutex *monitor_mutexp;
        std::condition_variable *cond_var;
    };
    RouteUpdatePtr()
        : entry_mutexp_(NULL), rt_update_(NULL),
          monitor_mutexp_(NULL), cond_var_(NULL) {
    }
    RouteUpdatePtr(tbb::mutex *entry_mutexp, RouteUpdate *rt_update,
                   tbb::mutex *monitor_mutexp,
                   std::condition_variable *cond_var);
    RouteUpdatePtr(RouteUpdatePtr &rhs)
        : entry_mutexp_(NULL), rt_update_(NULL),
          monitor_mutexp_(NULL), cond_var_(NULL) {
        swap(rhs);
    }
    RouteUpdatePtr(Proxy rhs)
        : entry_mutexp_(rhs.entry_mutexp), rt_update_(rhs.rt_update),
          monitor_mutexp_(rhs.monitor_mutexp), cond_var_(rhs.cond_var) {
    }
    ~RouteUpdatePtr();

    RouteUpdate *operator->() { return rt_update_; }
    RouteUpdate *get() { return rt_update_; }

    RouteUpdatePtr &operator=(RouteUpdatePtr &rhs) {
        swap(rhs);
        return *this;
    }

    RouteUpdatePtr &operator=(Proxy rhs) {
        RouteUpdatePtr tmp(rhs);
        swap(tmp);
        return *this;
    }

    RouteUpdate *release() {
        RouteUpdate *rt_update = rt_update_;
        RouteUpdatePtr tmp;
        tmp.swap(*this);
        return rt_update;
    }

    void swap(RouteUpdatePtr &rhs) {
        std::swap(entry_mutexp_, rhs.entry_mutexp_);
        std::swap(rt_update_, rhs.rt_update_);
        std::swap(monitor_mutexp_, rhs.monitor_mutexp_);
        std::swap(cond_var_, rhs.cond_var_);
    }

    operator Proxy() {
        Proxy proxy;
        std::swap(proxy.entry_mutexp, entry_mutexp_);
        std::swap(proxy.rt_update, rt_update_);
        std::swap(proxy.monitor_mutexp, monitor_mutexp_);
        std::swap(proxy.cond_var, cond_var_);
        return proxy;
    }

private:
    tbb::mutex *entry_mutexp_;
    RouteUpdate *rt_update_;
    tbb::mutex *monitor_mutexp_;
    std::condition_variable *cond_var_;
};

//
// This class implements the concurrency interface between the export module
// which generates updates and the update dequeue process which consumes them.
// Export processing runs under multiple table shards; update dequeue runs
// under a task that is associated with a scheduling group.
//
// The goal of this class is to ensure that both ends of the interface (export
// and dequeue) always see entries in a consistent state.
//
// The export module workflow is:
//    1) Access DBEntry state.
//    2) Try to acquire lock on entry.
//    3) Modify queue.
//
// The dequeue workflow is:
//    1) Access queue
//    2) Obtain lock on entry
//    3) Modify queue and DBEntry state.
//
class RibUpdateMonitor {
public:
    typedef boost::function<bool(const RouteUpdate *)> UpdateCmp;
    typedef std::vector<UpdateQueue *> QueueVec;
    explicit RibUpdateMonitor(RibOut *ribout, QueueVec *queue_vec);

    // Used by export module to obtain exclusive access to the DB state.
    // If an update is currently present and the comparison function
    // returns true, it is considered a duplicate and the function returns
    // NULL. Otherwise the update is dequeued and returned.
    DBState *GetDBStateAndDequeue(DBEntryBase *db_entry,
                                  UpdateCmp cmp,
                                  bool *duplicate);

    // Fill the mcurrent and mscheduled parameters with the contents of the
    // advertised bitmask (history) plus and updates that may be enqueue in
    // the specified queue.  Returns true if there is an update currently
    // enqueued. False otherwise.
    bool GetPeerSetCurrentAndScheduled(DBEntryBase *db_entry,
                                       int queue_id,
                                       RibPeerSet *mcurrent,
                                       RibPeerSet *mscheduled);

    // Used by the export module to enqueue/dequeue updates.
    bool EnqueueUpdate(DBEntryBase *db_entry, RouteUpdate *rt_update);
    void DequeueUpdate(RouteUpdate *rt_update);


    // Merge this new update into the existing state.
    bool MergeUpdate(DBEntryBase *db_entry, RouteUpdate *rt_update);

    // Cancel scheduled updates for the route and/or remove any current
    // advertised state.
    void ClearPeerSetCurrentAndScheduled(DBEntryBase *db_entry,
                                         RibPeerSet &mleave);

    // Used by the update dequeue process to retrieve an update.
    RouteUpdatePtr GetNextUpdate(int queue_id, UpdateEntry *upentry);

    // Used by the update dequeue process to retrieve the next entry in the
    // queue. If this is an update, it returns pointer and an associated lock.
    RouteUpdatePtr GetNextEntry(int queue_id, UpdateEntry *upentry,
                                UpdateEntry **next_upentry_p);

    // Used when iterating through updates with the same attribute.
    RouteUpdatePtr GetAttrNext(int queue_id, UpdateInfo *current_uinfo,
                               UpdateInfo **next_uinfo_p);

    void SetEntryState(DBEntryBase *db_entry, DBState *dbstate);
    void ClearEntryState(DBEntryBase *db_entry);

private:
    // Retrieve that mutex associated with the route state.
    tbb::mutex *DBStateMutex(RouteUpdate *rt_update);
    
    // Helper functions for GetRouteStateAndDequeue
    DBState *GetRouteUpdateAndDequeue(DBEntryBase *db_entry,
                                      RouteUpdate *rt_update,
                                      UpdateCmp cmp, bool *duplicate);
    DBState *GetUpdateListAndDequeue(DBEntryBase *db_entry,
                                     UpdateList *uplist);

    // Internal versions of EnqueueUpdate and DequeueUpdate.
    bool EnqueueUpdateUnlocked(DBEntryBase *db_entry,
                               RouteUpdate *rt_update,
                               UpdateList *uplist = NULL);
    void DequeueUpdateUnlocked(RouteUpdate *rt_update);

    // Helper functions for MergeUpdate
    bool RouteStateMergeUpdate(DBEntryBase *db_entry,
                               RouteUpdate *rt_update,
                               RouteState *rstate);
    bool RouteUpdateMergeUpdate(DBEntryBase *db_entry,
                                RouteUpdate *rt_update,
                                RouteUpdate *current_rt_update);
    bool UpdateListMergeUpdate(DBEntryBase *db_entry,
                               RouteUpdate *rt_update,
                               UpdateList *uplist);

    // Helper functions for ClearPeerSetCurrentAndScheduled
    void AdvertiseSListClearBits(AdvertiseSList &adv_slist,
            const RibPeerSet &clear);
    void UpdateInfoSListClearBits(UpdateInfoSList &uinfo_slist,
            const RibPeerSet &clear);
    void RouteStateClearPeerSet(DBEntryBase *db_entry,
            RouteState *rstate, const RibPeerSet &mleave);
    bool RouteUpdateClearPeerSet(DBEntryBase *db_entry,
            RouteUpdate *rt_update, const RibPeerSet &mleave);
    bool UpdateListClearPeerSet(DBEntryBase *db_entry,
            UpdateList *uplist, const RibPeerSet &mleave);

    tbb::mutex mutex_;      // consistency between queue and entry lock.
    std::condition_variable cond_var_;
    RibOut *ribout_;
    QueueVec *queue_vec_;
    DISALLOW_COPY_AND_ASSIGN(RibUpdateMonitor);
};

#endif
