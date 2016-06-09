/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_table_walk_mgr.h"

#include <tbb/atomic.h>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

DBTableWalkMgr::DBTableWalkMgr()
    : walk_request_trigger_(new TaskTrigger(
        boost::bind(&DBTableWalkMgr::ProcessWalkRequestList, this),
        TaskScheduler::GetInstance()->GetTaskId("db::Walker"), 0)),
      walk_done_trigger_(new TaskTrigger(
        boost::bind(&DBTableWalkMgr::ProcessWalkDone, this),
        TaskScheduler::GetInstance()->GetTaskId("db::Walker"), 0)) {
}

bool DBTableWalkMgr::ProcessWalkRequestList() {
    CHECK_CONCURRENCY("db::Walker");
    if (!current_table_walk_.empty()) return true;
    while (true) {
        if (walk_request_list_.empty()) break;
        boost::shared_ptr<WalkRequestInfo> info = walk_request_list_.front();
        walk_request_list_.pop_front();
        current_table_walk_.swap(info->pending_requests);
        DBTable *table = info->table;
        bool walk_table = false;
        BOOST_FOREACH(DBTableWalkRef walker, current_table_walk_) {
            if (walker->stopped()) continue;
            walker->set_in_progress();
            walker->reset_walk_again();
            walk_table = true;
        }
        if (walk_table) {
            // start the walk
            table->WalkTable();
            break;
        } else {
            current_table_walk_.clear();
        }
    }
    return true;
}

bool DBTableWalkMgr::ProcessWalkDone() {
    CHECK_CONCURRENCY("db::Walker");
    assert(!current_table_walk_.empty());
    BOOST_FOREACH(DBTableWalkRef walker, current_table_walk_) {
        if (walker->walk_again())
            walker->set_walk_requested();
        else
            walker->set_walk_done();
        if (walker->stopped() || walker->walk_again()) continue;
        walker->walk_complete()(walker, walker->table());
    }
    current_table_walk_.clear();
    walk_request_trigger_->Set();
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

    BOOST_FOREACH(boost::shared_ptr<WalkRequestInfo> info, walk_request_list_) {
        if (info->table == table) {
            info->AppendWalkReq(walk);
            return;
        }
    }

    WalkRequestInfo *new_info = new WalkRequestInfo(table);
    new_info->AppendWalkReq(walk);
    walk_request_list_.push_back(boost::shared_ptr<WalkRequestInfo>(new_info));
    walk_request_trigger_->Set();
}

void DBTableWalkMgr::WalkDone() {
    walk_done_trigger_->Set();
}

bool DBTableWalkMgr::InvokeWalkCb(DBTablePartBase *part, DBEntryBase *entry) {
    uint32_t skip_walk_count = 0;
    BOOST_FOREACH(DBTableWalkMgr::DBTableWalkRef walker, current_table_walk_) {
        if (walker->done() || walker->stopped()) {
            skip_walk_count++;
            continue;
        }
        bool more = walker->walk_fn()(part, entry);
        if (!more) {
            skip_walk_count++;
            walker->set_walk_done();
        }
    }
    return (skip_walk_count < current_table_walk_.size());
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
