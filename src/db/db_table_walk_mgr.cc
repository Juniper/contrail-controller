/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_table_walk_mgr.h"

#include <tbb/atomic.h>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

DBTableWalkMgr::DBTableWalkMgr()
    : walk_request_trigger_(new TaskTrigger(
        boost::bind(&DBTableWalkMgr::ProcessWalkRequestList, this),
        TaskScheduler::GetInstance()->GetTaskId("db::Walker"),
        0)),
      walk_done_trigger_(new TaskTrigger(
        boost::bind(&DBTableWalkMgr::ProcessWalkDone, this),
        TaskScheduler::GetInstance()->GetTaskId("db::Walker"), 0)) {
}

bool DBTableWalkMgr::ProcessWalkRequestList() {
    if (!current_table_walk_.empty()) return true;
    if (walk_request_list_.empty()) return true;
    WalkRequestInfo *info = walk_request_list_.front();
    walk_request_list_.pop_front();
    DBTable *table = info->table;
    current_table_walk_ = info->pending_requests;
    BOOST_FOREACH(DBTableWalkRef walker, current_table_walk_) {
        walker->set_in_progress();
        walker->reset_walk_again();
    }
    // start the walk
    table->WalkTable(current_table_walk_);
    delete info;
    return true;
}

bool DBTableWalkMgr::ProcessWalkDone() {
    assert(!current_table_walk_.empty());
    BOOST_FOREACH(DBTableWalkRef walker, current_table_walk_) {
        if (walker->walk_again())
            walker->set_walk_requested();
        else
            walker->set_walk_done();
        if (walker->stopped() || walker->walk_again()) continue;
        walker->walk_complete()(walker->table());
    }
    current_table_walk_.clear();
    ProcessWalkRequestList();
    return true;
}

DBTableWalkMgr::DBTableWalkRef DBTableWalkMgr::AllocWalker(DBTable *table,
                               WalkFn walk_fn, WalkCompleteFn walk_complete) {
    table->incr_walker_count();
    DBTableWalk *walker = new DBTableWalk(table, walk_fn, walk_complete);
    return DBTableWalkRef(walker);
}

void DBTableWalkMgr::ReleaseWalker(DBTableWalkMgr::DBTableWalkRef &ref) {
    ref->set_walk_stopped();
    if (!ref->in_progress()) {
        DBTable *table = ref->table();
        tbb::mutex::scoped_lock lock(mutex_);
        for (WalkRequestInfoList::iterator it = walk_request_list_.begin();
             it != walk_request_list_.end(); it++) {
            WalkRequestInfo *info = *it;
            if (info->table == table) {
                info->DeleteWalkReq(ref);
                if (!info->WalkPending()) {
                    walk_request_list_.erase(it);
                    delete info;
                }
                break;
            }
        }
    }
    ref.reset();
}

void DBTableWalkMgr::WalkAgain(DBTableWalkRef ref) {
    WalkTable(ref);
}

void DBTableWalkMgr::WalkTable(DBTableWalkRef walk) {
    tbb::mutex::scoped_lock lock(mutex_);
    DBTable *table = walk->table();

    if (walk->in_progress())
        walk->set_walk_again();
    else
        walk->set_walk_requested();

    BOOST_FOREACH(WalkRequestInfo *info, walk_request_list_) {
        if (info->table == table) {
            info->AppendWalkReq(walk);
            return;
        }
    }

    WalkRequestInfo *new_info = new WalkRequestInfo(table);
    new_info->AppendWalkReq(walk);
    walk_request_list_.push_back(new_info);
    walk_request_trigger_->Set();
}

void DBTableWalkMgr::WalkDone() {
    walk_done_trigger_->Set();
}

void intrusive_ptr_add_ref(DBTableWalk *walker) {
    walker->refcount_.fetch_and_increment();
}

void intrusive_ptr_release(DBTableWalk *walker) {
    int prev = walker->refcount_.fetch_and_decrement();
    if (prev == 1) {
        DBTable *table = walker->table();
        delete walker;
        table->decr_walker_count();
        table->RetryDelete();
    }
}
