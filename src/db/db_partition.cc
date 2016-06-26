/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_partition.h"

#include <list>
#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>

#include "base/task.h"
#include "db/db.h"
#include "db/db_client.h"
#include "db/db_entry.h"

using tbb::concurrent_queue;
using tbb::atomic;

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

    explicit WorkQueue(DBPartition *partition, int partition_id)
        : db_partition_(partition),
          db_partition_id_(partition_id),
          disable_(false),
          running_(false) {
        request_count_ = 0;
        max_request_queue_len_ = 0;
        total_request_count_ = 0;
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
        uint32_t max = request_count_.fetch_and_increment();
        if (max > max_request_queue_len_)
            max_request_queue_len_ = max;
        total_request_count_++;
        return max < (kThreshold - 1);

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

    int db_task_id() const { return db_partition_->task_id(); }

    bool IsDBQueueEmpty() const {
        return (request_queue_.empty() && change_list_.empty());
    }

    bool disable() { return disable_; }
    void set_disable(bool disable) { disable_ = disable; }

    long request_queue_len() const {
        return request_count_;
    }

    uint64_t total_request_count() const {
        return total_request_count_;
    }

    uint64_t max_request_queue_len() const {
        return max_request_queue_len_;
    }

private:
    DBPartition *db_partition_;
    RequestQueue request_queue_;
    TablePartList change_list_;
    atomic<long> request_count_;
    uint64_t total_request_count_;
    uint64_t max_request_queue_len_;
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
    if (disable) {
        work_queue_->set_disable(true);
    } else {
        work_queue_->set_disable(false);
        work_queue_->MaybeStartRunner();
    }
}

class DBPartition::QueueRunner : public Task {
public:
    static const int kMaxIterations = 32;
    QueueRunner(WorkQueue *queue) 
        : Task(queue->db_task_id(), queue->db_partition_id()),
          queue_(queue) {
    }

    virtual bool Run() {
        int count = 0;

        // Skip if the queue is disabled.
        if (queue_->disable())
            return queue_->RunnerDone();

        RemoveQueueEntry *rm_entry = NULL;
        while (queue_->DequeueRemove(&rm_entry)) {
            DBEntryBase *db_entry = rm_entry->db_entry;
            {
                tbb::spin_rw_mutex::scoped_lock
                    lock(rm_entry->tpart->dbstate_mutex(), false);
                if (!db_entry->IsDeleted() || db_entry->is_onlist() ||
                    !db_entry->is_state_empty_unlocked(rm_entry->tpart)) {
                    db_entry->ClearOnRemoveQ();
                    db_entry = NULL;
                }
            }
            if (db_entry) {
                rm_entry->tpart->Remove(db_entry);
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

    std::string Description() const {
        return "DBPartition QueueRunner";
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
    if (disable_ || (request_queue_.empty() && remove_queue_.empty())) {
        running_ = false;
        return true;
    }

    running_ = true;
    return false;
}

DBPartition::DBPartition(DB *db, int partition_id)
    : db_(db), work_queue_(new WorkQueue(this, partition_id)) {
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

long DBPartition::request_queue_len() const {
    return work_queue_->request_queue_len();
}

uint64_t DBPartition::total_request_count() const {
    return work_queue_->total_request_count();
}

uint64_t DBPartition::max_request_queue_len() const {
    return work_queue_->max_request_queue_len();
}

int DBPartition::task_id() const {
    return db_->task_id();
}
