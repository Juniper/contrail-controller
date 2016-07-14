/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_condition_listener.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <utility>

#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "db/db_table_partition.h"
#include "db/db_table_walk_mgr.h"

using std::make_pair;
using std::map;
using std::pair;
using std::set;

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
    typedef set<ConditionMatchPtr> MatchList;
    typedef map<ConditionMatchPtr,
            BgpConditionListener::RequestDoneCb> WalkList;
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

    void StoreDoneCb(ConditionMatch *obj,
                     BgpConditionListener::RequestDoneCb cb) {
        pair<WalkList::iterator, bool> ret =
            walk_list_.insert(make_pair(obj, cb));
        if (!ret.second) {
            if (ret.first->second.empty()) {
                ret.first->second = cb;
            }
        }
    }

    // Mutex required to manager MatchState list for concurrency
    tbb::mutex &table_state_mutex() {
        return table_state_mutex_;
    }

    void set_walk_ref(DBTable::DBTableWalkRef walk_ref) {
        walk_ref_ = walk_ref;
    }

    const DBTable::DBTableWalkRef &walk_ref() const {
        return walk_ref_;
    }

    DBTable::DBTableWalkRef &walk_ref() {
        return walk_ref_;
    }

    WalkList *walk_list() {
        return &walk_list_;
    }

    BgpTable *table() const {
        return table_;
    }

private:
    tbb::mutex table_state_mutex_;
    BgpTable *table_;
    DBTableBase::ListenerId id_;
    DBTable::DBTableWalkRef walk_ref_;
    WalkList walk_list_;
    MatchList match_object_list_;
    LifetimeRef<ConditionMatchTableState> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(ConditionMatchTableState);
};

BgpConditionListener::BgpConditionListener(BgpServer *server) :
    server_(server),
    purge_trigger_(new TaskTrigger(
                  boost::bind(&BgpConditionListener::PurgeTableState, this),
                  TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)) {
}

bool BgpConditionListener::PurgeTableState() {
    CHECK_CONCURRENCY("bgp::Config");
    BOOST_FOREACH(ConditionMatchTableState *ts, purge_list_) {
        if (ts->match_objects()->empty()) {
            BgpTable *bgptable = ts->table();
            if (ts->walk_ref() != NULL)
                bgptable->ReleaseWalker(ts->walk_ref());
            bgptable->Unregister(ts->GetListenerId());
            map_.erase(bgptable);
            delete ts;
        }
    }
    purge_list_.clear();

    return true;
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
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    tbb::mutex::scoped_lock lock(mutex_);
    ConditionMatchTableState *ts = NULL;
    TableMap::iterator loc = map_.find(table);
    if (loc == map_.end()) {
        DBTableBase::ListenerId id =
            table->Register(
                boost::bind(&BgpConditionListener::BgpRouteNotify,
                    this, server(), _1, _2),
                "BgpConditionListener");
        ts = new ConditionMatchTableState(table, id);
        map_.insert(make_pair(table, ts));
    } else {
        ts = loc->second;
    }
    ts->AddMatchObject(obj);
    TableWalk(ts, obj, cb);
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
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    tbb::mutex::scoped_lock lock(mutex_);
    obj->SetDeleted();

    TableMap::iterator loc = map_.find(table);
    assert(loc != map_.end());
    TableWalk(loc->second, obj, cb);
}

//
// MatchState
// BgpConditionListener will hold this as DBState for each BgpRoute
//
class MatchState : public DBState {
public:
    typedef map<ConditionMatchPtr, ConditionMatchState *> MatchStateList;

private:
    friend class BgpConditionListener;
    MatchStateList list_;
};

//
// CheckMatchState
// API to check if MatchState is added by module registering ConditionMatch
// object.
//
bool BgpConditionListener::CheckMatchState(BgpTable *table, BgpRoute *route,
                                           ConditionMatch *obj) {
    TableMap::iterator loc = map_.find(table);
    ConditionMatchTableState *ts = loc->second;
    tbb::mutex::scoped_lock lock(ts->table_state_mutex());

    // Get the DBState.
    MatchState *dbstate =
        static_cast<MatchState *>(route->GetState(table, ts->GetListenerId()));
    if (dbstate == NULL)
        return false;

    // Index with ConditionMatch object to check.
    MatchState::MatchStateList::iterator it =
        dbstate->list_.find(ConditionMatchPtr(obj));
    return (it != dbstate->list_.end()) ? true : false;
}

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
    dbstate->list_.insert(make_pair(obj, state));
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

void BgpConditionListener::TableWalk(ConditionMatchTableState *ts,
                 ConditionMatch *obj, BgpConditionListener::RequestDoneCb cb) {
    CHECK_CONCURRENCY("bgp::Config", "bgp::ConfigHelper");

    if (ts->walk_ref() == NULL) {
        DBTable::DBTableWalkRef walk_ref = ts->table()->AllocWalker(
            boost::bind(&BgpConditionListener::BgpRouteNotify,
                this, server(), _1, _2),
            boost::bind(&BgpConditionListener::WalkDone,
                this, ts, _2));
        ts->set_walk_ref(walk_ref);
    }
    ts->StoreDoneCb(obj, cb);
    obj->reset_walk_done();
    ts->table()->WalkTable(ts->walk_ref());
}

// Table listener
bool BgpConditionListener::BgpRouteNotify(BgpServer *server,
                                          DBTablePartBase *root,
                                          DBEntryBase *entry) {
    BgpTable *bgptable = static_cast<BgpTable *>(root->parent());
    BgpRoute *rt = static_cast<BgpRoute *> (entry);
    // Either the route is deleted or no valid path exists
    bool del_rt = !rt->IsUsable();

    TableMap::iterator loc = map_.find(bgptable);
    assert(loc != map_.end());
    ConditionMatchTableState *ts = loc->second;

    DBTableBase::ListenerId id = ts->GetListenerId();
    assert(id != DBTableBase::kInvalidId);

    for (ConditionMatchTableState::MatchList::iterator match_obj_it =
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
// WalkComplete is invoked only after all walk requests for BgpConditionListener
// is served. (say after multiple walkagain, only one WalkDone is invoked)
// Invoke the RequestDoneCb for all objects for which walk was started
// Clear the walk_list_
//
void BgpConditionListener::WalkDone(ConditionMatchTableState *ts,
                                    DBTableBase *table) {
    BgpTable *bgptable = static_cast<BgpTable *>(table);

    // Invoke the RequestDoneCb after the TableWalk
    for (ConditionMatchTableState::WalkList::iterator walk_it =
         ts->walk_list()->begin();
         walk_it != ts->walk_list()->end(); ++walk_it) {
        walk_it->first->set_walk_done();
        // If application has registered a WalkDone callback, invoke it
        if (!walk_it->second.empty()) {
            walk_it->second(bgptable, walk_it->first.get());
        }
    }

    ts->walk_list()->clear();
}

void BgpConditionListener::UnregisterMatchCondition(BgpTable *bgptable,
                                          ConditionMatch *obj) {
    TableMap::iterator loc = map_.find(bgptable);
    assert(loc != map_.end());
    ConditionMatchTableState *ts = loc->second;

    // Wait for Walk completion of deleted ConditionMatch object
    if (obj->deleted() && obj->walk_done()) {
        ts->match_objects()->erase(obj);
        purge_list_.insert(ts);
    }
    purge_trigger_->Set();
}

void BgpConditionListener::DisableTableWalkProcessing() {
    DBTableWalkMgr *walk_mgr = server()->database()->GetWalkMgr();
    walk_mgr->DisableWalkProcessing();
}

void BgpConditionListener::EnableTableWalkProcessing() {
    DBTableWalkMgr *walk_mgr = server()->database()->GetWalkMgr();
    walk_mgr->EnableWalkProcessing();
}

ConditionMatchTableState::ConditionMatchTableState(BgpTable *table,
                                                   DBTableBase::ListenerId id)
    : table_(table), id_(id), table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

ConditionMatchTableState::~ConditionMatchTableState() {
}
