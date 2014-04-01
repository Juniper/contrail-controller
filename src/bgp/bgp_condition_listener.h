/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_condition_listener_h
#define ctrlplane_bgp_condition_listener_h

#include <map>
#include <set>

#include <boost/intrusive_ptr.hpp>

#include <tbb/mutex.h>

#include "base/task_trigger.h"

#include "bgp/bgp_table.h"
#include "bgp/bgp_route.h"
#include "db/db_table_partition.h"
// 
// ConditionMatch
// Base class for ConditionMatch 
// Provides interface to match a implementation specific match condition and 
// action function
//
class ConditionMatch {
public:
    ConditionMatch() : deleted_(false), num_matchstate_(0) {
        refcount_ = 0;
    }

    virtual ~ConditionMatch() {
    }

    // Performs the Match and action function. 
    // Runs in the db::DBTable task context
    // Actual action can be performed in different task context
    // Concurrency: DB:DBTable task either from DB Notification context or 
    // from DB Walk context
    virtual bool Match(BgpServer *server, BgpTable *table, 
                       BgpRoute *route, bool deleted) = 0;

    bool deleted() {
        return deleted_;
    }

    void IncrementNumMatchstate() {
        tbb::mutex::scoped_lock lock(mutex_);
        num_matchstate_++;
    }

    void DecrementNumMatchstate() {
        assert(num_matchstate_);
        num_matchstate_--;
    }

    uint32_t num_matchstate() const {
        return num_matchstate_;
    }

private:
    friend class BgpConditionListener;
    friend void intrusive_ptr_add_ref(ConditionMatch *match);
    friend void intrusive_ptr_release(ConditionMatch *match);
    void SetDeleted() {
        deleted_ = true;
    }
    bool deleted_;

    tbb::mutex mutex_;
    uint32_t num_matchstate_;

    tbb::atomic<int> refcount_;
};

inline void intrusive_ptr_add_ref(ConditionMatch *match) {
    match->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(ConditionMatch *match) {
    int prev = match->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete match;
    }
}

//
// Intrusive Pointer for life time management of Condition Match object
//
typedef boost::intrusive_ptr<ConditionMatch> ConditionMatchPtr;

//
// ConditionMatchState
// Base class for meta data that Condition Match adds against BgpRoute
//
class ConditionMatchState {
public:
    ConditionMatchState() : refcount_(0), deleted_(false) {
    }
    virtual ~ConditionMatchState() {
    }
    uint32_t refcnt() const {
        return refcount_;
    }
    void IncrementRefCnt() {
        refcount_++;
    }
    void set_deleted() {
        deleted_ = true;
    }
    void reset_deleted() {
        deleted_ = false;
    }
    bool deleted() const {
        return deleted_;
    }
    void DecrementRefCnt() {
        assert(refcount_);
        refcount_--;
    }
private:
    uint32_t refcount_;
    bool deleted_;
};

// 
// Helper classes
//

//
// Store the state managed by BgpConditionListener against each table 
// it is registered
// 
class ConditionMatchTableState;

//
// Store the Walk request and current walk state for each BgpTable with active
// Walk request
//
class WalkRequest;

//
// BgpConditionListener
// Provides a generic interface to match a condition and call action function
// Application module registers with this module with ConditionMatch class
// to start applying the match condition on all BgpRoutes
// Provides an interface to add/remove module specific data on each BgpRoute
//
class BgpConditionListener {
public:
    typedef std::map<BgpTable *, ConditionMatchTableState *> TableMap;
    typedef std::map<BgpTable *, WalkRequest *> WalkRequestMap;

    // Called upon completion of Add or Delete operations
    typedef boost::function<void(BgpTable *, ConditionMatch *)> RequestDoneCb;

    BgpConditionListener(BgpServer *server);

    // Add a new match condition
    // All subsequent DB Table notification matches this condition
    // DB Table is walked to match this condition for existing entries
    void AddMatchCondition(BgpTable *table, ConditionMatch *obj, 
                           RequestDoneCb addDoneCb);

    // Delete a match condition
    // DB table should be walked to match this deleting condition and 
    // revert the action taken on previous match
    // DeleteDone callback indicates the calling application about Remove 
    // completion. Application should call UnregisterCondition to 
    // remove the ConditionMatch object from the table
    void RemoveMatchCondition(BgpTable *table, ConditionMatch *obj, 
                              RequestDoneCb deleteDonecb);

    // Return the meta-data added by the module requested for Match
    ConditionMatchState *GetMatchState(BgpTable *table, BgpRoute *route,
                                         ConditionMatch *obj);

    // Set the module specific meta-data after the match/action
    void SetMatchState(BgpTable *table, BgpRoute *route, 
                       ConditionMatch *obj, 
                       ConditionMatchState *state);

    // Clear the module specific DBState
    void RemoveMatchState(BgpTable *table, BgpRoute *route, 
                          ConditionMatch *obj);

    // API to remove condition object from the table
    void UnregisterCondition(BgpTable *table, ConditionMatch *obj);

    BgpServer *server() {
        return server_;
    }

private:
    BgpServer *server_;

    TableMap map_;

    WalkRequestMap walk_map_;

    // Table listener 
    bool BgpRouteNotify(BgpServer *server, DBTablePartBase *root,
                        DBEntryBase *entry);

    void TableWalk(BgpTable *table, ConditionMatch *obj, RequestDoneCb cb);

    bool StartWalk();

    // WalkComplete function
    void WalkDone(DBTableBase *table);

    boost::scoped_ptr<TaskTrigger> walk_trigger_;

    DISALLOW_COPY_AND_ASSIGN(BgpConditionListener);
};
#endif // ctrlplane_bgp_condition_listener_h
