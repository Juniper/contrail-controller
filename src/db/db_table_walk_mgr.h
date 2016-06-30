/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_walk_mgr_h
#define ctrlplane_db_table_walk_mgr_h

#include <list>
#include <set>

#include <boost/assign.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <tbb/mutex.h>
#include <tbb/task.h>

#include "base/logging.h"
#include "base/task_trigger.h"
#include "base/util.h"

#include "db/db_table.h"

//
// DBTableWalkMgr:
// ==============
// DBTableWalkMgr provides infrastructure to walk DBTable.
//
// DBTable class provides API for walking DBTable. Following APIs are provided
//
//    1. AllocWalker: This API allocates a walk handle and returns to the caller
//      Application is suppose to release the walker using ReleaseWalker.
//
//      AllocWalker API can be called from any task context.
//
//      Application provides two callback function as input to for this API.
//
//      DBTable::WalkFn : Callback invoked by walk infra while traversing each
//           DBEntry in DBTable. This API is invoked in db::DBTable task context
//           by default. Application can configure the task id in which
//           DBTable walk is performed using DBTable::SetWalkTaskId
//
//      DBTable::WalkCompleteFn: Callback invoked by walk infra on completion of
//           ongoing walk request. This API is invoked in db::Walker task
//           context irrespective of the task id set with DBTable::SetWalkTaskId
//           This callback is not invoked in cases where application
//           calls ReleaseWalker on ongoing walker. Also, WalkCompleteFn is
//           called only after handling all WalkAgain requests on the walker.
//
//    2. ReleaseWalker: This API releases the walker. After invoking this API,
//    application will stop getting Walk callback for the ongoing Walk request.
//    Application should not refer to the DBTableWalkRef after invoking this API
//
//    ReleaseWalker API can be called from any task context.
//
//    3. WalkTable: This API starts the table walk. This API should be called
//    from a task which is mutually exclusive from db::Walker task.
//
//    4. WalkAgain: To re-trigger/restart current walk request from application.
//    Callback for WalkFn is no longer invoked for ongoing walk and walk is
//    restarted from beginning of DBTable. This API should be called from a task
//    which is mutually exclusive from db::Walker task.
//
// DBTableWalkMgr ensures that not more than one DBTable is walked at any point
// in time. All other DBTable walk requests are queued and taken up only after
// current walk completes.
// Actual DBTable walk (i.e. iterating the DBTablePartition) is performed in
// db::DBTable task or task id configured with DBTable::SetWalkTaskId with
// instance id set as partition index.
// The advantage of running DBTable walk in serial manner is in clubbing
// multiple walk requests on a given table and serving such requests in one
// iteration of DBTable walk
//
// WalkReqList holds list of DBTableWalkRef(i.e. walkers created by multiple
// application modules) that requested for DBTable walk on a specific table.
// InvokeWalkCb notifies all such walkers stored in current_table_walk_, while
// iterating through DBTable entries
//
// WalkRequestInfo:
// ===============
// WalkRequestInfo is a per table Walk request structure. It also holds
// DBTableWalkRef which requested for DBTable walk.
//
// WalkRequestInfoList
// ===================
// walk_request_list_ holds list of WalkRequestInfo. This list is keyed by
// DBTable. Additional walk_request_set_ is maintained for easy search of
// WalkRequestInfo for a given DBTable.
// Current table on which walk is going on will not be present in the
// walk_request_list_. If caller requests for WalkAgain(), it is added back to
// the walk_request_list_ (in the end of the list).
//
// Task Triggers:
// walk_request_trigger_ : Task trigger which evaluate walk_request_list_.
// It removes the WalkRequestInfo on top of this list and starts walk on the
// table. This task trigger runs in "db::Walker" task context.
//
// walk_done_trigger_ : Task trigger ensures that WalkCompleteFn is triggered
// in db::Walker task context for all DBTableWalkRef which requested for
// current DBTable walk. At the end of ProcessWalkDone, walk_request_trigger_ is
// triggered to evaluate walk request from top of walk_request_list_.
//
class DBTableWalkMgr {
public:
    DBTableWalkMgr();

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
    typedef std::set<DBTable::DBTableWalkRef> WalkReqList;

    struct WalkRequestInfo {
        WalkRequestInfo(DBTable *table) : table(table) {
        }

        void AppendWalkReq(DBTable::DBTableWalkRef ref) {
            pending_requests.insert(ref);
        }

        void DeleteWalkReq(DBTable::DBTableWalkRef ref) {
            pending_requests.erase(ref);
        }

        bool WalkPending() {
            return !pending_requests.empty();
        }
        DBTable *table;
        WalkReqList pending_requests;
    };

    struct WalkRequestCompare {
        bool operator()(const WalkRequestInfo *lhs,
                        const WalkRequestInfo *rhs) {
            return lhs->table < rhs->table;
        }
    };
    typedef boost::shared_ptr<WalkRequestInfo> WalkRequestInfoPtr;
    typedef std::list<WalkRequestInfoPtr> WalkRequestInfoList;
    typedef std::set<WalkRequestInfo *, WalkRequestCompare> WalkRequestInfoSet;

    // Create a DBTable Walker
    DBTable::DBTableWalkRef AllocWalker(DBTable *table, DBTable::WalkFn walk_fn,
                       DBTable::WalkCompleteFn walk_complete);

    // Release the Walker
    void ReleaseWalker(DBTable::DBTableWalkRef &walk);

    // Start a walk on the table.
    void WalkTable(DBTable::DBTableWalkRef walk);

    // DBTable finished walking
    void WalkDone();

    // Walk the table again
    void WalkAgain(DBTable::DBTableWalkRef walk);

    bool ProcessWalkRequestList();

    bool ProcessWalkDone();

    bool InvokeWalkCb(DBTablePartBase *part, DBEntryBase *entry);

    boost::scoped_ptr<TaskTrigger> walk_request_trigger_;
    boost::scoped_ptr<TaskTrigger> walk_done_trigger_;

    // Mutex to protect walk_request_list_ and walk_request_set_ as
    // Walk can be requested from task which may run concurrently
    tbb::mutex mutex_;
    WalkRequestInfoList walk_request_list_;
    WalkRequestInfoSet walk_request_set_;

    WalkReqList current_table_walk_;

    DISALLOW_COPY_AND_ASSIGN(DBTableWalkMgr);
};

#endif
