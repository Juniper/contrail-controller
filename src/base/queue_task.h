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

#include <vector>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>
#include <tbb/spin_rw_mutex.h>

#include <base/task.h>

template <typename QueueEntryT, typename QueueT>
class QueueTaskRunner : public Task {
public:
    QueueTaskRunner(QueueT *queue)
        : Task(queue->GetTaskId(), queue->GetTaskInstance()), queue_(queue) {
    }

    bool Run() {
        // Check if this run needs to be deferred
        if (!queue_->OnEntry()) {
            return false;
        }
        return RunQueue();
        // No more client callbacks after updating
        // queue running_ and current_runner_ in RunQueue to 
        // avoid client callbacks running concurrently
    }

private:
    bool RunQueue() {
        // Check if we need to abort
        if (queue_->RunnerAbort()) {
            return queue_->RunnerDone();
        }

        QueueEntryT entry = QueueEntryT();
        size_t count = 0;
        while (queue_->Dequeue(&entry)) {
            // Process the entry
            if (!queue_->GetCallback()(entry)) {
                break;
            }
            if (++count == queue_->max_iterations_) {
                return queue_->RunnerDone();
            }
        }

        // Running is done if queue_ is empty
        // While notification is being run, its possible that more entries
        // are added into queue_
        return queue_->RunnerDone();
    }

    QueueT *queue_;
};

template <typename QueueEntryT>
struct WorkQueueDelete {
    template <typename QueueT>
    void operator()(QueueT &, bool) {}
};

template <typename QueueEntryT>
struct WorkQueueDelete<QueueEntryT *> {
    template <typename QueueT>
    void operator()(QueueT &q, bool delete_entry) {
        QueueEntryT *entry;
        while (q.try_pop(entry)) {
            if (delete_entry) {
                delete entry;
            }
        }
    }
};

template <typename QueueEntryT>
class WorkQueue {
public:
    static const int kMaxSize = 1024;
    static const int kMaxIterations = 32;
    typedef tbb::concurrent_queue<QueueEntryT> Queue;
    typedef boost::function<bool (QueueEntryT)> Callback;
    typedef boost::function<bool (void)> StartRunnerFunc;
    typedef boost::function<void (bool)> TaskExitCallback;
    typedef boost::function<bool ()> TaskEntryCallback;
    typedef boost::function<void (size_t)> WaterMarkCallback;

    struct WaterMarkInfo {
        WaterMarkInfo(size_t count, WaterMarkCallback cb) :
            count_(count),
            cb_(cb) {
        }
        size_t count_;
        WaterMarkCallback cb_;
    };

    WorkQueue(int taskId, int taskInstance, Callback callback,
              size_t size = kMaxSize,
              size_t max_iterations = kMaxIterations) :
        running_(false),
        taskId_(taskId),
        taskInstance_(taskInstance),
        callback_(callback),
        on_entry_cb_(0),
        on_exit_cb_(0),
        start_runner_(0),
        current_runner_(NULL),
        on_entry_defer_count_(0),
        disabled_(false),
        deleted_(false),
        enqueues_(0),
        dequeues_(0),
        drops_(0),
        max_iterations_(max_iterations),
        size_(size),
        bounded_(false),
        shutdown_scheduled_(false),
        delete_entries_on_shutdown_(true) {
        count_ = 0;
    }

    // Concurrency - should be called from a task whose policy
    // assures that the dequeue task - QueueTaskRunner is not running
    // concurrently
    void Shutdown(bool delete_entries = true) {
        tbb::mutex::scoped_lock lock(mutex_);
        ShutdownLocked(delete_entries);
    }

    // Concurrency - can be called from any context
    // Schedule shutdown of the WorkQueue, shutdown may happen asynchronously
    // or in the caller's context also
    void ScheduleShutdown(bool delete_entries = true) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (shutdown_scheduled_) {
            return;
        }
        shutdown_scheduled_ = true;
        delete_entries_on_shutdown_ = delete_entries;

        // Cancel QueueTaskRunner
        if (running_) {
            assert(current_runner_);
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            TaskScheduler::CancelReturnCode cancel_code =
                scheduler->Cancel(current_runner_);
            if (cancel_code == TaskScheduler::CANCELLED) {
                running_ = false;
                current_runner_ = NULL;
                ShutdownLocked(delete_entries);
            } else {
                assert(cancel_code == TaskScheduler::QUEUED);
            }
        } else {
            ShutdownLocked(delete_entries);
        }
    }

    ~WorkQueue() {
        tbb::mutex::scoped_lock lock(mutex_);
        // Shutdown() needs to be called before deleting
        //assert(!running_ && deleted_);
    }

    void SetStartRunnerFunc(StartRunnerFunc start_runner_fn) {
        start_runner_ = start_runner_fn;
    }

    void SetBounded(bool bounded) {
        bounded_ = bounded;
    }

    bool GetBounded() const {
        return bounded_;
    }

    void SetHighWaterMark(std::vector<WaterMarkInfo> &high_water) {
        tbb::spin_rw_mutex::scoped_lock write_lock(hwater_mutex_, true);
        high_water_ = high_water;
    }

    void SetHighWaterMark(WaterMarkInfo hwm_info) {
        tbb::spin_rw_mutex::scoped_lock write_lock(hwater_mutex_, true);
        high_water_.push_back(hwm_info);
    }

    void ResetHighWaterMark() {
        tbb::spin_rw_mutex::scoped_lock write_lock(hwater_mutex_, true);
        high_water_.clear();
    }

    std::vector<WaterMarkInfo> GetHighWaterMark() const {
        tbb::spin_rw_mutex::scoped_lock read_lock(hwater_mutex_, false);
        return high_water_;
    }

    void SetLowWaterMark(std::vector<WaterMarkInfo> &low_water) {
        tbb::spin_rw_mutex::scoped_lock write_lock(lwater_mutex_, true);
        low_water_ = low_water;
    }

    void SetLowWaterMark(WaterMarkInfo lwm_info) {
        tbb::spin_rw_mutex::scoped_lock write_lock(lwater_mutex_, true);
        low_water_.push_back(lwm_info);
    }

    void ResetLowWaterMark() {
        tbb::spin_rw_mutex::scoped_lock write_lock(lwater_mutex_, true);
        low_water_.clear();
    }

    std::vector<WaterMarkInfo> GetLowWaterMark() const {
        tbb::spin_rw_mutex::scoped_lock read_lock(lwater_mutex_, false);
        return low_water_;
    }

    bool Enqueue(QueueEntryT entry) {
        if (bounded_) {
            return EnqueueBounded(entry);
        } else {
            return EnqueueInternal(entry);
        }
    }

    // Returns true if pop is successful.
    bool Dequeue(QueueEntryT *entry) {
        bool success = queue_.try_pop(*entry);
        if (success) {
            dequeues_++;
            size_t ocount = count_.fetch_and_decrement();
            ProcessLowWaterMarks(ocount);
        }
        return success;
    }

    int GetTaskId() const {
        return taskId_;
    }

    int GetTaskInstance() const {
        return taskInstance_;
    }

    void MayBeStartRunner() {
        tbb::mutex::scoped_lock lock(mutex_);
        if (running_ || queue_.empty() || deleted_ || RunnerAbortLocked()) {
            return;
        }
        running_ = true;
        assert(current_runner_ == NULL);
        current_runner_ =
            new QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> >(this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(current_runner_);
    }

    Callback GetCallback() const {
        return callback_;
    }

    void SetEntryCallback(TaskEntryCallback on_entry) {
        on_entry_cb_ = on_entry;
    }

    void SetExitCallback(TaskExitCallback on_exit) {
        on_exit_cb_ = on_exit;
    }

    // For testing only.
    void set_disable(bool disabled) {
        disabled_ = disabled;
        MayBeStartRunner();
    }

    bool IsDisabled() const {
        return disabled_;
    }

    size_t on_entry_defer_count() const {
        return on_entry_defer_count_;
    }

    bool OnEntry() {
        bool run = (on_entry_cb_.empty() || on_entry_cb_());

        // Track number of times this queue run is deferred
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

    bool IsQueueEmpty() const {
        return queue_.empty();
    }

    size_t Length() const {
        return count_;
    }

    size_t NumEnqueues() const {
        return enqueues_;
    }

    size_t NumDequeues() const {
        return dequeues_;
    }

    size_t NumDrops() const {
        return drops_;
    }

private:
    void ShutdownLocked(bool delete_entries) {
        // Cancel QueueTaskRunner from the scheduler
        assert(!deleted_);
        if (running_) {
            running_ = false;
            assert(current_runner_);
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            TaskScheduler::CancelReturnCode cancel_code =
                scheduler->Cancel(current_runner_);
            assert(cancel_code == TaskScheduler::CANCELLED);
            current_runner_ = NULL;
        }
        ResetHighWaterMark();
        ResetLowWaterMark();
        WorkQueueDelete<QueueEntryT> deleter;
        deleter(queue_, delete_entries);
        queue_.clear();
        count_ = 0;
        deleted_ = true;
    }

    void ProcessWaterMarks(std::vector<WaterMarkInfo> &wm,
                           size_t count) {
        // Are we crossing any water marks ?
        for (size_t i = 0; i < wm.size(); i++) {
            if (count == wm[i].count_) {
                wm[i].cb_(count);
                break;
            }
        }
    }

    void ProcessHighWaterMarks(size_t count) {
        tbb::spin_rw_mutex::scoped_lock read_lock(hwater_mutex_, false);
        ProcessWaterMarks(high_water_, count);
    }

    void ProcessLowWaterMarks(size_t count) {
        tbb::spin_rw_mutex::scoped_lock read_lock(lwater_mutex_, false);
        ProcessWaterMarks(low_water_, count);
    }

    bool EnqueueInternal(QueueEntryT entry) {
        queue_.push(entry);
        enqueues_++;
        MayBeStartRunner();
        size_t ocount = count_.fetch_and_increment();
        ProcessHighWaterMarks(ocount);
        return ocount < (size_ - 1);
    }

    bool EnqueueBounded(QueueEntryT entry) {
        if (count_ < size_ - 1) {
            enqueues_++;
            queue_.push(entry);
            MayBeStartRunner();
            size_t ocount = count_.fetch_and_increment();
            ProcessHighWaterMarks(ocount);
            return true;
        }
        drops_++;
        return false;
    }

    bool RunnerAbortLocked() {
        return (disabled_ || shutdown_scheduled_ ||
                (!start_runner_.empty() && !start_runner_()));
    }

    bool RunnerAbort() {
        tbb::mutex::scoped_lock lock(mutex_);
        return RunnerAbortLocked();
    }

    bool RunnerDone() {
        tbb::mutex::scoped_lock lock(mutex_);
        bool done = false;
        if (queue_.empty() || RunnerAbortLocked()) {
            done = true;
            OnExit(done);
            current_runner_ = NULL;
            running_ = false;
            if (shutdown_scheduled_) {
                ShutdownLocked(delete_entries_on_shutdown_);
            }
        } else {
            OnExit(done);
            running_ = true;
        }
        return done;
    }

    Queue queue_;
    tbb::atomic<size_t> count_;
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
    bool disabled_;
    bool deleted_;
    size_t enqueues_;
    size_t dequeues_;
    size_t drops_;
    size_t max_iterations_;
    size_t size_;
    bool bounded_;
    bool shutdown_scheduled_;
    bool delete_entries_on_shutdown_;
    std::vector<WaterMarkInfo> high_water_; // When queue count goes above
    std::vector<WaterMarkInfo> low_water_; // When queue count goes below 
    tbb::spin_rw_mutex hwater_mutex_;
    tbb::spin_rw_mutex lwater_mutex_;

    friend class QueueTaskTest;
    friend class QueueTaskShutdownTest;
    friend class QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> >;

    DISALLOW_COPY_AND_ASSIGN(WorkQueue);
};

#endif /* __QUEUE_TASK_H__ */
