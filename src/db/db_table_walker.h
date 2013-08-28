/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_table_walker_h
#define ctrlplane_db_table_walker_h

#include <boost/function.hpp>
#include <boost/dynamic_bitset.hpp>
#include <tbb/task.h>

#include "base/logging.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

// A DB contains a TableWalker that is able to iterate though all the
// entries in a certain routing table.
class DBTableWalker {
public:

    // Walker function:
    // Called for each DBEntry under a task that corresponds to the
    // specific partition.
    // arguments: DBTable partition and DBEntry.
    // returns: true (continue); false (stop).
    typedef boost::function<bool(DBTablePartBase *, DBEntryBase *)> WalkFn;

    // Called when all partitions are done iterating.
    typedef boost::function<void(DBTableBase *)> WalkCompleteFn;

    typedef int WalkId;

    static const WalkId kInvalidWalkerId = -1;

    // Start a walk request on the specified table. If non null, 'key_start'
    // specifies the starting point for the walk. The walk is performed in
    // all table shards in parallel.
    WalkId WalkTable(DBTable *table, const DBRequestKey *key_start,
                     WalkFn walker, WalkCompleteFn walk_complete);

    // cancel a walk that may be in progress. This cannot be called from
    // the walker function itself.
    void WalkCancel(WalkId id);

    DBTableWalker();

    uint64_t walk_request_count() { return walk_request_count_; }
    uint64_t walk_complete_count() { return walk_complete_count_; }
    void update_walk_complete_count(uint64_t inc) {
        tbb::mutex::scoped_lock lock(walkers_mutex_);
        walk_complete_count_ += inc;
    }
    uint64_t walk_cancel_count() { return walk_cancel_count_; }

private:
    static const int kIterationToYield = 1024;

    static const int GetIterationToYield() {
        static int iter_ = kIterationToYield;
        static bool init_ = false;

        if (!init_) {

            // XXX To be used for testing purposes only.
            char *count_str = getenv("DB_ITERATION_TO_YIELD");
            if (count_str) {
                iter_ = strtol(count_str, NULL, 0);
            }
            init_ = true;
        }

        return iter_;
    }

    // A Walker allocated to iterator through a DBTable
    class Walker;

    // A Job for walking through the DBTablePartition
    class Worker;

    typedef std::vector<Walker *> WalkerList;
    typedef boost::dynamic_bitset<> WalkerMap;

    // Purge the walker after the walk is completed/cancelled
    void PurgeWalker(WalkId id);

    // List of walkers allocated
    tbb::mutex walkers_mutex_;
    WalkerList walkers_;
    WalkerMap walker_map_;

    uint64_t walk_request_count_;
    uint64_t walk_complete_count_;
    uint64_t walk_cancel_count_;

    static int walker_task_id_;
};
#endif
