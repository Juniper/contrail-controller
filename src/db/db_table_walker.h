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

    static const int kIterationToYield = 1024;
    static const WalkId kInvalidWalkerId = -1;

    DBTableWalker(int task_id = -1);

    // Start a walk request on the specified table. If non null, 'key_start'
    // specifies the starting point for the walk. The walk is performed in
    // all table shards in parallel.
    WalkId WalkTable(DBTable *table, const DBRequestKey *key_start,
                     WalkFn walker, WalkCompleteFn walk_complete,
                     bool postpone_walk = false);

    // cancel a walk that may be in progress. This cannot be called from
    // the walker function itself.
    void WalkCancel(WalkId id);
    void WalkResume(WalkId id);

    int task_id() const { return task_id_; }

    static void SetIterationToYield(int count) {
        max_iteration_to_yield_ = count;
    }

private:
    static int max_iteration_to_yield_;

    static int GetIterationToYield() {
        static bool init_ = false;

        if (!init_) {

            // XXX To be used for testing purposes only.
            char *count_str = getenv("DB_ITERATION_TO_YIELD");
            if (count_str) {
                max_iteration_to_yield_ = strtol(count_str, NULL, 0);
            }
            init_ = true;
        }

        return max_iteration_to_yield_;
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
    int task_id_;
    tbb::mutex walkers_mutex_;
    WalkerList walkers_;
    WalkerMap walker_map_;
};

#endif
