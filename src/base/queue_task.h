/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

// queue_task.h
//
// Task based queue processor implementing thread safe enqueue and dequeue
// using concurrent queues. If queue is empty, enqueue creates a dequeue task
// that drains the queue. The dequeue task runs a maximum of kMaxIterations
// before yielding.
//
#ifndef __QUEUE_TASK_H__
#define __QUEUE_TASK_H__

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>

#include "base/task.h"

template <typename QueueEntryT, typename QueueT>
class QueueTaskRunner : public Task {
public:
    static const int kMaxIterations = 32;

    QueueTaskRunner(QueueT *queue)
        : Task(queue->GetTaskId(), queue->GetTaskInstance()), queue_(queue) {
    }

    bool RunQueue() {
        QueueEntryT entry = QueueEntryT();
        int count = 0;
        
        while (queue_->Dequeue(&entry)) {
            // Process the entry
            if (!queue_->GetCallback()(entry)) {
                break;
            }

            if (++count == kMaxIterations) {
                return queue_->RunnerDone();
            }
        }
        
        // Running is done if queue_ is empty
        // While notification is being run, its possible that more entries
        // are added into queue_
        return queue_->RunnerDone();
    }

    bool Run() {

        //
        // Check if this run needs to be deferred
        //
        if (!queue_->OnEntry()) {
            return false;
        }
        bool done = RunQueue();
        queue_->OnExit(done);
        return done;
    }

private:
    QueueT *queue_;
    bool run_disable_;
};

template <typename QueueEntryT>
struct WorkQueueDelete {
    template <typename QueueT>
    void operator()(QueueT &) {}
};
template <typename QueueEntryT>
struct WorkQueueDelete<QueueEntryT *> {
    template <typename QueueT>
    void operator()(QueueT &q) {
        for (typename QueueT::iterator iter = q.unsafe_begin();
             iter != q.unsafe_end(); ++iter) {
            delete *iter;
        }
    }
};

template <typename QueueEntryT>
class WorkQueue {
public:
    static const int kThreshold = 1024;
    typedef tbb::concurrent_queue<QueueEntryT> Queue;
    typedef boost::function<bool (QueueEntryT)> Callback;
    typedef boost::function<bool (void)> StartRunnerFunc;
    typedef boost::function<void (bool)> TaskExitCallback;
    typedef boost::function<bool ()> TaskEntryCallback;

    WorkQueue(int taskId, int taskInstance, Callback callback,
            StartRunnerFunc start_runner = 0) :
    	running_(false),
    	taskId_(taskId),
    	taskInstance_(taskInstance),
    	callback_(callback),
        on_entry_cb_(0),
        on_exit_cb_(0),
    	start_runner_(start_runner),
        current_runner_(NULL),
        on_entry_defer_count_(0),
        disable_(false),
        deleted_(false),
        enqueues_(0) {
        count_ = 0;
    }

    // Concurrency - should be called from a task whose policy
    // assures that the dequeue task - QueueTaskRunner is not running
    // concurrently
    void Shutdown() {
        WorkQueueDelete<QueueEntryT> deleter;
        deleter(queue_);
        queue_.clear();
        // Cancel QueueTaskRunner from the scheduler
        tbb::mutex::scoped_lock lock(mutex_);
        assert(!deleted_);
        if (running_) {
            running_ = false;
            assert(current_runner_);
            TaskScheduler *scheduler = TaskScheduler::GetInstance(); 
            scheduler->Cancel(current_runner_);
            current_runner_ = NULL;
        }
        deleted_ = true; 
    }

    ~WorkQueue() {
        tbb::mutex::scoped_lock lock(mutex_);
        // Shutdown() needs to be called before deleting
        //assert(!running_ && deleted_);
    }

    bool Enqueue(QueueEntryT entry) {
        queue_.push(entry);
        enqueues_++;
        MayBeStartRunner();
        return count_.fetch_and_increment() < (kThreshold - 1);
    }

    // Returns true if pop is successful.
    bool Dequeue(QueueEntryT *entry) {
        bool success = queue_.try_pop(*entry);
        if (success) {
            count_.fetch_and_decrement();
        }
        return success;
    }

    int GetTaskId() {
    	return taskId_;
    }

    int GetTaskInstance() {
    	return taskInstance_;
    }

    void MayBeStartRunner() {
    	tbb::mutex::scoped_lock lock(mutex_);
    	if (running_ || queue_.empty() || deleted_) {
    		return;
    	}
    	if (!start_runner_.empty() && !start_runner_()) {
    	    return;
    	}
    	running_ = true;
        assert(current_runner_ == NULL);
    	current_runner_ = new QueueTaskRunner<
    		                  QueueEntryT, WorkQueue<QueueEntryT> >(this);
    	TaskScheduler *scheduler = TaskScheduler::GetInstance();
    	scheduler->Enqueue(current_runner_);
    }

    Callback GetCallback() {
    	return callback_;
    }

    void SetEntryCallback(TaskEntryCallback on_entry) {
        on_entry_cb_ = on_entry;
    }

    void SetExitCallback(TaskExitCallback on_exit) {
        on_exit_cb_ = on_exit;
    }

    void set_disable(bool disable) { disable_ = disable; }
    size_t on_entry_defer_count() { return on_entry_defer_count_; }

    bool OnEntry() {

        //
        // XXX For testing only, defer this task run
        //
        if (disable_) {
            return false;
        }

        bool run = (on_entry_cb_.empty() || on_entry_cb_());

        //
        // Track number of times this queue run is deferred
        //
        if (!run) {
            on_entry_defer_count_++;
        }

        return run;
    }

    void OnExit(bool done) {
        if (!on_exit_cb_.empty()) {
            on_exit_cb_(done);
        }
    }

    bool IsQueueEmpty() {
        return queue_.empty();
    }

    uint64_t QueueCount() {
        return count_;
    }

    uint64_t EnqueueCount() {
        return enqueues_;
    }

private:
    bool RunnerDone() {
        tbb::mutex::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            current_runner_ = NULL;         
            running_ = false;
            return true;
        } else if (!start_runner_.empty() && !start_runner_()) {
            current_runner_ = NULL;
            running_ = false;
            return true;
        }

        running_ = true;
        return false;
    }

    Queue queue_;
    tbb::atomic<long> count_;
    tbb::mutex mutex_;
    bool running_;
    int taskId_;
    int taskInstance_;
    Callback callback_;
    TaskEntryCallback on_entry_cb_;
    TaskExitCallback on_exit_cb_;
    StartRunnerFunc start_runner_;
    QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> > *current_runner_;
    size_t on_entry_defer_count_;
    bool disable_;
    bool deleted_;
    uint64_t enqueues_;

    friend class QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> >;

    DISALLOW_COPY_AND_ASSIGN(WorkQueue);
};

#endif /* __QUEUE_TASK_H__ */
