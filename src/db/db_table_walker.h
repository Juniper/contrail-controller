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
    void RegisterTable(const DBTableBase *tbl_base);
    void UnregisterTable(const DBTableBase *tbl_base);

    uint64_t walk_request_count(const DBTableBase *tbl_base) const;
    uint64_t walk_complete_count(const DBTableBase *tbl_base) const;
    uint64_t walk_cancel_count(const DBTableBase *tbl_base) const;

private:
    static int walker_task_id_;
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

    struct TableStats {
        TableStats() : request_count(0), complete_count(0), cancel_count(0) {
        }
        uint64_t request_count;
        uint64_t complete_count;
        uint64_t cancel_count;
    };

    typedef std::vector<Walker *> WalkerList;
    typedef boost::dynamic_bitset<> WalkerMap;
    typedef std::map<const DBTableBase *, TableStats> TableStatsMap;

    // Purge the walker after the walk is completed/cancelled
    void PurgeWalker(WalkId id);
    void inc_walk_request_count(const DBTableBase *tbl_base);
    void inc_walk_complete_count(const DBTableBase *tbl_base);
    void inc_walk_cancel_count(const DBTableBase *tbl_base);

    // List of walkers allocated
    mutable tbb::mutex walkers_mutex_;
    WalkerList walkers_;
    WalkerMap walker_map_;
    TableStatsMap stats_map_;
};

#endif
