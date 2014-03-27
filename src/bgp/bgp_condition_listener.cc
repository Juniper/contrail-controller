/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_condition_listener.h"

#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"

#include "db/db_table_partition.h"
#include "db/db_table_walker.h"

//
// Helper class to maintain the WalkRequests
// Contains a map of ConditionMatch object and RequestComplete callback
// pending_walk_list_: List of ConditionObjects requesting walk(not completed)
// current_walk_list_: List of ConditionObjects for which current walk is done
//
class WalkRequest {
public:
    typedef std::map<ConditionMatchPtr, 
            BgpConditionListener::RequestDoneCb> WalkList;

    WalkRequest();

    void AddMatchObject(ConditionMatch *obj, 
                        BgpConditionListener::RequestDoneCb cb) {
        std::pair<WalkList::iterator, bool> ret = 
            pending_walk_list_.insert(std::make_pair(obj, cb));
        if (!ret.second) {
            if (ret.first->second.empty()) {
                ret.first->second = cb;
            }
        }
    }

    // 
    // When the walk actually starts, pending_walk_list_ entries are moved to 
    // current_walk_list_.
    //
    void WalkStarted(DBTableWalker::WalkId id) {
        id_ = id;
        pending_walk_list_.swap(current_walk_list_);
        pending_walk_list_.clear();
    }

    DBTableWalker::WalkId GetWalkId() const {
        return id_;
    }

    void ResetWalkId() {
        id_ = DBTableWalker::kInvalidWalkerId;
    }

    bool walk_in_progress() {
        return (id_ != DBTableWalker::kInvalidWalkerId);
    }

    //
    // Table requires further walk as requests are cached in pending_walk_list_
    // during the current table walk
    //
    bool walk_again() {
        return !pending_walk_list_.empty();
    }

    WalkList *walk_list() {
        return &current_walk_list_;
    }

    bool is_walk_pending(ConditionMatch *obj) {
        if (pending_walk_list_.empty()) {
            return false;
        } else {
            return (pending_walk_list_.find(ConditionMatchPtr(obj)) != 
                    pending_walk_list_.end());
        }
    }
private:
    WalkList pending_walk_list_;
    WalkList current_walk_list_;
    DBTableWalker::WalkId id_;
};

//
// ConditionMatchTableState
// State managed by the BgpConditionListener for each of the table it is 
// listening to.
// BgpConditionListener registers for a DBTable when application request 
// for ConditionMatch
// BgpConditionListener unregisters from the DBTable when application removes
// the ConditionMatch and all table walks have finished
// Holds a table reference to ensure that table with active walk or listener
// is not deleted
//
class ConditionMatchTableState {
public:
    typedef std::set<ConditionMatchPtr> MatchList;
    ConditionMatchTableState(BgpTable *table, DBTableBase::ListenerId id);
    ~ConditionMatchTableState();

    void ManagedDelete() {
    }

    DBTableBase::ListenerId GetListenerId() const {
        return id_;
    }

    MatchList *match_objects() {
        return &match_object_list_;
    }

    void AddMatchObject(ConditionMatch *obj) {
        match_object_list_.insert(ConditionMatchPtr(obj));
    }

    //
    // Mutex required to manager MatchState list for concurrency
    //
    tbb::mutex &table_state_mutex() {
        return table_state_mutex_;;
    }

private:
    tbb::mutex table_state_mutex_;
    DBTableBase::ListenerId id_;
    MatchList match_object_list_;
    LifetimeRef<ConditionMatchTableState> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(ConditionMatchTableState);
};

BgpConditionListener::BgpConditionListener(BgpServer *server) : 
    server_(server), 
    walk_trigger_(new TaskTrigger(boost::bind(&BgpConditionListener::StartWalk, 
                                              this), 
                  TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)) {
}

//
// AddMatchCondition:
// API to add ConditionMatch object against a table
// All entries present in this table will be matched for this ConditionMatch
// object.[All table partition]
// Match is done either in TableWalk or During Table entry notification
//
void BgpConditionListener::AddMatchCondition(BgpTable *table, 
                                             ConditionMatch *obj, 
                                             RequestDoneCb cb) {
    CHECK_CONCURRENCY("bgp::Config");

    ConditionMatchTableState *ts = NULL;
    TableMap::iterator loc = map_.find(table);
    if (loc == map_.end()) {
        DBTableBase::ListenerId id = 
            table->Register(boost::bind(&BgpConditionListener::BgpRouteNotify, 
                                        this, server(), _1, _2));
        ts = new ConditionMatchTableState(table, id);
        map_.insert(std::make_pair(table, ts));
    } else {
        ts = loc->second;
    }
    ts->AddMatchObject(obj);
    TableWalk(table, obj, cb);
}


//
// RemoveMatchCondition:
// API to Remove ConditionMatch object from a table
// All entries present in this table will be matched for this ConditionMatch
// object[All table partition] and notified to application with "DELETE" flag
// All Match notifications after invoking this API is called with "DELETE" flag
//
void BgpConditionListener::RemoveMatchCondition(BgpTable *table, 
                                                ConditionMatch *obj, 
                                                RequestDoneCb cb) {
    CHECK_CONCURRENCY("bgp::Config");
    obj->SetDeleted();
    TableWalk(table, obj, cb);
}

//
// MatchState
// BgpConditionListener will hold this as DBState for each BgpRoute
//
class MatchState : public DBState {
public:
    typedef std::map<ConditionMatchPtr, 
            ConditionMatchState *> MatchStateList;
private:
    friend class BgpConditionListener;
    MatchStateList list_;
};

//
// GetMatchState
// API to fetch MatchState added by module registering ConditionMatch object
// MatchState is maintained as Map of ConditionMatch object and State in
// DBState added BgpConditionListener module
//
ConditionMatchState * BgpConditionListener::GetMatchState(BgpTable *table, 
                                                          BgpRoute *route,
                                                          ConditionMatch *obj) {
    TableMap::iterator loc = map_.find(table);
    ConditionMatchTableState *ts = loc->second;
    tbb::mutex::scoped_lock lock(ts->table_state_mutex());

    // Get the DBState
    MatchState *dbstate = 
        static_cast<MatchState *>(route->GetState(table, ts->GetListenerId()));
    if (dbstate == NULL) return NULL;

    // Index with ConditionMatch object to retrieve the MatchState
    MatchState::MatchStateList::iterator it = 
        dbstate->list_.find(ConditionMatchPtr(obj));
    return (it != dbstate->list_.end()) ? it->second : NULL;
}

//
// SetMatchState
// API for  module registering ConditionMatch object to add MatchState
//
void BgpConditionListener::SetMatchState(BgpTable *table, BgpRoute *route,
                                         ConditionMatch *obj, 
                                         ConditionMatchState *state) {
    TableMap::iterator loc = map_.find(table);
    ConditionMatchTableState *ts = loc->second;
    tbb::mutex::scoped_lock lock(ts->table_state_mutex());

    // Get the DBState
    MatchState *dbstate = 
        static_cast<MatchState *>(route->GetState(table, ts->GetListenerId()));

    if (!dbstate) {
        // Add new DBState when first application requests for MatchState
        dbstate = new MatchState();
        route->SetState(table, ts->GetListenerId(), dbstate);
    } else {
        // Add Match to the existing list
        MatchState::MatchStateList::iterator it = 
            dbstate->list_.find(ConditionMatchPtr(obj));
        assert(it == dbstate->list_.end());
    }
    dbstate->list_.insert(std::make_pair(obj, state));
    obj->IncrementNumMatchstate();
}

//
// RemoveMatchState
// Clear the module specific MatchState
//
void BgpConditionListener::RemoveMatchState(BgpTable *table, BgpRoute *route,
                                            ConditionMatch *obj) {
    TableMap::iterator loc = map_.find(table);
    ConditionMatchTableState *ts = loc->second;
    tbb::mutex::scoped_lock lock(ts->table_state_mutex());

    // Get the DBState
    MatchState *dbstate = 
        static_cast<MatchState *>(route->GetState(table, ts->GetListenerId()));

    MatchState::MatchStateList::iterator it = 
        dbstate->list_.find(ConditionMatchPtr(obj));
    assert(it != dbstate->list_.end());
    dbstate->list_.erase(it);
    obj->DecrementNumMatchstate();
    if (dbstate->list_.empty()) {
        // Remove the DBState when last module removes the MatchState
        route->ClearState(table, ts->GetListenerId());
        delete dbstate;
    }
}

//
// Add ConditionMatch object to pending_walk_list_ 
// and trigger the task to actually start the walk
//
void BgpConditionListener::TableWalk(BgpTable *table, ConditionMatch *obj,
                                     RequestDoneCb cb) {
    CHECK_CONCURRENCY("bgp::Config");
    WalkRequestMap::iterator loc = walk_map_.find(table);
    WalkRequest *walk_req = NULL;
    if (loc != walk_map_.end()) {
        walk_req = loc->second;
        walk_req->AddMatchObject(obj, cb);
    } else {
        walk_req = new WalkRequest();
        walk_map_.insert(std::make_pair(table, walk_req));
        walk_req->AddMatchObject(obj, cb);
    }
    walk_trigger_->Set();
}

bool BgpConditionListener::StartWalk() {
    CHECK_CONCURRENCY("bgp::Config");

    DBTableWalker::WalkCompleteFn walk_complete 
        = boost::bind(&BgpConditionListener::WalkDone, this, _1);

    DBTableWalker::WalkFn walker 
        = boost::bind(&BgpConditionListener::BgpRouteNotify, this, server(),
                      _1, _2);

    for(WalkRequestMap::iterator it = walk_map_.begin(); 
        it != walk_map_.end(); it++) {
        if (it->second->walk_in_progress()) {
            continue;
        }
        DB *db = server()->database();
        DBTableWalker::WalkId id = 
            db->GetWalker()->WalkTable(it->first, NULL, walker, walk_complete);
        it->second->WalkStarted(id);
    }
    return true;
}

// Table listener 
bool BgpConditionListener::BgpRouteNotify(BgpServer *server, 
                                          DBTablePartBase *root,
                                          DBEntryBase *entry) {
    BgpTable *bgptable = static_cast<BgpTable *>(root->parent());
    BgpRoute *rt = static_cast<BgpRoute *> (entry);
    bool del_rt = rt->IsDeleted();

    TableMap::iterator loc = map_.find(bgptable);
    assert(loc != map_.end());
    ConditionMatchTableState *ts = loc->second;

    DBTableBase::ListenerId id = ts->GetListenerId();
    assert(id != DBTableBase::kInvalidId);

    for(ConditionMatchTableState::MatchList::iterator match_obj_it = 
        ts->match_objects()->begin();
        match_obj_it != ts->match_objects()->end(); match_obj_it++) {
        bool deleted = false;
        if ((*match_obj_it)->deleted() || del_rt) {
            deleted = true;
        }
        (*match_obj_it)->Match(server, bgptable, rt, deleted);
    }
    return true;
}

// 
// WalkComplete function
// At the end of the walk reset the WalkId.
// Invoke the RequestDoneCb for all objects for which walk was started
// Clear the current_walk_list_ and check whether the table needs to be 
// walked again.
//
void BgpConditionListener::WalkDone(DBTableBase *table) {
    BgpTable *bgptable = static_cast<BgpTable *>(table);
    WalkRequestMap::iterator it = walk_map_.find(bgptable);
    assert (it != walk_map_.end());
    WalkRequest *walk_state = it->second;

    walk_state->ResetWalkId();

    //
    // Invoke the RequestDoneCb after the TableWalk
    //
    for(WalkRequest::WalkList::iterator walk_it = 
        walk_state->walk_list()->begin();
        walk_it != walk_state->walk_list()->end(); walk_it++) {
        // If application has registered a WalkDone callback, invoke it
        if (!walk_it->second.empty())
            walk_it->second(bgptable, walk_it->first.get());
    }

    walk_state->walk_list()->clear();

    if (walk_state->walk_again()) {
        // More walk requests are pending
        walk_trigger_->Set();
    } else {
        delete walk_state;
        walk_map_.erase(it);
    }
}

void BgpConditionListener::UnregisterCondition(BgpTable *bgptable, 
                                          ConditionMatch *obj) {
    TableMap::iterator loc = map_.find(bgptable);
    assert (loc != map_.end());
    ConditionMatchTableState *ts = loc->second;

    WalkRequestMap::iterator it = walk_map_.find(bgptable);
    WalkRequest *walk_state = NULL;
    if (it != walk_map_.end()) {
        walk_state = it->second;
    }

    //
    // Wait for Walk completion of deleted ConditionMatch object
    //
    if ((!walk_state || !walk_state->is_walk_pending(obj)) && 
        obj->deleted()) {
        ts->match_objects()->erase(obj);
    }

    if (ts->match_objects()->empty()) {
        bgptable->Unregister(ts->GetListenerId());
        map_.erase(bgptable);
        delete ts;
    }
}

ConditionMatchTableState::ConditionMatchTableState(BgpTable *table, 
                                                   DBTableBase::ListenerId id)
    : id_(id), table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

ConditionMatchTableState::~ConditionMatchTableState() {
}

WalkRequest::WalkRequest() : id_(DBTableWalker::kInvalidWalkerId) {
}
