/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

// Task is a wrapper over tbb::task to support policies.
//
// There are two kind of tasks,
// - <task-id, instance-id> specifies task with a given instance-id
// - <task-id> specifies task without any instance
//
// The policies can be specified in the form of,
// task(tid0) => <tid1, -1> <tid2, 2> <tid3, 3>
// The rule implies that,
// - Task <tid0, *> cannot run as long as <tid1, *> is running
// - Task <tid0, 2> cannot run as long as task <tid2, 2> is running
// - Task <tid0, 3> cannot run as long as task <tid3, 3> is running
//
// The policy rules are symmetric. That is,
// - Task <tid1, *> cannot run as long as <tid0, *> is running
// - Task <tid2, 2> cannot run as long as task <tid0, 2> is running
// - Task <tid3, 3> cannot run as long as task <tid0, 3> is running
//
// If task_instance == -1, means instance is not applicable.
// It implies that, any number of tasks with instance -1 can run at a time
//
// If task_instance != -1, only one task of given instnace can run at a time
//
// When there are multiple tasks ready to run, they are scheduled in their
// order of enqueue

#ifndef ctrlplane_task_h
#define ctrlplane_task_h

#include <boost/scoped_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <map>
#include <vector>
#include <tbb/mutex.h>
#include <tbb/reader_writer_lock.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>
#include "base/util.h"

class TaskGroup;
class TaskEntry;
class SandeshTaskScheduler;
class TaskTbbKeepAwake;
class EventManager;
class TaskMonitor;
class TaskScheduler;

struct TaskStats {
    int     wait_count_;                // #Entries in waitq
    int     run_count_;                 // #Entries currently running
    int     defer_count_;               // #Entries in deferq
    uint64_t enqueue_count_;            // #Tasks enqueued
    uint64_t total_tasks_completed_;    // #Total tasks ran
    uint64_t last_exit_time_;           // #Time stamp of latest exist
};

struct TaskExclusion {
    TaskExclusion(int task_id) : match_id(task_id), match_instance(-1) {}
    TaskExclusion(int task_id, int instance_id)
        : match_id(task_id), match_instance(instance_id) {
    }
    int match_id;               // must be a valid id (>= 0).
    int match_instance;         // -1 (wildcard) or user specified id.
};
typedef std::vector<TaskExclusion> TaskPolicy;

class Task {
public:
    // Task states
    enum State {
        INIT,
        WAIT,
        RUN
    };

    enum TbbState {
        TBB_INIT,
        TBB_ENQUEUED,
        TBB_EXEC,
        TBB_DONE
    };

    const static int kTaskInstanceAny = -1;
    Task(int task_id, int task_instance);
    Task(int task_id);
    virtual ~Task() { };

    // Code to execute
    // Return true if task is completed. Return false to reschedule the task
    virtual bool Run() = 0;

    // Called on task exit, if it is marked for cancellation.
    // If the user wants to do any cleanup on task cancellation,
    // then he/she can overload this function.
    virtual void OnTaskCancel() { };

    // Accessor methods
    State GetState() const { return state_; };
    int GetTaskId() const { return task_id_; };
    int GetTaskInstance() const { return task_instance_; };
    uint64_t GetSeqno() const { return seqno_; };
    friend std::ostream& operator<<(std::ostream& out, const Task &task);

    // return a pointer to the current task the code is executing under.
    static Task *Running();

    bool task_cancelled() const { return task_cancel_; };
    virtual std::string Description() const  = 0;

    uint64_t enqueue_time() const { return enqueue_time_; }
    uint64_t schedule_time() const { return schedule_time_; }
    uint32_t execute_delay() const { return execute_delay_; }
    uint32_t schedule_delay() const { return schedule_delay_; }

private:
    friend class TaskEntry;
    friend class TaskScheduler;
    friend class TaskImpl;
    void SetSeqNo(uint64_t seqno) {seqno_ = seqno;};
    void SetTbbState(TbbState s) { tbb_state_ = s; };
    void SetState(State s) { state_ = s; };
    void SetTaskRecycle() { task_recycle_ = true; };
    void SetTaskComplete() { task_recycle_ = false; };
    void StartTask(TaskScheduler *scheduler);

    int                 task_id_;       // The code path executed by the task.
    int                 task_instance_; // The dataset id within a code path.
    tbb::task           *task_impl_;
    State               state_;
    TbbState            tbb_state_;
    uint64_t            seqno_;
    bool                task_recycle_;
    bool                task_cancel_;
    uint64_t            enqueue_time_;
    uint64_t            schedule_time_;
    uint32_t            execute_delay_;
    uint32_t            schedule_delay_;
    // Hook in intrusive list for TaskEntry::waitq_
    boost::intrusive::list_member_hook<> waitq_hook_;

    DISALLOW_COPY_AND_ASSIGN(Task);
};

// The TaskScheduler keeps track of what tasks are currently schedulable.
// When a task is enqueued it is added to the run queue or the pending queue
// depending as to whether there is a runable or pending task ahead of it
// that violates the mutual exclusion policies.
// When tasks exit the scheduler re-examines the tasks on the pending queue
// which may now be runnable. It is important that this process is efficient
// such that exit events do not scan tasks that are not waiting on a particular
// task id or task instance to have a 0 count.
class TaskScheduler {
public:
    typedef boost::function<void(const char *file_name, uint32_t line_no,
                                 const Task *task, const char *description,
                                 uint32_t delay)> LogFn;

    TaskScheduler(int thread_count = 0);
    ~TaskScheduler();

    static void Initialize(uint32_t thread_count = 0, EventManager *evm = NULL);
    static TaskScheduler *GetInstance();

    // Enqueue a task. This may result in the task being immedietly added to
    // the run queue or to a pending queue. Tasks may not be added to the
    // run queue in violation of their exclusion policy.
    void Enqueue(Task *task);
    void EnqueueUnLocked(Task *task);

    enum CancelReturnCode {
        CANCELLED,
        FAILED,
        QUEUED,
    };
    CancelReturnCode Cancel(Task *task);

    // Set the task exclusion policy.
    void SetPolicy(int task_id, TaskPolicy &policy);

    bool GetRunStatus() { return running_; };
    int GetTaskId(const std::string &name);
    std::string GetTaskName(int task_id) const;

    TaskStats *GetTaskGroupStats(int task_id);
    TaskStats *GetTaskStats(int task_id);
    TaskStats *GetTaskStats(int task_id, int instance_id);
    void ClearTaskGroupStats(int task_id);
    void ClearTaskStats(int task_id);
    void ClearTaskStats(int task_id, int instance_id);

    TaskGroup *GetTaskGroup(int task_id);
    TaskGroup *QueryTaskGroup(int task_id);
    bool IsTaskGroupEmpty(int task_id) const;

    TaskEntry *GetTaskEntry(int task_id, int instance_id);
    TaskEntry *QueryTaskEntry(int task_id, int instance_id);
    void OnTaskExit(Task *task);

    void Stop();                              // Stop scheduling of all tasks
    void Start();                             // Start scheduling of all tasks
    void Print();                             // Debug print routine
    // Returns true if there are no tasks running and/or enqueued
    bool IsEmpty(bool running_only = false);

    void Terminate();

    int HardwareThreadCount() { return hw_thread_count_; }

    // Get number of tbb worker threads.
    static int GetThreadCount(int thread_count = 0);
    static bool ShouldUseSpawn();

    static int GetDefaultThreadCount();

    uint64_t enqueue_count() const { return enqueue_count_; }
    uint64_t done_count() const { return done_count_; }
    uint64_t cancel_count() const { return cancel_count_; }
    // Force number of threads
    void SetMaxThreadCount(int n);
    void GetSandeshData(SandeshTaskScheduler *resp, bool summary);
    void Log(const char *file_name, uint32_t line_no, const Task *task,
             const char *description, uint32_t delay);
    void RegisterLog(LogFn fn);

    void SetTrackRunTime(bool value) { track_run_time_ = value; }
    bool track_run_time() const { return track_run_time_; }

    // Enable logging of tasks exceeding configured latency
    void EnableLatencyThresholds(uint32_t execute, uint32_t schedule);
    uint32_t schedule_delay() const { return schedule_delay_; }
    uint32_t execute_delay() const { return execute_delay_; }

    bool measure_delay() const { return measure_delay_; }
    void SetLatencyThreshold(const std::string &name, uint32_t execute,
                             uint32_t schedule);
    uint32_t schedule_delay(Task *task) const;
    uint32_t execute_delay(Task *task) const;
    void set_event_manager(EventManager *evm);

    void DisableTaskGroup(int task_id);
    void EnableTaskGroup(int task_id);
    void DisableTaskEntry(int task_id, int instance_id);
    void EnableTaskEntry(int task_id, int instance_id);

    void ModifyTbbKeepAwakeTimeout(uint32_t timeout);
    // Enable Task monitoring
    void EnableMonitor(EventManager *evm, uint64_t tbb_keepawake_time_msec,
                       uint64_t inactivity_time_msec,
                       uint64_t poll_interval_msec);
    const TaskMonitor *task_monitor() const { return task_monitor_; }
    const TaskTbbKeepAwake *tbb_awake_task() const { return tbb_awake_task_; }
    bool use_spawn() const { return use_spawn_; }

    // following function allows one to increase max num of threads used by
    // TBB
    static void SetThreadAmpFactor(int n);

private:
    friend class ConcurrencyScope;
    typedef std::vector<TaskGroup *> TaskGroupDb;
    typedef std::map<std::string, int> TaskIdMap;

    static const int        kVectorGrowSize = 16;
    static boost::scoped_ptr<TaskScheduler> singleton_;

    // XXX
    // Following two methods are only for Unit Testing to control
    // current running task. Usage of this method would result in
    // unexpected behavior.
    void SetRunningTask(Task *);
    void ClearRunningTask();
    void WaitForTerminateCompletion();

    int CountThreadsPerPid(pid_t pid);

    // Use spawn() to run a tbb::task instead of enqueue()
    bool                    use_spawn_;
    TaskEntry               *stop_entry_;

    tbb::task_scheduler_init task_scheduler_;
    mutable tbb::mutex      mutex_;
    bool                    running_;
    uint64_t                seqno_;
    TaskGroupDb             task_group_db_;

    tbb::reader_writer_lock id_map_mutex_;
    TaskIdMap               id_map_;
    int                     id_max_;

    LogFn                   log_fn_;
    int                     hw_thread_count_;

    bool                    track_run_time_;
    bool                    measure_delay_;
    // Log if time between enqueue and task-execute exceeds the delay
    uint32_t                schedule_delay_;
    // Log if time taken to execute exceeds the delay
    uint32_t                execute_delay_;

    uint64_t                enqueue_count_;
    uint64_t                done_count_;
    uint64_t                cancel_count_;
    EventManager            *evm_;
    // following variable allows one to increase max num of threads used by
    // TBB
    static int ThreadAmpFactor_;
    TaskTbbKeepAwake *tbb_awake_task_;
    TaskMonitor      *task_monitor_;
    DISALLOW_COPY_AND_ASSIGN(TaskScheduler);
};

#endif
