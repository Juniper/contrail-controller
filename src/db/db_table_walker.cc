/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_table_walker.h"

#include <list>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "db/db.h"
#include "db/db_partition.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

using namespace tbb;

int DBTableWalker::walker_task_id_ = -1;

DBTableWalker::DBTableWalker() {
    if (walker_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        // Using same task id as DBPartition
        walker_task_id_ = scheduler->GetTaskId("db::DBTable");
    }
    walk_request_count_ = 0;
    walk_complete_count_ = 0;
    walk_cancel_count_ = 0;
}
class DBTableWalker::Walker {
public:
    Walker(WalkId id, DBTableWalker *wkmgr, DBTable *table,
           const DBRequestKey *key, WalkFn walker, 
           WalkCompleteFn walk_done);

    void StopWalk() {
        should_stop_.fetch_and_store(true);
    }

    WalkId  id_;

    // Parent walker manager
    DBTableWalker *wkmgr_;

    // Table on which walk is done
    DBTable *table_;

    // Take the ownership of key passed
    std::auto_ptr<DBRequestKey> key_start_;

    WalkFn walker_fn_;
    WalkCompleteFn done_fn_;

    // Will be true if Table walk is cancelled
    tbb::atomic<bool> should_stop_;

    // check whether iteraton is completed on all Table Partition
    tbb::atomic<long> status_;
};

class DBTableWalker::Worker : public Task {
public:
    Worker(Walker *walker, int db_partition_id, const DBRequestKey *key) 
        : Task(walker_task_id_, db_partition_id), walker_(walker), 
          key_start_(key) {
        tbl_partition_ = static_cast<DBTablePartition *>(
            walker_->table_->GetTablePartition(db_partition_id));
    }

    virtual bool Run();

private:
    DBTableWalker::Walker *walker_;

    // Store the last visited node to continue walk
    std::auto_ptr<DBRequestKey> walk_ctx_;

    // This is where the walk started
    const DBRequestKey *key_start_;

    // Table partition for which this worker was created
    DBTablePartition *tbl_partition_;
};

static void db_walker_wait() {
    static int walk_sleep_usecs_;
    static bool once;

    if (!once) {
        once = true;

        char *wait = getenv("DB_WALKER_WAIT_USECS");
        if (wait) walk_sleep_usecs_ = strtoul(wait, NULL, 0);
    }

    if (walk_sleep_usecs_) {
        usleep(walk_sleep_usecs_);
    }
}

bool DBTableWalker::Worker::Run() {
    int count = 0;
    DBRequestKey *key_resume;

    // Check whether Walker was requested to be cancelled
    if (walker_->should_stop_) {
        goto walk_done;
    }

    // Check where we left in last iteration
    if ((key_resume = walk_ctx_.get()) == NULL) {
        // First time invoke of worker thread, start from key_start_
        key_resume = const_cast <DBRequestKey *>(key_start_);
    }

    DBEntry *entry;
    if (key_resume != NULL) {
        DBTable *table = walker_->table_;
        std::auto_ptr<const DBEntryBase> start;
        start = table->AllocEntry(key_resume);
        // Find matching or next in sort order
        entry = tbl_partition_->lower_bound(start.get());
    } else {
        entry = tbl_partition_->GetFirst();
    }
    if (entry == NULL) {
        goto walk_done;
    }

    for (DBEntry *next = NULL; entry; entry = next) {
        next = tbl_partition_->GetNext(entry);
        // Check whether Walker was requested to be cancelled
        if (walker_->should_stop_) {
            break; 
        }
        if (count == GetIterationToYield()) {
            // store the context
            walk_ctx_ = entry->GetDBRequestKey();
            return false;
        }

        // Invoke walker function
        bool more = walker_->walker_fn_(tbl_partition_, entry);
        if (!more) {
            break;
        }

        db_walker_wait();
        count++;
    }

walk_done:
    // Check whether all other walks on the table is completed
    long num_walkers_on_tpart = walker_->status_.fetch_and_decrement();
    if (num_walkers_on_tpart == 1) {
        // Invoke Walker_Complete callback
        if (!walker_->should_stop_) {
            walker_->wkmgr_->update_walk_complete_count(+1);
        }
        if (walker_->done_fn_ != NULL) {
            if (!walker_->should_stop_) {
                walker_->done_fn_(walker_->table_);
            }
        }
        // Release the memory for walker and bitmap
        walker_->wkmgr_->PurgeWalker(walker_->id_);
    }
    return true;
}

DBTableWalker::Walker::Walker(WalkId id, DBTableWalker *wkmgr,
                              DBTable *table, const DBRequestKey *key,
                              WalkFn walker, WalkCompleteFn walk_done)
    : id_(id), wkmgr_(wkmgr), table_(table),
      key_start_(const_cast<DBRequestKey *>(key)), 
      walker_fn_(walker), done_fn_(walk_done) {
    int num_worker = DB::PartitionCount(); 
    should_stop_ = false;
    status_ = num_worker;
    for (int i = 0; i < num_worker; i++) {
        Worker *task = new Worker(this, i, key);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
    }
}

DBTableWalker::WalkId DBTableWalker::WalkTable(DBTable *table, 
                                               const DBRequestKey *key_start, 
                                               WalkFn walkerfn , 
                                               WalkCompleteFn walk_complete) {
    tbb::mutex::scoped_lock lock(walkers_mutex_);
    walk_request_count_++;
    size_t i = walker_map_.find_first();
    if (i == walker_map_.npos) {
        i = walkers_.size();
        Walker *walker = new Walker(i, this, table, key_start, 
                                    walkerfn, walk_complete);
        walkers_.push_back(walker);
    } else {
        walker_map_.reset(i);
        if (walker_map_.none()) {
            walker_map_.clear();
        }
        Walker *walker = new Walker(i, this, table, key_start, 
                                    walkerfn, walk_complete);
        walkers_[i] = walker;
    }
    return i;
}

void DBTableWalker::WalkCancel(WalkId id) {
    tbb::mutex::scoped_lock lock(walkers_mutex_);
    walk_cancel_count_++;
    walkers_[id]->StopWalk();
    // Purge to be called after task has stopped
}

void DBTableWalker::PurgeWalker(WalkId id) {
    tbb::mutex::scoped_lock lock(walkers_mutex_);
    Walker *walker = walkers_[id];
    delete walker;
    walkers_[id] = NULL;
    if ((size_t) id == walkers_.size() - 1) {
        while (!walkers_.empty() && walkers_.back() == NULL) {
            walkers_.pop_back();
        }
        if (walker_map_.size() > walkers_.size()) {
            walker_map_.resize(walkers_.size());
        }
    } else {
        if ((size_t) id >= walker_map_.size()) {
            walker_map_.resize(id + 1);
        }
        walker_map_.set(id);
    }
}
