/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_partition.h"

#include <list>
#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>

#include "base/task.h"
#include "db/db_client.h"
#include "db/db_entry.h"

using tbb::concurrent_queue;
using tbb::atomic;

int DBPartition::db_partition_task_id_ = -1;

struct RequestQueueEntry {
    // Constructor takes ownership of DBRequest key, data.
    RequestQueueEntry(DBTablePartBase *tpart, DBClient *client, DBRequest *req)
        : tpart(tpart), client(client) {
        request.Swap(req);
    }
    DBTablePartBase *tpart;
    DBClient *client;
    DBRequest request;
};

struct RemoveQueueEntry {
    RemoveQueueEntry(DBTablePartBase *tpart, DBEntryBase *db_entry)
        : tpart(tpart), db_entry(db_entry) {
    }
    DBTablePartBase *tpart;
    DBEntryBase *db_entry;
};

class DBPartition::WorkQueue {
public:
    static const int kThreshold = 1024;
    typedef concurrent_queue<RequestQueueEntry *> RequestQueue;
    typedef concurrent_queue<RemoveQueueEntry *> RemoveQueue;
    typedef std::list<DBTablePartBase *> TablePartList;

    explicit WorkQueue(int partition_id) 
        : db_partition_id_(partition_id), disable_(false), running_(false) {
        request_count_ = 0;
    }
    ~WorkQueue() {
        for (RequestQueue::iterator iter = request_queue_.unsafe_begin();
             iter != request_queue_.unsafe_end();) {
            RequestQueueEntry *req_entry = *iter;
            ++iter;
            delete req_entry;
        }
        request_queue_.clear();
    }

    bool EnqueueRequest(RequestQueueEntry *req_entry) {
        request_queue_.push(req_entry);
        MaybeStartRunner();
        return request_count_.fetch_and_increment() < (kThreshold - 1);
    }

    bool DequeueRequest(RequestQueueEntry **req_entry) {
        bool success = request_queue_.try_pop(*req_entry);
        if (success) {
            request_count_.fetch_and_decrement();
        }
        return success;
    }

    void EnqueueRemove(RemoveQueueEntry *rm_entry) {
        remove_queue_.push(rm_entry);
        MaybeStartRunner();
    }

    bool DequeueRemove(RemoveQueueEntry **rm_entry) {
        bool success = remove_queue_.try_pop(*rm_entry);
        return success;
    }

    void MaybeStartRunner();
    bool RunnerDone();

    void SetActive(DBTablePartBase *tpart) {
        change_list_.push_back(tpart);
        MaybeStartRunner();
    }

    DBTablePartBase *GetActiveTable() {
        DBTablePartBase *tpart = NULL;
        if (!change_list_.empty()) {
            tpart = change_list_.front();
            change_list_.pop_front();
        }
        return tpart;
    }

    int db_partition_id() {
        return db_partition_id_;
    }

    bool IsDBQueueEmpty() const {
        return (request_queue_.empty() && change_list_.empty());
    }

    bool disable() { return disable_; }
    void set_disable(bool disable) { disable_ = disable; }

private:
    RequestQueue request_queue_;
    TablePartList change_list_;
    atomic<long> request_count_;
    RemoveQueue remove_queue_;
    tbb::mutex mutex_;
    int db_partition_id_;
    bool disable_;
    bool running_;
    DISALLOW_COPY_AND_ASSIGN(WorkQueue);
};

bool DBPartition::IsDBQueueEmpty() const {
    return work_queue_->IsDBQueueEmpty();
}

void DBPartition::SetQueueDisable(bool disable) {
    work_queue_->set_disable(disable);
}

class DBPartition::QueueRunner : public Task {
public:
    static const int kMaxIterations = 32;
    QueueRunner(WorkQueue *queue) 
        : Task(db_partition_task_id_, queue->db_partition_id()), 
          queue_(queue) {
    }

    virtual bool Run() {
        int count = 0;

        //
        // Skip if the queue is disabled from running
        //
        if (queue_->disable()) return false;

        RemoveQueueEntry *rm_entry = NULL;
        while (queue_->DequeueRemove(&rm_entry)) {
            if (rm_entry->db_entry->IsDeleted() &&
                !rm_entry->db_entry->is_onlist() &&
                rm_entry->db_entry->is_state_empty(rm_entry->tpart)) {
                rm_entry->tpart->Remove(rm_entry->db_entry);
            } else {
                rm_entry->db_entry->ClearOnRemoveQ();
            }
            delete rm_entry;
            if (++count == kMaxIterations) {
                return false;
            }
        }

        RequestQueueEntry *req_entry = NULL;
        while (queue_->DequeueRequest(&req_entry)) {
            req_entry->tpart->Process(req_entry->client, &req_entry->request);
            delete req_entry;
            if (++count == kMaxIterations) {
                return false;
            }
        }
        
        while (true) {
            DBTablePartBase *tpart = queue_->GetActiveTable();
            if (tpart == NULL) {
                break;
            }
            bool done = tpart->RunNotify();
            if (!done) {
                return false;
            }
        }

        // Running is done only if queue_ is empty. It's possible that more
        // entries are added into in the input or remove queues during the
        // time we were processing those queues.
        return queue_->RunnerDone();
    }
    
private:
    WorkQueue *queue_;
};

void DBPartition::WorkQueue::MaybeStartRunner() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    QueueRunner *runner = new QueueRunner(this);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(runner);
}

bool DBPartition::WorkQueue::RunnerDone() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (request_queue_.empty() && remove_queue_.empty()) {
        running_ = false;
        return true;
    }

    running_ = true;
    return false;
}

DBPartition::DBPartition(int partition_id)
    : work_queue_(new WorkQueue(partition_id)) {
    if (db_partition_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        db_partition_task_id_ = scheduler->GetTaskId("db::DBTable");
    }
}

// The DBPartition destructor needs to be defined after WorkQueue has
// been declared.
DBPartition::~DBPartition() {
}

bool DBPartition::EnqueueRequest(DBTablePartBase *tpart, DBClient *client,
                                 DBRequest *req) {
    RequestQueueEntry *entry = new RequestQueueEntry(tpart, client, req);
    return work_queue_->EnqueueRequest(entry);
}

void DBPartition::EnqueueRemove(DBTablePartBase *tpart, DBEntryBase *db_entry) {
    RemoveQueueEntry *entry = new RemoveQueueEntry(tpart, db_entry);
    db_entry->SetOnRemoveQ();
    work_queue_->EnqueueRemove(entry);
}

// concurrency: called from DBPartition task.
void DBPartition::OnTableChange(DBTablePartBase *tablepart) {
    work_queue_->SetActive(tablepart);
}
