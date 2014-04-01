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
        current_runner_(NULL),
        on_entry_defer_count_(0),
        disable_(false),
        deleted_(false),
        enqueues_(0),
        dequeues_(0),
        drops_(0),
        max_iterations_(max_iterations),
        size_(size),
        bounded_(false) {
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

    void SetStartRunnerFunc(StartRunnerFunc start_runner_fn) {
        start_runner_ = start_runner_fn;
    }

    void SetBounded(bool bounded) {
        bounded_ = bounded;
    }

    void GetBounded() const {
        return bounded_;
    }

    void SetHighWaterMark(std::vector<WaterMarkInfo> &high_water) {
        high_water_ = high_water;
    }

    void SetHighWaterMark(WaterMarkInfo hwm_info) {
        high_water_.push_back(hwm_info);
    }

    void ResetHighWaterMark() {
        high_water_.clear();
    }

    std::vector<WaterMarkInfo>& GetHighWaterMark() const {
        return high_water_;
    }

    void SetLowWaterMark(std::vector<WaterMarkInfo> &low_water) {
        low_water_ = low_water;
    }

    void SetLowWaterMark(WaterMarkInfo lwm_info) {
        low_water_.push_back(lwm_info);
    }

    void ResetLowWaterMark() {
        low_water_.clear();
    }

    std::vector<WaterMarkInfo>& GetLowWaterMark() const {
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
            ProcessWaterMarks(low_water_, ocount);
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

    Callback GetCallback() const {
    	return callback_;
    }

    void SetEntryCallback(TaskEntryCallback on_entry) {
        on_entry_cb_ = on_entry;
    }

    void SetExitCallback(TaskExitCallback on_exit) {
        on_exit_cb_ = on_exit;
    }

    void set_disable(bool disable) { disable_ = disable; }
    size_t on_entry_defer_count() const { return on_entry_defer_count_; }

    bool OnEntry() {
        // XXX For testing only, defer this task run
        if (disable_) {
            return false;
        }
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

    bool IsQueueEmpty() {
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

    bool EnqueueInternal(QueueEntryT entry) {
        queue_.push(entry);
        enqueues_++;
        MayBeStartRunner();
        size_t ocount = count_.fetch_and_increment();
        ProcessWaterMarks(high_water_, ocount);
        return ocount < (size_ - 1);
    }

    bool EnqueueBounded(QueueEntryT entry) {
        if (count_ < size_ - 1) {
            enqueues_++;
            queue_.push(entry);
            MayBeStartRunner();
            size_t ocount = count_.fetch_and_increment();
            ProcessWaterMarks(high_water_, ocount);
            return true;
        }
        drops_++;
        return false;
    }

    bool RunnerDone() {
        tbb::mutex::scoped_lock lock(mutex_);
        bool done = false;
        if (queue_.empty()) {
            done = true; 
            OnExit(done);
            current_runner_ = NULL;         
            running_ = false;
        } else if (!start_runner_.empty() && !start_runner_()) {
            done = true;
            OnExit(done);
            current_runner_ = NULL;
            running_ = false;
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
    bool disable_;
    bool deleted_;
    size_t enqueues_;
    size_t dequeues_;
    size_t drops_;
    size_t max_iterations_;
    size_t size_;
    bool bounded_;
    std::vector<WaterMarkInfo> high_water_; // When queue count goes above
    std::vector<WaterMarkInfo> low_water_; // When queue count goes below 

    friend class QueueTaskTest;
    friend class QueueTaskRunner<QueueEntryT, WorkQueue<QueueEntryT> >;

    DISALLOW_COPY_AND_ASSIGN(WorkQueue);
};

#endif /* __QUEUE_TASK_H__ */
