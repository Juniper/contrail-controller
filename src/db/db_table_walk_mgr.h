/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_walk_mgr_h
#define ctrlplane_db_table_walk_mgr_h

#include <list>

#include <boost/assign.hpp>
#include <boost/function.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <tbb/mutex.h>
#include <tbb/task.h>

#include "base/logging.h"
#include "base/task_trigger.h"
#include "base/util.h"

class DBTableWalk;
class DBTable;
class DBTableBase;
class DBTablePartBase;
class DBEntryBase;

// A DB contains a TableWalker that is able to iterate though all the
// entries in a certain routing table.
class DBTableWalkMgr {
public:
    typedef boost::intrusive_ptr<DBTableWalk> DBTableWalkRef;
    typedef boost::function<bool(DBTablePartBase *, DBEntryBase *)> WalkFn;
    typedef boost::function<void(DBTableWalkRef, DBTableBase *)> WalkCompleteFn;

    typedef std::set<DBTableWalkRef> WalkReqList;

    DBTableWalkMgr();

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
    friend class DBTable;
    struct WalkRequestInfo;
    typedef std::list<boost::shared_ptr<WalkRequestInfo> > WalkRequestInfoList;

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

    // Create a DBTable Walker
    DBTableWalkRef AllocWalker(DBTable *table, WalkFn walk_fn,
                       WalkCompleteFn walk_complete);

    // Release the Walker
    void ReleaseWalker(DBTableWalkRef &walk);

    // Start a walk on the table.
    void WalkTable(DBTableWalkRef walk);

    // Walk the table again
    void WalkAgain(DBTableWalkRef walk);

    bool ProcessWalkRequestList();

    bool ProcessWalkDone();

    bool InvokeWalkCb(DBTablePartBase *part, DBEntryBase *entry);

    boost::scoped_ptr<TaskTrigger> walk_request_trigger_;
    boost::scoped_ptr<TaskTrigger> walk_done_trigger_;

    tbb::mutex mutex_;
    WalkRequestInfoList walk_request_list_;
    WalkReqList current_table_walk_;

    DISALLOW_COPY_AND_ASSIGN(DBTableWalkMgr);
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

    DBTable *table() const { return table_;}
    DBTableWalkMgr::WalkFn walk_fn() const { return walk_fn_;}
    DBTableWalkMgr::WalkCompleteFn walk_complete() const { return walk_complete_;}

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

    WalkState walk_state() const {
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
    tbb::atomic<WalkState> walk_state_;
    tbb::atomic<bool> walk_again_;
    tbb::atomic<int> refcount_;

    DISALLOW_COPY_AND_ASSIGN(DBTableWalk);
};
#endif
