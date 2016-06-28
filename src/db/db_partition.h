/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_partition_h
#define ctrlplane_db_partition_h

#include <boost/function.hpp>

#include "base/util.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

class DB;
class DBClient;
class DBTablePartBase;

// Database shard interface.
// Each shard handles the full pipeline of DB update processing.
class DBPartition {
public:
    typedef boost::function<void(void)> Callback;

    explicit DBPartition(DB *db, int partition_id);
    ~DBPartition();

    // Enqueue a request at the start of the DB processing pipeline.
    // Returns false if the client should stop enqueuing updates.
    bool EnqueueRequest(DBTablePartBase *tpart, DBClient *client,
                        DBRequest *req);

    void EnqueueRemove(DBTablePartBase *tpart, DBEntryBase *db_entry);

    // Enqueue table on change list.
    void OnTableChange(DBTablePartBase *tpart);
    bool IsDBQueueEmpty() const;
    void SetQueueDisable(bool disable);

    long request_queue_len() const;
    uint64_t total_request_count() const;
    uint64_t max_request_queue_len() const;
    int task_id() const;

private:
    class WorkQueue;
    class QueueRunner;

    DB *db_;
    std::auto_ptr<WorkQueue> work_queue_;
    static int db_partition_task_id_;

    DISALLOW_COPY_AND_ASSIGN(DBPartition);
};

#endif
