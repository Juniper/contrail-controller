/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_walk_mgr_h
#define ctrlplane_db_table_walk_mgr_h

#include <list>

#include <boost/assign.hpp>
#include <boost/function.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <tbb/mutex.h>
#include <tbb/task.h>

#include "base/logging.h"
#include "base/task_trigger.h"

class DBTableWalk;
class DBTable;
class DBTableBase;
class DBTablePartBase;
class DBEntryBase;

// A DB contains a TableWalker that is able to iterate though all the
// entries in a certain routing table.
class DBTableWalkMgr {
public:

    // Walker function:
    // Called for each DBEntry under a db::DBTable task that corresponds to the
    // specific partition.
    // arguments: DBTable partition and DBEntry.
    // returns: true (continue); false (stop).
    typedef boost::function<bool(DBTablePartBase *, DBEntryBase *)> WalkFn;

    // Called when all partitions are done iterating.
    typedef boost::function<void(DBTableBase *)> WalkCompleteFn;

    typedef boost::intrusive_ptr<DBTableWalk> DBTableWalkRef;

    typedef std::set<DBTableWalkRef> WalkReqList;

    static const int kIterationToYield = 1024;

    DBTableWalkMgr();

    // Create a DBTable Walker
    DBTableWalkRef AllocWalker(DBTable *table, WalkFn walk_fn,
                       WalkCompleteFn walk_complete);

    // Delete the Walker
    void ReleaseWalker(DBTableWalkRef &walk);

    // Start a walk on the table.
    void WalkTable(DBTableWalkRef walk);

    // Walk the table again
    void WalkAgain(DBTableWalkRef walk);

    void WalkDone();

    void DisableWalkProcessing() {
        walk_request_trigger_->set_disable();
    }

    void EnableWalkProcessing() {
        walk_request_trigger_->set_enable();
    }

    void DisableWalkDoneTrigger() {
        walk_done_trigger_->set_disable();
    }

    void EnableWalkDoneTrigger() {
        walk_done_trigger_->set_enable();
    }

private:
    struct WalkRequestInfo;
    typedef std::list<WalkRequestInfo *> WalkRequestInfoList;

    struct WalkRequestInfo {
        WalkRequestInfo(DBTable *table) : table(table) {
        }

        void AppendWalkReq(DBTableWalkRef ref) {
            pending_requests.insert(ref);
        }

        void DeleteWalkReq(DBTableWalkRef ref) {
            pending_requests.erase(ref);
        }

        bool WalkPending() {
            return !pending_requests.empty();
        }
        DBTable *table;
        WalkReqList pending_requests;
    };

    bool ProcessWalkRequestList();
    bool ProcessWalkDone();

    boost::scoped_ptr<TaskTrigger> walk_request_trigger_;
    boost::scoped_ptr<TaskTrigger> walk_done_trigger_;

    tbb::mutex mutex_;
    WalkRequestInfoList walk_request_list_;
    WalkReqList current_table_walk_;
};

class DBTableWalk {
public:
    enum WalkState {
        INIT = 1,
        WALK_REQUESTED = 2,
        WALK_IN_PROGRESS = 3,
        WALK_DONE = 4,
        WALK_STOPPED = 5,
    };

    DBTableWalk(DBTable *table, DBTableWalkMgr::WalkFn walk_fn,
                DBTableWalkMgr::WalkCompleteFn walk_complete)
        : table_(table), walk_fn_(walk_fn), walk_complete_(walk_complete) {
        walk_state_ = INIT;
        walk_again_ = false;
        refcount_ = 0;
    }

    DBTable *table() { return table_;}
    DBTableWalkMgr::WalkFn walk_fn() { return walk_fn_;}
    DBTableWalkMgr::WalkCompleteFn walk_complete() { return walk_complete_;}

    void set_walk_done() { walk_state_ = WALK_DONE;}

    bool requested() const { return (walk_state_ == WALK_REQUESTED);}
    bool in_progress() const { return (walk_state_ == WALK_IN_PROGRESS);}
    bool done() const { return (walk_state_ == WALK_DONE);}
    bool stopped() const { return (walk_state_ == WALK_STOPPED);}
    bool walk_again() const { return walk_again_;}
    bool walk_is_active() const {
        return ((walk_state_ == WALK_REQUESTED) ||
                (walk_state_ == WALK_IN_PROGRESS));
    }

    uint8_t walk_state() {
        return walk_state_;
    }

private:
    friend class DBTableWalkMgr;

    friend void intrusive_ptr_add_ref(DBTableWalk *walker);
    friend void intrusive_ptr_release(DBTableWalk *walker);

    void set_walk_again() { walk_again_ = true;}
    void reset_walk_again() { walk_again_ = false;}

    void set_walk_requested() { walk_state_ = WALK_REQUESTED;}
    void set_in_progress() { walk_state_ = WALK_IN_PROGRESS;}
    void set_walk_stopped() { walk_state_ = WALK_STOPPED;}

    DBTable *table_;
    DBTableWalkMgr::WalkFn walk_fn_;
    DBTableWalkMgr::WalkCompleteFn walk_complete_;
    tbb::atomic<uint8_t> walk_state_;
    tbb::atomic<bool> walk_again_;
    tbb::atomic<int> refcount_;
};
#endif
