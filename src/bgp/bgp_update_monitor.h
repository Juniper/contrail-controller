/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_UPDATE_MONITOR_H_
#define SRC_BGP_BGP_UPDATE_MONITOR_H_

#include <boost/function.hpp>

#include <algorithm>
#include <vector>

#include "base/util.h"

class AdvertiseSList;
class DBEntryBase;
struct DBState;
class RibOut;
class RibPeerSet;
class RouteState;
class RouteUpdate;
class UpdateEntry;
class UpdateInfo;
class UpdateInfoSList;
class UpdateList;
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
// A RouteUpdatePtr is typically used by bgp::SendUpdate task that's dequeuing
// updates from the UpdateQueue.
//
// Movable semantics implemented in c++03.
//
class RouteUpdatePtr {
public:
    struct Proxy {
        Proxy() : rt_update(NULL) {
        }
        RouteUpdate *rt_update;
    };
    RouteUpdatePtr() : rt_update_(NULL) {
    }
    RouteUpdatePtr(RouteUpdate *rt_update);
    RouteUpdatePtr(RouteUpdatePtr &rhs) : rt_update_(NULL) {
        swap(rhs);
    }
    RouteUpdatePtr(Proxy rhs) : rt_update_(rhs.rt_update) {
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
        std::swap(rt_update_, rhs.rt_update_);
    }

    operator Proxy() {
        Proxy proxy;
        std::swap(proxy.rt_update, rt_update_);
        return proxy;
    }

private:
    RouteUpdate *rt_update_;
};

//
// This implements the interface between the export module which generates
// updates (using the db::DBTable Task) and the update sender module which
// consumes them (using the bgp::SendUpdate Task).  Both export processing
// and update sender run concurrently under multiple table shards.
// TaskPolicy configuration ensures that they don't run in parallel for the
// same same shard.
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
    bool EnqueueUpdate(DBEntryBase *db_entry,
                       RouteUpdate *rt_update,
                       UpdateList *uplist = NULL);
    void DequeueUpdate(RouteUpdate *rt_update);


    // Merge this new update into the existing state.
    bool MergeUpdate(DBEntryBase *db_entry, RouteUpdate *rt_update);

    // Cancel scheduled updates for the route and/or remove any current
    // advertised state.
    void ClearPeerSetCurrentAndScheduled(DBEntryBase *db_entry,
                                         const RibPeerSet &mleave);

    // Used by the update dequeue process to retrieve an update.
    RouteUpdatePtr GetNextUpdate(int queue_id, UpdateEntry *upentry);

    // Used by the update dequeue process to retrieve the next entry in the
    // queue. If this is an update, it returns the pointer.
    RouteUpdatePtr GetNextEntry(int queue_id, UpdateEntry *upentry,
                                UpdateEntry **next_upentry_p);

    // Used when iterating through updates with the same attribute.
    RouteUpdatePtr GetAttrNext(int queue_id, UpdateInfo *current_uinfo,
                               UpdateInfo **next_uinfo_p);

    void SetEntryState(DBEntryBase *db_entry, DBState *dbstate);
    void ClearEntryState(DBEntryBase *db_entry);

private:
    // Helper functions for GetRouteStateAndDequeue
    DBState *GetRouteUpdateAndDequeue(DBEntryBase *db_entry,
                                      RouteUpdate *rt_update,
                                      UpdateCmp cmp, bool *duplicate);
    DBState *GetUpdateListAndDequeue(DBEntryBase *db_entry,
                                     UpdateList *uplist);

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

    RibOut *ribout_;
    QueueVec *queue_vec_;

    DISALLOW_COPY_AND_ASSIGN(RibUpdateMonitor);
};

#endif  // SRC_BGP_BGP_UPDATE_MONITOR_H_
