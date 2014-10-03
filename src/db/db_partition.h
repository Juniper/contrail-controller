/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_partition_h
#define ctrlplane_db_partition_h

#include <boost/function.hpp>

#include "base/util.h"
#include "db/db_table.h"
#include "db/db_table_partition.h"

class DBClient;
class DBRecord;
class DBTablePartBase;

// Database shard interface.
// Each shard handles the full pipeline of DB update processing.
class DBPartition {
public:
    typedef boost::function<void(void)> Callback;

    explicit DBPartition(int partition_id);
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

private:
    class WorkQueue;
    class QueueRunner;
    std::auto_ptr<WorkQueue> work_queue_;
    static int db_partition_task_id_;
    DISALLOW_COPY_AND_ASSIGN(DBPartition);
};

#endif
