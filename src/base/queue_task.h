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

#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <set>

#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>
#include <tbb/spin_rw_mutex.h>

#include <base/task.h>
#include <base/time_util.h>

// WaterMarkInfo
typedef boost::function<void (size_t)> WaterMarkCallback;

struct WaterMarkInfo {
    WaterMarkInfo(size_t count, WaterMarkCallback cb) :
        count_(count),
        cb_(cb) {
    }
    friend inline bool operator<(const WaterMarkInfo& lhs,
        const WaterMarkInfo& rhs);
    friend inline bool operator==(const WaterMarkInfo& lhs,
        const WaterMarkInfo& rhs);
    size_t count_;
    WaterMarkCallback cb_;
};

inline bool operator<(const WaterMarkInfo& lhs, const WaterMarkInfo& rhs) {
    return lhs.count_ < rhs.count_;
}

inline bool operator==(const WaterMarkInfo& lhs, const WaterMarkInfo& rhs) {
    return lhs.count_ == rhs.count_;
}

typedef std::vector<WaterMarkInfo> WaterMarkInfos;

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

    virtual std::string Description() const {
        return queue_->Description();
    }

private:
    bool RunQueue() {
        // Check if we need to abort
        if (queue_->RunnerAbort()) {
            return queue_->RunnerDone();
        }

        uint64_t start = 0;
        if (queue_->measure_busy_time_)
            start = ClockMonotonicUsec();

        QueueEntryT entry = QueueEntryT();
        size_t count = 0;
        while (queue_->Dequeue(&entry)) {
            // Process the entry
            if (!queue_->GetCallback()(entry)) {
                break;
            }
            if (++count == queue_->max_iterations_) {
                if (start)
                    queue_->add_busy_time(ClockMonotonicUsec() - start);
                return queue_->RunnerDone();
            }
        }

        if (start)
            queue_->add_busy_time(ClockMonotonicUsec() - start);

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

    WorkQueue(int taskId, int taskInstance, Callback callback,
              size_t size = kMaxSize,
              size_t max_iterations = kMaxIterations) :
        running_(false),
        taskId_(taskId),
        taskInstance_(taskInstance),
        name_(""),
        callback_(callback),
        on_entry_cb_(0),
        on_exit_cb_(0),
        start_runner_(0),
        current_runner_(NULL),
        on_entry_defer_count_(0),
        deleted_(false),
        enqueues_(0),
        dequeues_(0),
        drops_(0),
        max_iterations_(max_iterations),
        size_(size),
        bounded_(false),
        shutdown_scheduled_(false),
        delete_entries_on_shutdown_(true),
        task_starts_(0),
        max_queue_len_(0),
        busy_time_(0),
        measure_busy_time_(false) {
        count_ = 0;
        hwater_index_ = -1;
        lwater_index_ = -1;
        disabled_ = false;
        hwater_mark_set_ = false;
        lwater_mark_set_ = false;
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

    void SetSize(size_t size) {
        size_ = size;
    }

    void SetBounded(bool bounded) {
        bounded_ = bounded;
    }

    bool GetBounded() const {
        return bounded_;
    }

    void SetHighWaterMark(const WaterMarkInfos &high_water) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMarkInfo> hwater_set(high_water.begin(),
            high_water.end());
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        high_water_ = WaterMarkInfos(hwater_set.begin(), hwater_set.end());
        hwater_mark_set_ = true;
    }

    void SetHighWaterMark(const WaterMarkInfo& hwm_info) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMarkInfo> hwater_set(high_water_.begin(),
            high_water_.end());
        hwater_set.insert(hwm_info);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        high_water_ = WaterMarkInfos(hwater_set.begin(), hwater_set.end());
        hwater_mark_set_ = true;
    }

    void ResetHighWaterMark() {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        high_water_.clear();
        hwater_mark_set_ = false;
    }

    WaterMarkInfos GetHighWaterMark() const {
        tbb::mutex::scoped_lock lock(water_mutex_);
        return high_water_;
    }

    void SetLowWaterMark(const WaterMarkInfos &low_water) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMarkInfo> lwater_set(low_water.begin(),
            low_water.end());
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        low_water_ = WaterMarkInfos(lwater_set.begin(), lwater_set.end());
        lwater_mark_set_ = true;
     }

    void SetLowWaterMark(const WaterMarkInfo& lwm_info) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Eliminate duplicates and sort by converting to set
        std::set<WaterMarkInfo> lwater_set(low_water_.begin(),
            low_water_.end());
        lwater_set.insert(lwm_info);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        low_water_ = WaterMarkInfos(lwater_set.begin(), lwater_set.end());
        lwater_mark_set_ = true;
     }

    void ResetLowWaterMark() {
        tbb::mutex::scoped_lock lock(water_mutex_);
        // Update both high and low water mark indexes
        SetWaterMarkIndexes(-1, -1);
        low_water_.clear();
        lwater_mark_set_ = false;
    }

    WaterMarkInfos GetLowWaterMark() const {
        tbb::mutex::scoped_lock lock(water_mutex_);
        return low_water_;
    }

    bool Enqueue(QueueEntryT entry) {
        if (bounded_) {
            if (AreWaterMarksSet()) {
                return EnqueueBoundedLocked(entry);
            } else {
                return EnqueueBounded(entry);
            }
        } else {
            if (AreWaterMarksSet()) {
                return EnqueueInternalLocked(entry);
            } else {
                return EnqueueInternal(entry);
            }
        }
    }

    // Returns true if pop is successful.
    bool Dequeue(QueueEntryT *entry) {
        if (AreWaterMarksSet()) {
            return DequeueInternalLocked(entry);
        } else {
            return DequeueInternal(entry);
        }
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
        task_starts_++;
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

    void set_name(const std::string &name) {
        name_ = name;
    }
    std::string Description() const {
        if (name_.empty() == false)
            return name_;

        std::ostringstream str;
        str << "Function " << callback_;
        return str.str();
    }

    void set_disable(bool disabled) {
        if (disabled_ != disabled) {
            disabled_ = disabled;
            if (!disabled_) {
                MayBeStartRunner();
            }
        }
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

    bool deleted() const {
        return deleted_;
    }

    uint32_t task_starts() const { return task_starts_; }
    uint32_t max_queue_len() const { return max_queue_len_; }
    bool measure_busy_time() const { return measure_busy_time_; }
    void set_measure_busy_time(bool val) const { measure_busy_time_ = val; }
    uint64_t busy_time() const { return busy_time_; }
    void add_busy_time(uint64_t t) { busy_time_ += t; }
    void ClearStats() const {
        max_queue_len_ = 0;
        enqueues_ = 0;
        dequeues_ = 0;
        busy_time_ = 0;
        task_starts_ = 0;
    }
private:
    // Returns true if pop is successful.
    bool DequeueInternal(QueueEntryT *entry) {
        bool success = queue_.try_pop(*entry);
        if (success) {
            dequeues_++;
            size_t ncount(AtomicDecrementQueueCount(entry));
            ProcessLowWaterMarks(ncount);
        }
        return success;
    }

    bool DequeueInternalLocked(QueueEntryT *entry) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        return DequeueInternal(entry);
    }

    bool AreWaterMarksSet() const {
        return hwater_mark_set_ || lwater_mark_set_;
    }

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

    size_t AtomicIncrementQueueCount(QueueEntryT *entry) {
        return count_.fetch_and_increment() + 1;
    }

    size_t AtomicDecrementQueueCount(QueueEntryT *entry) {
        return count_.fetch_and_decrement() - 1;
    }

    void ProcessHighWaterMarks(size_t count) {
        if (!hwater_mark_set_ || high_water_.size() == 0) {
            return;
        }
        // Are we crossing any new high water marks ? Assumption here is that
        // the vector is sorted in ascending order of the high water
        // mark counts. Upper bound finds first element that is greater than
        // count.
        WaterMarkInfos::const_iterator ubound(std::upper_bound(
            high_water_.begin(), high_water_.end(),
            WaterMarkInfo(count, NULL)));
        // Get high and low water mark indexes
        int hwater_index, lwater_index;
        GetWaterMarkIndexes(&hwater_index, &lwater_index);
        // If the first element is greater than count, then we have not
        // yet crossed any water marks
        if (ubound == high_water_.begin()) {
            SetWaterMarkIndexes(-1, lwater_index);
            return;
        }
        int nhwater_index(ubound - high_water_.begin() - 1);
        if (hwater_index == nhwater_index) {
            return;
        }
        // Update the high and low water indexes
        SetWaterMarkIndexes(nhwater_index, nhwater_index + 1);
        const WaterMarkInfo &wm_info(high_water_[nhwater_index]);
        assert(count >= wm_info.count_);
        wm_info.cb_(count);
    }

    void ProcessLowWaterMarks(size_t count) {
        if (!lwater_mark_set_ || low_water_.size() == 0) {
            return;
        }
        // Are we crossing any new low water marks ? Assumption here is that
        // the vector is sorted in ascending order of the low water
        // mark counts. Lower bound finds first element that is not less than
        // count.
        WaterMarkInfos::const_iterator lbound(std::lower_bound(
            low_water_.begin(), low_water_.end(),
            WaterMarkInfo(count, NULL)));
        // If no element is not less than count we have not yet crossed
        // any low water marks
        if (lbound == low_water_.end()) {
            return;
        }
        int nlwater_index(lbound - low_water_.begin());
        // Get the high and low water indexes
        int hwater_index, lwater_index;
        GetWaterMarkIndexes(&hwater_index, &lwater_index);
        if (lwater_index == nlwater_index) {
            return;
        }
        // Update the high and low water indexes
        SetWaterMarkIndexes(nlwater_index - 1, nlwater_index);
        const WaterMarkInfo &wm_info(low_water_[nlwater_index]);
        assert(count <= wm_info.count_);
        wm_info.cb_(count);
    }

    bool EnqueueInternal(QueueEntryT entry) {
        enqueues_++;
        size_t ncount(AtomicIncrementQueueCount(&entry));
        if (ncount > max_queue_len_)
            max_queue_len_ = ncount;
        ProcessHighWaterMarks(ncount);
        queue_.push(entry);
        MayBeStartRunner();
        return ncount < size_;
    }

    bool EnqueueInternalLocked(QueueEntryT entry) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        return EnqueueInternal(entry);
    }

    bool EnqueueBounded(QueueEntryT entry) {
        size_t ncount(AtomicIncrementQueueCount(&entry));
        if (ncount > max_queue_len_)
            max_queue_len_ = ncount;
        if (ncount < size_) {
            enqueues_++;
            ProcessHighWaterMarks(ncount);
            queue_.push(entry);
            MayBeStartRunner();
            return true;
        }
        AtomicDecrementQueueCount(&entry);
        drops_++;
        max_queue_len_ = count_;
        return false;
    }

    bool EnqueueBoundedLocked(QueueEntryT entry) {
        tbb::mutex::scoped_lock lock(water_mutex_);
        return EnqueueBounded(entry);
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

    void GetWaterMarkIndexes(int *hwater_index, int *lwater_index) const {
        *hwater_index = hwater_index_;
        *lwater_index = lwater_index_;
    }

    void SetWaterMarkIndexes(int hwater_index, int lwater_index) {
        hwater_index_ = hwater_index;
        lwater_index_ = lwater_index;
    }

    Queue queue_;
    tbb::atomic<size_t> count_;
    tbb::mutex mutex_;
    bool running_;
    int taskId_;
    int taskInstance_;
    std::string name_;
    Callback callback_;
    TaskEntryCallback on_entry_cb_;
    TaskExitCallback on_exit_cb_;
    StartRunnerFunc start_runner_;
    QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> > *current_runner_;
    size_t on_entry_defer_count_;
    tbb::atomic<bool> disabled_;
    bool deleted_;
    mutable size_t enqueues_;
    mutable size_t dequeues_;
    size_t drops_;
    size_t max_iterations_;
    size_t size_;
    bool bounded_;
    bool shutdown_scheduled_;
    bool delete_entries_on_shutdown_;
    // Watermarks
    // Sorted in ascending order
    WaterMarkInfos high_water_; // When queue count goes above
    WaterMarkInfos low_water_; // When queue count goes below
    int hwater_index_;
    int lwater_index_;
    mutable tbb::mutex water_mutex_;
    tbb::atomic<bool> hwater_mark_set_;
    tbb::atomic<bool> lwater_mark_set_;
    mutable uint32_t task_starts_;
    mutable uint32_t max_queue_len_;
    mutable uint64_t busy_time_;
    mutable bool measure_busy_time_;

    friend class QueueTaskTest;
    friend class QueueTaskShutdownTest;
    friend class QueueTaskWaterMarkTest;
    friend class QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> >;

    DISALLOW_COPY_AND_ASSIGN(WorkQueue);
};

#endif /* __QUEUE_TASK_H__ */
