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
    DBTableWalk(DBTable *table, DBTableWalkMgr::WalkFn walk_fn,
                DBTableWalkMgr::WalkCompleteFn walk_complete)
        : table_(table), walk_fn_(walk_fn), walk_complete_(walk_complete) {
        walk_stopped_ = false;
        walk_done_ = false;
        walk_again_ = false;
        refcount_ = 0;
    }

    DBTable *table() { return table_;}
    DBTableWalkMgr::WalkFn walk_fn() { return walk_fn_;}
    DBTableWalkMgr::WalkCompleteFn walk_complete() { return walk_complete_;}

    void set_walk_done() { walk_done_ = true;}
    bool done() { return walk_done_;}
    bool stopped() { return walk_stopped_;}
    bool in_progress() { return walk_in_progress_;}
    bool walk_again() { return walk_again_;}

private:
    friend class DBTableWalkMgr;

    friend void intrusive_ptr_add_ref(DBTableWalk *walker);
    friend void intrusive_ptr_release(DBTableWalk *walker);

    void set_walk_stopped() { walk_stopped_ = true;}
    void set_walk_again() { walk_again_ = true;}
    void reset_walk_again() { walk_again_ = false;}
    void set_in_progress() { walk_in_progress_ = true;}
    void reset_in_progress() { walk_in_progress_ = false;}

    DBTable *table_;
    DBTableWalkMgr::WalkFn walk_fn_;
    DBTableWalkMgr::WalkCompleteFn walk_complete_;
    bool walk_stopped_;
    tbb::atomic<bool> walk_done_;
    tbb::atomic<bool> walk_in_progress_;
    tbb::atomic<bool> walk_again_;
    tbb::atomic<int> refcount_;
};
#endif
