/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
#include <fstream>
#include <map>
#include <iostream>
#include <boost/intrusive/set.hpp>

#include "tbb/atomic.h"
#include "tbb/task.h"
#include "tbb/enumerable_thread_specific.h"
#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_tbbkeepawake.h"
#include "base/task_monitor.h"

#include <base/sandesh/task_types.h>

#if defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <libprocstat.h>
#endif

using namespace std;
using tbb::task;

int TaskScheduler::ThreadAmpFactor_ = 1;
class TaskEntry;
struct TaskDeferEntryCmp;

typedef tbb::enumerable_thread_specific<Task *> TaskInfo;

static TaskInfo task_running;

// Vector of Task entries
typedef std::vector<TaskEntry *> TaskEntryList;

boost::scoped_ptr<TaskScheduler> TaskScheduler::singleton_;

#define TASK_TRACE(scheduler, task, msg, delay)\
    do {\
        scheduler->Log(__FILE__, __LINE__, task, msg, delay);\
    } while(false);\

// Private class used to implement tbb::task
// An object is created when task is ready for execution and 
// registered with tbb::task
class TaskImpl : public tbb::task {
public:
    TaskImpl(Task *t) : parent_(t) {};
    virtual ~TaskImpl();

private:
    tbb::task *execute();

    Task    *parent_;

    DISALLOW_COPY_AND_ASSIGN(TaskImpl);
};

// Information maintained for every <task, instance>
// policyq_  : contains,
//      - Policies configured for a task
//      - Complementary policies for a task. 
//          Example, if a policy is of form <tid0> => <tid1, inst1> <tid2, -1>
//          <tid1, inst1> cannot run if <tid0, inst1> is running
//          <tid2, *> cannot run when <tid0, *> are running. These become 
//          complementary rule
// waitq_   : Tasks of this instance created and waiting to be executed. Tasks 
//            are stored and executed in order of their creation
//            Task can be added here on Enqueue if policy conditions are not 
//            met. Its taken out from waitq_ only when its about to Run
// deferq_  : Tree of TaskEntry waiting on this task instance. The TaskEntry.
//            This tree is populated if all conditions are met
//            - This TaskEntry has tasks created
//            - The TaskEntry in deferq_ has tasks created
//            - The Tree is sorted on task seqno_
// run_task_ : Task running in context of this TaskEntry. Only entries in 
//            task_entry_db_ have this set. Entries in task_db_ will always
//            have this as NULL
//            Running task is not in waitq_ or deferq_
// run_count_: Number of running tasks for this TaskEntry
class TaskEntry {
public:
    TaskEntry(int task_id);
    TaskEntry(int task_id, int task_instance);
    ~TaskEntry();

    void AddPolicy(TaskEntry *entry);
    int WaitQSize() const {return waitq_.size();};
    void AddToWaitQ(Task *t);
    bool DeleteFromWaitQ(Task *t);
    void AddToDeferQ(TaskEntry *entry);
    void DeleteFromDeferQ(TaskEntry &entry);
    TaskEntry *ActiveEntryInPolicy();
    bool DeferOnPolicyFail(Task *t);
    void RunTask(Task *t);
    void RunDeferQ();
    void RunCombinedDeferQ();
    void RunWaitQ();
    void RunDeferEntry();
    void RunDeferQForGroupEnable();
    void TaskExited(Task *t, TaskGroup *group);
    TaskStats *GetTaskStats();
    void ClearTaskStats();
    void ClearQueues();
    int GetTaskDeferEntrySeqno() const;
    int GetTaskId() const { return task_id_; }
    int GetTaskInstance() const { return task_instance_; }
    int GetRunCount() const { return run_count_; }
    void SetDisable(bool disable) { disable_ = disable; }
    bool IsDisabled() { return disable_; }
    void GetSandeshData(SandeshTaskEntry *resp) const;

private:
    friend class TaskGroup;
    friend class TaskScheduler;

    // List of Task's in waitq_
    typedef boost::intrusive::member_hook<Task,
            boost::intrusive::list_member_hook<>, &Task::waitq_hook_> WaitQHook;
    typedef boost::intrusive::list<Task, WaitQHook> TaskWaitQ;
    
    boost::intrusive::set_member_hook<> task_defer_node;
    typedef boost::intrusive::member_hook<TaskEntry, 
        boost::intrusive::set_member_hook<>, 
        &TaskEntry::task_defer_node> TaskDeferListOption;
    // It is a tree of TaskEntries deferred and waiting on the containing task
    // to exit. The tree is sorted by seqno_ of first task in the TaskEntry
    typedef boost::intrusive::set<TaskEntry, TaskDeferListOption,
        boost::intrusive::compare<TaskDeferEntryCmp> > TaskDeferList;

    int             task_id_;
    int             task_instance_;
    int             run_count_; // # of tasks running

    Task            *run_task_; // Task currently running
    TaskWaitQ       waitq_;     // Tasks waiting to run on some condition
    TaskEntryList   policyq_;   // Policy rules for a task
    TaskDeferList   *deferq_;    // Tasks deferred for this to exit
    TaskEntry       *deferq_task_entry_;
    TaskGroup       *deferq_task_group_;
    bool            disable_;

    // Cummulative Maintenance stats
    TaskStats       stats_;

    DISALLOW_COPY_AND_ASSIGN(TaskEntry);
};

// Comparison routine for the TaskDeferList 
struct TaskDeferEntryCmp {
    bool operator() (const TaskEntry &lhs, const TaskEntry &rhs) const {
        return (lhs.GetTaskDeferEntrySeqno() < 
                rhs.GetTaskDeferEntrySeqno());
    }
};

// TaskGroup maintains per <task-id> information including,
// polic_set_   : Boolean used to ensure policy is set only once per task
//                Task policy change is not yet supported
// policy_      : List of policy rules for the task
// run_count_   : Number of tasks running in context of this task-group
// deferq_      : Tasks deferred till run_count_ on this task becomes 0
// task_entry_  : Default TaskEntry used for task without an instance
// disable_entry_ : TaskEntry which maintains a deferQ for tasks enqueued
//                  while TaskGroup is disabled
class TaskGroup {
public:
    TaskGroup(int task_id);
    ~TaskGroup();

    TaskEntry *QueryTaskEntry(int task_instance) const;
    TaskEntry *GetTaskEntry(int task_instance);
    void AddPolicy(TaskGroup *group);
    void AddToDeferQ(TaskEntry *entry);
    void AddToDisableQ(TaskEntry *entry);
    void AddEntriesToDisableQ();
    TaskEntry *GetDisableEntry() { return disable_entry_; }
    void DeleteFromDeferQ(TaskEntry &entry);
    TaskGroup *ActiveGroupInPolicy();
    bool DeferOnPolicyFail(TaskEntry *entry, Task *t);
    bool IsWaitQEmpty();
    int  TaskRunCount() const {return run_count_;};
    void RunDeferQ();
    void RunDisableEntries();
    void TaskExited(Task *t);
    void PolicySet();
    void TaskStarted() {run_count_++;};
    void IncrementTotalRunTime(int64_t rtime) { total_run_time_ += rtime; }
    TaskStats *GetTaskGroupStats();
    TaskStats *GetTaskStats();
    TaskStats *GetTaskStats(int task_instance);
    void ClearTaskGroupStats();
    void ClearTaskStats();
    void ClearTaskStats(int instance_id);
    void SetDisable(bool disable) { disable_ = disable; }
    bool IsDisabled() { return disable_; }
    void GetSandeshData(SandeshTaskGroup *resp, bool summary) const;

    int task_id() const { return task_id_; }
    int deferq_size() const { return deferq_.size(); }
    size_t num_tasks() const {
        size_t count = 0;
        for (TaskEntryList::const_iterator it = task_entry_db_.begin();
             it != task_entry_db_.end(); ++it) {
            if (*it != NULL) {
                count++;
            }
        }
        return count;
    }

private:
    friend class TaskEntry;
    friend class TaskScheduler;
    
    // Vector of Task Group policies
    typedef std::vector<TaskGroup *> TaskGroupPolicyList;
    typedef boost::intrusive::member_hook<TaskEntry, 
        boost::intrusive::set_member_hook<>, 
        &TaskEntry::task_defer_node> TaskDeferListOption;
    // It is a tree of TaskEntries deferred and waiting on the containing task
    // to exit. The tree is sorted by seqno_ of first task in the TaskEntry
    typedef boost::intrusive::set<TaskEntry, TaskDeferListOption,
        boost::intrusive::compare<TaskDeferEntryCmp> > TaskDeferList;

    static const int        kVectorGrowSize = 16;
    int                     task_id_;
    bool                    policy_set_;// policy already set?
    int                     run_count_; // # of tasks running in the group
    tbb::atomic<uint64_t>   total_run_time_;

    TaskGroupPolicyList     policy_;    // Policy rules for the group
    TaskDeferList           deferq_;    // Tasks deferred till run_count_ is 0
    TaskEntry               *task_entry_;// Task entry for instance(-1)
    TaskEntry               *disable_entry_;// Task entry for disabled group
    TaskEntryList           task_entry_db_;  // task-entries in this group
    uint32_t                execute_delay_;
    uint32_t                schedule_delay_;
    bool                    disable_;

    TaskStats               stats_;
    DISALLOW_COPY_AND_ASSIGN(TaskGroup);
};

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskImpl 
////////////////////////////////////////////////////////////////////////////

// Method called from tbb::task to execute.
// Invoke Run() method of client.
// Supports task continuation when Run() returns false
tbb::task *TaskImpl::execute() {
    TaskInfo::reference running = task_running.local();
    running = parent_;
    parent_->SetTbbState(Task::TBB_EXEC);
    try {
        uint64_t t = 0;
        if (parent_->enqueue_time() != 0) {
            t = ClockMonotonicUsec();
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            if ((t - parent_->enqueue_time()) >
                scheduler->schedule_delay(parent_)) {
                TASK_TRACE(scheduler, parent_, "TBB schedule time(in usec) ",
                           (t - parent_->enqueue_time()));
            }
        } else if (TaskScheduler::GetInstance()->track_run_time()) {
            t = ClockMonotonicUsec();
        }

        bool is_complete = parent_->Run();
        if (t != 0) {
            int64_t delay = ClockMonotonicUsec() - t;
            TaskScheduler *scheduler = TaskScheduler::GetInstance();
            uint32_t execute_delay = scheduler->execute_delay(parent_);
            if (execute_delay && delay > execute_delay) {
                TASK_TRACE(scheduler, parent_, "Run time(in usec) ", delay);
            }
            if (scheduler->track_run_time()) {
                TaskGroup *group =
                    scheduler->QueryTaskGroup(parent_->GetTaskId());
                group->IncrementTotalRunTime(delay);
            }
        }

        running = NULL;
        if (is_complete == true) {
            parent_->SetTaskComplete();
        } else {
            parent_->SetTaskRecycle();
        }
    } catch (std::exception &e) {

        // Store exception information statically, to easily read exception
        // information from the core.
        static std::string what = e.what();

        LOG(ERROR, "!!!! ERROR !!!! Task caught fatal exception: " << what
            << " TaskImpl: " << this);
        assert(0);
    } catch (...) {
        LOG(ERROR, "!!!! ERROR !!!! Task caught fatal unknown exception"
            << " TaskImpl: " << this);
        assert(0);
    }

    return NULL;
}

// Destructor called when a task execution is compeleted. Invoked
// implicitly by tbb::task. 
// Invokes OnTaskExit to schedule tasks pending tasks
TaskImpl::~TaskImpl() {
    assert(parent_ != NULL);

    TaskScheduler *sched = TaskScheduler::GetInstance();
    sched->OnTaskExit(parent_);
}

// XXX For testing purposes only. Limit the number of tbb worker threads.
int TaskScheduler::GetThreadCount(int thread_count) {
    static bool init_;
    static int num_cores_;

    if (init_) {
        return num_cores_ * ThreadAmpFactor_;
    }

    char *num_cores_str = getenv("TBB_THREAD_COUNT");
    if (!num_cores_str) {
        if (thread_count == 0)
            num_cores_ = tbb::task_scheduler_init::default_num_threads();
        else
            num_cores_ = thread_count;
    } else {
        num_cores_ = strtol(num_cores_str, NULL, 0);
    }

    init_ = true;
    return num_cores_ * ThreadAmpFactor_;
}

int TaskScheduler::GetDefaultThreadCount() {
    return tbb::task_scheduler_init::default_num_threads();
}

bool TaskScheduler::ShouldUseSpawn() {
    if (getenv("TBB_USE_SPAWN"))
        return true;

    return false;
}

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskScheduler 
////////////////////////////////////////////////////////////////////////////

// TaskScheduler constructor.
// TBB assumes it can use the "thread" invoking tbb::scheduler can be used
// for task scheduling. But, in our case we dont want "main" thread to be
// part of tbb. So, initialize TBB with one thread more than its default
TaskScheduler::TaskScheduler(int task_count) : 
    use_spawn_(ShouldUseSpawn()), task_scheduler_(GetThreadCount(task_count) + 1),
    running_(true), seqno_(0), id_max_(0), log_fn_(), track_run_time_(false),
    measure_delay_(false), schedule_delay_(0), execute_delay_(0),
    enqueue_count_(0), done_count_(0), cancel_count_(0), evm_(NULL),
    tbb_awake_task_(NULL), task_monitor_(NULL) {
    hw_thread_count_ = GetThreadCount(task_count);
    task_group_db_.resize(TaskScheduler::kVectorGrowSize);
    stop_entry_ = new TaskEntry(-1);
}

// Free up the task_entry_db_ allocated for scheduler
TaskScheduler::~TaskScheduler() {
    TaskGroup   *group;

    for (TaskGroupDb::iterator iter = task_group_db_.begin();
         iter != task_group_db_.end(); ++iter) {
        if ((group = *iter) == NULL) {
            continue;
        }
        *iter = NULL;
        delete group;
    }

    for (TaskIdMap::iterator loc = id_map_.begin(); loc != id_map_.end();
         id_map_.erase(loc++)) {
    }

    delete stop_entry_;
    stop_entry_ = NULL;
    task_group_db_.clear();

    return;
}

void TaskScheduler::Initialize(uint32_t thread_count, EventManager *evm) {
    assert(singleton_.get() == NULL);
    singleton_.reset(new TaskScheduler((int)thread_count));

    if (evm) {
        singleton_.get()->evm_ = evm;
        singleton_.get()->tbb_awake_task_ = new TaskTbbKeepAwake();
        assert(singleton_.get()->tbb_awake_task_);

        singleton_.get()->tbb_awake_task_->StartTbbKeepAwakeTask(
                                               singleton_.get(), evm,
                                               "TaskScheduler::TbbKeepAwake");
    }
}

void TaskScheduler::set_event_manager(EventManager *evm) {
    assert(evm);
    evm_ = evm;
    if (tbb_awake_task_ == NULL) {
        tbb_awake_task_ = new TaskTbbKeepAwake();
        assert(tbb_awake_task_);

        tbb_awake_task_->StartTbbKeepAwakeTask(this, evm,
            "TaskScheduler::TbbKeepAwake");
    }
}

void TaskScheduler::ModifyTbbKeepAwakeTimeout(uint32_t timeout) {
    if (tbb_awake_task_) {
        tbb_awake_task_->ModifyTbbKeepAwakeTimeout(timeout);
    }
}

void TaskScheduler::EnableMonitor(EventManager *evm,
                                  uint64_t tbb_keepawake_time_msec,
                                  uint64_t inactivity_time_msec,
                                  uint64_t poll_interval_msec) {
    if (task_monitor_ != NULL)
        return;

    task_monitor_ = new TaskMonitor(this, tbb_keepawake_time_msec,
                                    inactivity_time_msec, poll_interval_msec);
    task_monitor_->Start(evm);
}

void TaskScheduler::Log(const char *file_name, uint32_t line_no,
                        const Task *task, const char *description,
                        uint32_t delay) {
    if (log_fn_.empty() == false) {
        log_fn_(file_name, line_no, task, description, delay);
    }
}

void TaskScheduler::RegisterLog(LogFn fn) {
    log_fn_ = fn;
}

uint32_t TaskScheduler::schedule_delay(Task *task) const {
    if (task->schedule_delay() > schedule_delay_)
        return task->schedule_delay();
    return schedule_delay_;
}

uint32_t TaskScheduler::execute_delay(Task *task) const {
    if (task->execute_delay() > execute_delay_)
        return task->execute_delay();
    return execute_delay_;
}

TaskScheduler *TaskScheduler::GetInstance() {
    if (singleton_.get() == NULL) {
        singleton_.reset(new TaskScheduler());
    }
    return singleton_.get();
}

// Get TaskGroup for a task_id. Grows task_entry_db_ if necessary
TaskGroup *TaskScheduler::GetTaskGroup(int task_id) {
    assert(task_id >= 0);
    int size = task_group_db_.size();
    if (size <= task_id) {
        task_group_db_.resize(task_id + TaskScheduler::kVectorGrowSize);
    }

    TaskGroup *group = task_group_db_[task_id];
    if (group == NULL) {
        group = new TaskGroup(task_id);
        task_group_db_[task_id] = group;
    }

    return group;
}

// Query TaskGroup for a task_id.Assumes valid entry is present for task_id
TaskGroup *TaskScheduler::QueryTaskGroup(int task_id) {
    return task_group_db_[task_id];
}

//
// Check if there are any Tasks in the given TaskGroup.
// Assumes that all task ids are mutually exclusive with bgp::Config.
//
bool TaskScheduler::IsTaskGroupEmpty(int task_id) const {
    CHECK_CONCURRENCY("bgp::Config");
    tbb::mutex::scoped_lock lock(mutex_);
    TaskGroup *group = task_group_db_[task_id];
    assert(group);
    assert(group->TaskRunCount() == 0);
    return group->IsWaitQEmpty();
}

// Get TaskGroup for a task_id. Grows task_entry_db_ if necessary
TaskEntry *TaskScheduler::GetTaskEntry(int task_id, int task_instance) {
    TaskGroup *group = GetTaskGroup(task_id);
    return group->GetTaskEntry(task_instance);
}

// Query TaskEntry for a task-id and task-instance 
TaskEntry *TaskScheduler::QueryTaskEntry(int task_id, int task_instance) {
    TaskGroup *group = QueryTaskGroup(task_id);
    if (group == NULL)
        return NULL;
    return group->QueryTaskEntry(task_instance);
}

void TaskScheduler::EnableLatencyThresholds(uint32_t execute,
                                            uint32_t schedule) {
    execute_delay_ = execute;
    schedule_delay_ = schedule;
    measure_delay_ = (execute_delay_ != 0 || schedule_delay_ != 0);
}

void TaskScheduler::SetLatencyThreshold(const std::string &name,
                                        uint32_t execute, uint32_t schedule) {
    int task_id = GetTaskId(name);
    TaskGroup *group = GetTaskGroup(task_id);
    group->execute_delay_ = execute;
    group->schedule_delay_ = schedule;
}

// Sets Policy for a task.
// Adds policy entries for the task
// Example: Policy <tid0> => <tid1, -1> <tid2, inst2> will result in following,
//      task_db_[tid0] : Rule <tid1, -1> is added to policyq
//      task_group_db_[tid0, inst2] : Rule <tid2, inst2> is added to policyq
//
//      The symmetry of policy will result in following additional rules,
//      task_db_[tid1] : Rule <tid0, -1> is added to policyq
//      task_group_db_[tid2, inst2] : Rule <tid0, inst2> is added to policyq
void TaskScheduler::SetPolicy(int task_id, TaskPolicy &policy) {
    tbb::mutex::scoped_lock     lock(mutex_);

    TaskGroup *group = GetTaskGroup(task_id);
    TaskEntry *group_entry = group->GetTaskEntry(-1);
    group->PolicySet();

    for (TaskPolicy::iterator it = policy.begin(); it != policy.end(); ++it) {

        if (it->match_instance == -1) {
            TaskGroup *policy_group = GetTaskGroup(it->match_id);
            group->AddPolicy(policy_group);
            policy_group->AddPolicy(group);
        } else {
            TaskEntry *entry = GetTaskEntry(task_id, it->match_instance);
            TaskEntry *policy_entry = GetTaskEntry(it->match_id,
                                                   it->match_instance);
            entry->AddPolicy(policy_entry);
            policy_entry->AddPolicy(entry);

            group_entry->AddPolicy(policy_entry);
            policy_entry->AddPolicy(group_entry);
        }
    }
}

// Enqueue a Task for running. Starts task if all policy rules are met else 
// puts task in waitq
void TaskScheduler::Enqueue(Task *t) {
    tbb::mutex::scoped_lock     lock(mutex_);

    EnqueueUnLocked(t);
}

void TaskScheduler::EnqueueUnLocked(Task *t) {
    if (measure_delay_) {
        t->enqueue_time_ = ClockMonotonicUsec();
    }
    // Ensure that task is enqueued only once.
    assert(t->GetSeqno() == 0);
    enqueue_count_++;
    t->SetSeqNo(++seqno_);
    TaskGroup *group = GetTaskGroup(t->GetTaskId());
    t->schedule_delay_ = group->schedule_delay_;
    t->execute_delay_ = group->execute_delay_;
    group->stats_.enqueue_count_++;

    TaskEntry *entry = GetTaskEntry(t->GetTaskId(), t->GetTaskInstance());
    entry->stats_.enqueue_count_++;
    // If either TaskGroup or TaskEntry is disabled for Unit-Test purposes,
    // enqueue new task in waitq and update TaskGroup if needed.
    if (group->IsDisabled() || entry->IsDisabled()) {
        entry->AddToWaitQ(t);
        if (group->IsDisabled()) {
            group->AddToDisableQ(entry);
        }
        return;
    }

    // Add task to waitq_ if its already populated
    if (entry->WaitQSize() != 0) {
        entry->AddToWaitQ(t);
        return;
    }

    // Is scheduler stopped? Dont add task to deferq_ if scheduler is stopped.
    // TaskScheduler::Start() will run tasks from waitq_
    if (running_ == false) {
        entry->AddToWaitQ(t);
        stop_entry_->AddToDeferQ(entry);
        return;
    }

    // Check Task Group policy. On policy violation, DeferOnPolicyFail()
    // adds the Task to the TaskEntry's waitq_ and the TaskEntry will be 
    // added to deferq_ of the matching TaskGroup.
    if (group->DeferOnPolicyFail(entry, t)) {
        return;
    }

    // Check Task Entry policy. On policy violation, DeferOnPolicyFail() 
    // adds the Task to the TaskEntry's waitq_ and the TaskEntry will be
    // added to deferq_ of the matching TaskEntry.
    if (entry->DeferOnPolicyFail(t)) {
        return;
    }

    entry->RunTask(t);
    return;
}

// Cancel a Task that can be in RUN/WAIT state.
// [Note]: The caller needs to ensure that the task exists when Cancel() is invoked. 
TaskScheduler::CancelReturnCode TaskScheduler::Cancel(Task *t) {
    tbb::mutex::scoped_lock  lock(mutex_);

    // If the task is in RUN state, mark the task for cancellation and return.
    if (t->state_ == Task::RUN) {
        t->task_cancel_ = true;
    } else if (t->state_ == Task::WAIT) {
        TaskEntry *entry = QueryTaskEntry(t->GetTaskId(), t->GetTaskInstance());
        TaskGroup *group = QueryTaskGroup(t->GetTaskId());
        assert(entry->WaitQSize());
        // Get the first entry in the waitq_
        Task *first_wait_task = &(*entry->waitq_.begin());
        TaskEntry *disable_entry = group->GetDisableEntry();
        assert(entry->DeleteFromWaitQ(t) == true);
        // If the waitq_ is empty, then remove the TaskEntry from the deferq.
        if (!entry->WaitQSize()) {
            if (entry->deferq_task_group_) {
                assert(entry->deferq_task_entry_ == NULL);
                entry->deferq_task_group_->DeleteFromDeferQ(*entry);
            } else if (entry->deferq_task_entry_) {
                entry->deferq_task_entry_->DeleteFromDeferQ(*entry);
            } else if (group->IsDisabled()) {
                // Remove TaskEntry from deferq of disable_entry
                disable_entry->DeleteFromDeferQ(*entry);
            } else {
                if (!entry->IsDisabled()) {
                    assert(0);
                }
            }
        } else if (t == first_wait_task) {
            // TaskEntry is inserted in the deferq_ based on the Task seqno. 
            // deferq_ comparison function uses the seqno of the first entry in
            // the waitq_. Therefore, if the task to be cancelled is the first
            // entry in the waitq_, then delete the entry from the deferq_ and
            // add it again.
            TaskGroup *deferq_tgroup = entry->deferq_task_group_;
            TaskEntry *deferq_tentry = entry->deferq_task_entry_;
            if (deferq_tgroup) {
                assert(deferq_tentry == NULL);
                deferq_tgroup->DeleteFromDeferQ(*entry);
                deferq_tgroup->AddToDeferQ(entry);
            } else if (deferq_tentry) {
                deferq_tentry->DeleteFromDeferQ(*entry);
                deferq_tentry->AddToDeferQ(entry);
            } else if (group->IsDisabled()) {
                // Remove TaskEntry from deferq of disable_entry and add back
                disable_entry->DeleteFromDeferQ(*entry);
                disable_entry->AddToDeferQ(entry);
            } else {
                if (!entry->IsDisabled()) {
                    assert(0);
                }
            }
        }
        delete t;
        cancel_count_++;
        return CANCELLED;
    } else {
        return FAILED;
    }
    return QUEUED;
}

// Method invoked on exit of a Task.
// Exit of a task can potentially start tasks in pendingq.
void TaskScheduler::OnTaskExit(Task *t) {
    tbb::mutex::scoped_lock lock(mutex_);
    done_count_++;

    t->SetTbbState(Task::TBB_DONE);
    TaskEntry *entry = QueryTaskEntry(t->GetTaskId(), t->GetTaskInstance());
    entry->TaskExited(t, GetTaskGroup(t->GetTaskId()));

    //
    // Delete the task it is not marked for recycling or already cancelled.
    //
    if ((t->task_recycle_ == false) || (t->task_cancel_ == true)) {
        // Delete the container Task object, if the 
        // task is not marked to be recycled (or) 
        // if the task is marked for cancellation
        if (t->task_cancel_ == true) {
            t->OnTaskCancel();
        }
        delete t;
        return;
    }

    // Task is being recycled, reset the state, seq_no and TBB task handle
    t->task_impl_ = NULL;
    t->SetSeqNo(0);
    t->SetState(Task::INIT);
    t->SetTbbState(Task::TBB_INIT);
    EnqueueUnLocked(t);
}

void TaskScheduler::Stop() {
    tbb::mutex::scoped_lock             lock(mutex_);

    running_ = false;
}

void TaskScheduler::Start() {
    tbb::mutex::scoped_lock             lock(mutex_);

    running_ = true;

    // Run all tasks that may be suspended
    stop_entry_->RunDeferQ();
    return;
}

void TaskScheduler::Print() {
    for (TaskGroupDb::iterator iter = task_group_db_.begin();
         iter != task_group_db_.end(); ++iter) {
        TaskGroup *group = *iter;
	if (group == NULL) {
	    continue;
	}
        cout << "id: " << group->task_id()
	     << " run: " << group->TaskRunCount() << endl;
        cout << "deferq: " << group->deferq_size()
	     << " task count: " << group->num_tasks() << endl;
    }
}

// Returns true if there are no tasks enqueued and/or running.
// If running_only is true, enqueued tasks are ignored i.e. return true if
// there are no running tasks. Ignore TaskGroup or TaskEntry if it is
// disabled.
bool TaskScheduler::IsEmpty(bool running_only) {
    TaskGroup *group;

    tbb::mutex::scoped_lock lock(mutex_);

    for (TaskGroupDb::iterator it = task_group_db_.begin();
         it != task_group_db_.end(); ++it) {
        if ((group = *it) == NULL) {
            continue;
        }
        if (group->TaskRunCount()) {
            return false;
        }
        if (group->IsDisabled()) {
            continue;
        }
        if ((false == running_only) && (false == group->IsWaitQEmpty())) {
            return false;
        }
    }

    return true;
}
std::string TaskScheduler::GetTaskName(int task_id) const {
    for (TaskIdMap::const_iterator it = id_map_.begin(); it != id_map_.end();
         it++) {
        if (task_id == it->second)
            return it->first;
    }

    return "ERROR";
}

int TaskScheduler::GetTaskId(const string &name) {
    {
        // Grab read-only lock first. Most of the time, task-id already exists
        // in the id_map_. Hence there should not be any contention for lock
        // aquisition.
        tbb::reader_writer_lock::scoped_lock_read lock(id_map_mutex_);
        TaskIdMap::iterator loc = id_map_.find(name);
        if (loc != id_map_.end()) {
            return loc->second;
        }
    }

    // Grab read-write lock to allocate a new task id and insert into the map.
    tbb::reader_writer_lock::scoped_lock lock(id_map_mutex_);
    int tid = ++id_max_;
    id_map_.insert(make_pair(name, tid));
    return tid;
}

void TaskScheduler::ClearTaskGroupStats(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return;

    group->ClearTaskGroupStats();
}

void TaskScheduler::ClearTaskStats(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return;

    group->ClearTaskStats();
}

void TaskScheduler::ClearTaskStats(int task_id, int instance_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return;

    group->ClearTaskStats(instance_id);
}

TaskStats *TaskScheduler::GetTaskGroupStats(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return NULL;

    return group->GetTaskGroupStats();
}

TaskStats *TaskScheduler::GetTaskStats(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return NULL;

    return group->GetTaskStats();
}

TaskStats *TaskScheduler::GetTaskStats(int task_id, int instance_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (group == NULL)
        return NULL;

    return group->GetTaskStats(instance_id);
}

//
// Platfrom-dependent subroutine in Linux and FreeBSD implementations,
// used only in TaskScheduler::WaitForTerminateCompletion()
//
// In Linux, make sure that all the [tbb] threads launched have completely
// exited. We do so by looking for the Threads count of this process in
// /proc/<pid>/status
//
// In FreeBSD use libprocstat to check how many threads is running
// in specific process.
//
int TaskScheduler::CountThreadsPerPid(pid_t pid) {
    int threads;
    threads = 0;

#if defined(__FreeBSD__)
    struct kinfo_proc *ki_proc;
    struct procstat *pstat;
    unsigned int count_procs;


    count_procs = 0;

    pstat = procstat_open_sysctl();
    if(pstat == NULL) {
        LOG(ERROR, "procstat_open_sysctl() failed");
        return -1;
    }

    ki_proc = procstat_getprocs(pstat, KERN_PROC_PID, pid, &count_procs);
    if (ki_proc == NULL) {
        LOG(ERROR, "procstat_open_sysctl() failed");
        return -1;
    }

    if (count_procs != 0)
        procstat_getprocs(pstat, KERN_PROC_PID | KERN_PROC_INC_THREAD,
                            ki_proc->ki_pid, &threads);

    procstat_freeprocs(pstat, ki_proc);
    procstat_close(pstat);

#elif defined(__linux__)
    std::ostringstream file_name;
    std::string line;

    file_name << "/proc/" << pid << "/status";

    std::ifstream file(file_name.str().c_str());

    if(!file) {
        LOG(ERROR, "opening /proc failed");
        return -1;
    }

    while (threads == 0 && file.good()) {
        getline(file, line);
        if (line == "Threads:\t1") threads = 1;
    }
    file.close();
#else
#error "TaskScheduler::CountThreadsPerPid() - unsupported platform."
#endif

    return threads;
}

void TaskScheduler::WaitForTerminateCompletion() {
    //
    // Wait for a bit to give a chance for all the threads to exit
    //
    usleep(1000);

    int count = 0;
    int threadsRunning;
    pid_t pid = getpid();

    while (count++ < 12000) {
        threadsRunning = CountThreadsPerPid(pid);

        if (threadsRunning == 1)
            break;

        if (threadsRunning == -1) {
            LOG(ERROR, "could not check if any thread is running");
            usleep(10000);
            break;
        }

        usleep(10000);
    }
}

void TaskScheduler::Terminate() {
    if (task_monitor_) {
        task_monitor_->Terminate();
        delete task_monitor_;
        task_monitor_ = NULL;
    }

    for (int i = 0; i < 10000; i++) {
        if (IsEmpty()) break;
        usleep(1000);
    }
    assert(IsEmpty());
    if (tbb_awake_task_) {
        tbb_awake_task_->ShutTbbKeepAwakeTask();
        delete tbb_awake_task_;
        tbb_awake_task_ = NULL;
    }
    evm_ = NULL;
    singleton_->task_scheduler_.terminate();
    WaitForTerminateCompletion();
    singleton_.reset(NULL);
}

// XXX This function should not be called in production code.
// It is only for unit testing to control current running task
// This function modifies the running task as specified by the input
void TaskScheduler::SetRunningTask(Task *unit_test) {
    TaskInfo::reference running = task_running.local();
    running = unit_test;
}

void TaskScheduler::ClearRunningTask() {
    TaskInfo::reference running = task_running.local();
    running = NULL;
}

// following function allows one to increase max num of threads used by
// TBB
void TaskScheduler::SetThreadAmpFactor(int n) {
    ThreadAmpFactor_ = n;
}

void TaskScheduler::DisableTaskGroup(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    if (!group->IsDisabled()) {
        // Add TaskEntries(that contain enqueued tasks) which are already
        // disabled to disable_ entry maintained at TaskGroup.
        group->SetDisable(true);
        group->AddEntriesToDisableQ();
    }
}

void TaskScheduler::EnableTaskGroup(int task_id) {
    TaskGroup *group = GetTaskGroup(task_id);
    group->SetDisable(false);
    // Run tasks that maybe suspended
    group->RunDisableEntries();
}

void TaskScheduler::DisableTaskEntry(int task_id, int instance_id) {
    TaskEntry *entry = GetTaskEntry(task_id, instance_id);
    entry->SetDisable(true);
}

void TaskScheduler::EnableTaskEntry(int task_id, int instance_id) {
    TaskEntry *entry = GetTaskEntry(task_id, instance_id);
    entry->SetDisable(false);
    TaskGroup *group = GetTaskGroup(task_id);
    // If group is still disabled, do not schedule the task. Task will be
    // scheduled for run when TaskGroup is enabled.
    if (group->IsDisabled()) {
        return;
    }
    // Run task instances that maybe suspended
    if (entry->WaitQSize() != 0) {
        entry->RunDeferEntry();
    }
}

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskGroup 
////////////////////////////////////////////////////////////////////////////

TaskGroup::TaskGroup(int task_id) : task_id_(task_id), policy_set_(false), 
    run_count_(0), execute_delay_(0), schedule_delay_(0), disable_(false) {
    total_run_time_ = 0;
    task_entry_db_.resize(TaskGroup::kVectorGrowSize);
    task_entry_ = new TaskEntry(task_id);
    memset(&stats_, 0, sizeof(stats_));
    disable_entry_ = new TaskEntry(task_id);
}

TaskGroup::~TaskGroup() {
    policy_.clear();
    deferq_.clear();

    delete task_entry_;
    task_entry_ = NULL;

    for (size_t i = 0; i < task_entry_db_.size(); i++) {
        if (task_entry_db_[i] != NULL) {
            delete task_entry_db_[i];
            task_entry_db_[i] = NULL;
        }
    }

    delete disable_entry_;
    disable_entry_ = NULL;
    task_entry_db_.clear();
}

TaskEntry *TaskGroup::GetTaskEntry(int task_instance) {
    if (task_instance == -1)
        return task_entry_;

    int size = task_entry_db_.size();
    if (size <= task_instance) {
        task_entry_db_.resize(task_instance + TaskGroup::kVectorGrowSize);
    }

    TaskEntry *entry = task_entry_db_.at(task_instance);
    if (entry == NULL) {
        entry = new TaskEntry(task_id_, task_instance);
        task_entry_db_[task_instance] = entry;
    }

    return entry;
}

TaskEntry *TaskGroup::QueryTaskEntry(int task_instance) const {
    if (task_instance == -1) {
        return task_entry_;
    }

    if (task_instance >= (int)task_entry_db_.size())
        return NULL;

    return task_entry_db_[task_instance];
}

void TaskGroup::AddPolicy(TaskGroup *group) {
    policy_.push_back(group);
}

TaskGroup *TaskGroup::ActiveGroupInPolicy() {
    for (TaskGroupPolicyList::iterator it = policy_.begin();
         it != policy_.end(); ++it) {
        if ((*it)->run_count_ != 0) {
            return (*it);
        }
    }
    return NULL;
}

bool TaskGroup::DeferOnPolicyFail(TaskEntry *entry, Task *task) {
    TaskGroup *group;
    if ((group = ActiveGroupInPolicy()) != NULL) {
        // TaskEntry is inserted in the deferq_ based on the Task seqno. 
        // deferq_ comparison function uses the seqno of the first Task queued
        // in the waitq_. Therefore, add the Task to waitq_ before adding
        // TaskEntry in the deferq_.
        if (0 == entry->WaitQSize()) {
            entry->AddToWaitQ(task);
        }
        group->AddToDeferQ(entry);
        return true;
    }
    return false;
}

// Add task to deferq_
// Only one task of a given instance goes into deferq_ for its policies.
void TaskGroup::AddToDeferQ(TaskEntry *entry) {
    stats_.defer_count_++;
    deferq_.insert(*entry);
    assert(entry->deferq_task_group_ == NULL);
    entry->deferq_task_group_ = this;
}

// Delete task from deferq_
void TaskGroup::DeleteFromDeferQ(TaskEntry &entry) {
    assert(this == entry.deferq_task_group_);
    deferq_.erase(deferq_.iterator_to(entry));
    entry.deferq_task_group_ = NULL;
}

// Enqueue TaskEntry in disable_entry's deferQ
void TaskGroup::AddToDisableQ(TaskEntry *entry) {
    disable_entry_->AddToDeferQ(entry);
}

void TaskGroup::PolicySet() {
    assert(policy_set_ == false);
    policy_set_ = true;
}

// Start executing tasks from deferq_ of a TaskGroup
void TaskGroup::RunDeferQ() {
    TaskDeferList::iterator     it;

    it = deferq_.begin(); 
    while (it != deferq_.end()) {
        TaskEntry &entry = *it;
        TaskDeferList::iterator it_work = it++;
        DeleteFromDeferQ(*it_work);
        entry.RunDeferEntry();
    }

    return;
}

inline void TaskGroup::TaskExited(Task *t) {
    run_count_--;
    stats_.total_tasks_completed_++;
}

void TaskGroup::RunDisableEntries() {
    // Run tasks that maybe suspended. Schedule tasks only for
    // TaskEntries which are enabled.
    disable_entry_->RunDeferQForGroupEnable();
}

// Add TaskEntries to disable_entry_ which have tasks enqueued and are already
// disabled.
void TaskGroup::AddEntriesToDisableQ() {
    TaskEntry *entry;
    if (task_entry_->WaitQSize()) {
        AddToDisableQ(task_entry_);
    }

    // Walk thru the task_entry_db_ and add if waitq is non-empty
    for (TaskEntryList::iterator it = task_entry_db_.begin();
         it != task_entry_db_.end(); ++it) {
        if ((entry = *it) == NULL) {
            continue;
        }
        if (entry->WaitQSize()) {
            AddToDisableQ(entry);
        }
    }
}

// Returns true, if the waiq_ of all the tasks in the group are empty.
//
// Note: This function is invoked from TaskScheduler::IsEmpty() for each
// task group and is intended to be invoked only in the test code. If this
// function needs to be used outside test code, then we may want to consider
// storing the waitq_ count for performance reason.
bool TaskGroup::IsWaitQEmpty() {
    TaskEntry *entry;

    // Check the waitq_ of the instance -1
    if (task_entry_->WaitQSize()) {
        return false;
    }

    // Walk thru the task_entry_db_ until waitq_ of any of the task is non-zero
    for (TaskEntryList::iterator it = task_entry_db_.begin();
         it != task_entry_db_.end(); ++it) {
        if ((entry = *it) == NULL) {
            continue;
        }
        if (entry->IsDisabled()) {
            continue;
        }
        if (entry->WaitQSize()) {
            return false;
        }
    }

    // Well, no task has been enqueued in this task group
    return true;
}

void TaskGroup::ClearTaskGroupStats() {
    memset(&stats_, 0, sizeof(stats_));
}

void TaskGroup::ClearTaskStats() {
    task_entry_->ClearTaskStats();
}

void TaskGroup::ClearTaskStats(int task_instance) {
    TaskEntry *entry = QueryTaskEntry(task_instance);
    if (entry != NULL)
        entry->ClearTaskStats();
}

TaskStats *TaskGroup::GetTaskGroupStats() {
    return &stats_;
}

TaskStats *TaskGroup::GetTaskStats() {
    return task_entry_->GetTaskStats();
}

TaskStats *TaskGroup::GetTaskStats(int task_instance) {
    TaskEntry *entry = QueryTaskEntry(task_instance);
    return entry->GetTaskStats();
}

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskEntry 
////////////////////////////////////////////////////////////////////////////

TaskEntry::TaskEntry(int task_id, int task_instance) : task_id_(task_id),
    task_instance_(task_instance), run_count_(0), run_task_(NULL),
    waitq_(), deferq_task_entry_(NULL), deferq_task_group_(NULL),
    disable_(false) {
    // When a new TaskEntry is created, adds an implicit rule into policyq_ to
    // ensure that only one Task of an instance is run at a time
    if (task_instance != -1) {
        policyq_.push_back(this);
    }
    memset(&stats_, 0, sizeof(stats_));
    // allocate memory for deferq
    deferq_ = new TaskDeferList;
}

TaskEntry::TaskEntry(int task_id) : task_id_(task_id),
    task_instance_(-1), run_count_(0), run_task_(NULL),
    deferq_task_entry_(NULL), deferq_task_group_(NULL), disable_(false) {
    memset(&stats_, 0, sizeof(stats_));
    // allocate memory for deferq
    deferq_ = new TaskDeferList;
}

TaskEntry::~TaskEntry() {
    policyq_.clear();

    assert(0 == deferq_->size());
    delete deferq_;
}

void TaskEntry::AddPolicy(TaskEntry *entry) {
    policyq_.push_back(entry);
}

TaskEntry *TaskEntry::ActiveEntryInPolicy() {
    for (TaskEntryList::iterator it = policyq_.begin(); it != policyq_.end();
         ++it) {
        if ((*it)->run_count_ != 0) {
            return (*it);
        }
    }

    return NULL;
}

bool TaskEntry::DeferOnPolicyFail(Task *task) {
    TaskEntry *policy_entry;

    if ((policy_entry = ActiveEntryInPolicy()) != NULL) {
        // TaskEntry is inserted in the deferq_ based on the Task seqno. 
        // deferq_ comparison function uses the seqno of the first Task queued
        // in the waitq_. Therefore, add the Task to waitq_ before adding
        // TaskEntry in the deferq_.
        if (0 == WaitQSize()) {
            AddToWaitQ(task);
        }
        policy_entry->AddToDeferQ(this);
        return true;
    }
    return false;
}

void TaskEntry::AddToWaitQ(Task *t) {
    t->SetState(Task::WAIT);
    stats_.wait_count_++;
    waitq_.push_back(*t);

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskGroup *group = scheduler->GetTaskGroup(task_id_);
    group->stats_.wait_count_++;
}

bool TaskEntry::DeleteFromWaitQ(Task *t) {
    TaskWaitQ::iterator it = waitq_.iterator_to(*t);
    waitq_.erase(it);
    return true;
}

// Add task to deferq_
// Only one task of a given instance goes into deferq_ for its policies.
void TaskEntry::AddToDeferQ(TaskEntry *entry) {
    stats_.defer_count_++;
    deferq_->insert(*entry);
    assert(entry->deferq_task_entry_ == NULL);
    entry->deferq_task_entry_ = this;
}

// Delete task from deferq_
void TaskEntry::DeleteFromDeferQ(TaskEntry &entry) {
    assert(this == entry.deferq_task_entry_);
    deferq_->erase(deferq_->iterator_to(entry));
    entry.deferq_task_entry_ = NULL;
}

// Start a single task.
// If there are more entries in waitq_ add them to deferq_
void TaskEntry::RunTask (Task *t) {
    stats_.run_count_++;
    if (t->GetTaskInstance() != -1) {
        assert(run_task_ == NULL);
        assert (run_count_ == 0);
        run_task_ = t;
    }

    run_count_++;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskGroup *group = scheduler->QueryTaskGroup(t->GetTaskId());
    group->TaskStarted();

    t->StartTask(scheduler);
}

void TaskEntry::RunWaitQ() {
    if (waitq_.size() == 0)
        return;

    TaskWaitQ::iterator it = waitq_.begin();

    if (task_instance_ != -1) {
        Task *t = &(*it);
        DeleteFromWaitQ(t);
        RunTask(t);
        // If there are more tasks in waitq_, put them in deferq_
        if (waitq_.size() != 0) {
            AddToDeferQ(this);
        }
    } else {
        // Run all instances in waitq_
        while (it != waitq_.end()) {
            Task *t = &(*it);
            DeleteFromWaitQ(t);
            RunTask(t);
            if (waitq_.size() == 0)
                break;
            it = waitq_.begin();
        }
    }
}

void TaskEntry::RunDeferEntry() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskGroup *group = scheduler->GetTaskGroup(task_id_);

    // Sanity check
    assert(waitq_.size());
    Task *task = &(*waitq_.begin());

    // Check Task group policies
    if (group->DeferOnPolicyFail(this, task)) {
        return;
    }

    // Check Task entry policies
    if (DeferOnPolicyFail(task)) {
        return;
    }

    RunWaitQ();
    return;
}

// Start executing tasks from deferq_ of a TaskEntry
void TaskEntry::RunDeferQ() {
    TaskDeferList::iterator     it;

    it = deferq_->begin(); 
    while (it != deferq_->end()) {
        TaskEntry &entry = *it;
        TaskDeferList::iterator it_work = it++;
        DeleteFromDeferQ(*it_work);
        entry.RunDeferEntry();
    }

    return;
}

// Start executing tasks from deferq_ of TaskEntries which are enabled
void TaskEntry::RunDeferQForGroupEnable() {
    TaskDeferList::iterator     it;

    it = deferq_->begin();
    while (it != deferq_->end()) {
        TaskEntry &entry = *it;
        TaskDeferList::iterator it_work = it++;
        DeleteFromDeferQ(*it_work);
        if (!entry.IsDisabled()) {
            entry.RunDeferEntry();
        }
    }

    return;
}
// Start executing tasks from deferq_ of TaskEntry and TaskGroup in the temporal order
void TaskEntry::RunCombinedDeferQ() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskGroup *group = scheduler->QueryTaskGroup(task_id_);
    TaskDeferEntryCmp defer_entry_compare;
    
    TaskDeferList::iterator group_it = group->deferq_.begin();
    TaskDeferList::iterator entry_it = deferq_->begin();

    // Loop thru the deferq_ of TaskEntry and TaskGroup in the temporal order.
    // Exit the loop when any of the queues become empty.
    while ((group_it != group->deferq_.end()) && 
           (entry_it != deferq_->end())) {
        TaskEntry &g_entry = *group_it;
        TaskEntry &t_entry = *entry_it;

        if (defer_entry_compare(g_entry, t_entry)) { 
            TaskDeferList::iterator group_it_work = group_it++;
            group->DeleteFromDeferQ(*group_it_work);
            g_entry.RunDeferEntry();
        } else {
            TaskDeferList::iterator entry_it_work = entry_it++;
            DeleteFromDeferQ(*entry_it_work);
            t_entry.RunDeferEntry();
        }
    }

    // Now, walk thru the non-empty deferq_
    if (group_it != group->deferq_.end()) {
        group->RunDeferQ();
    } else if (entry_it != deferq_->end()) {
        RunDeferQ();
    }
}

void TaskEntry::TaskExited(Task *t, TaskGroup *group) {
    if (task_instance_ != -1) {
        assert(run_task_ == t);
        run_task_ = NULL;
        assert(run_count_ == 1);
    }
    
    run_count_--;
    stats_.total_tasks_completed_++;
    group->TaskExited(t);

    if (!group->run_count_ && !run_count_) {
        RunCombinedDeferQ();
    } else if (!group->run_count_) {
        group->RunDeferQ();
    } else if (!run_count_) {
        RunDeferQ();
    }
}

void TaskEntry::ClearQueues() {
    deferq_->clear();
    policyq_.clear();
    waitq_.clear();
}

void TaskEntry::ClearTaskStats() {
    memset(&stats_, 0, sizeof(stats_));
}

TaskStats *TaskEntry::GetTaskStats() {
    return &stats_;
}

// Addition/deletion of TaskEntry in the deferq_ is based on the seqno. 
// seqno of the first Task in the waitq_ is used as the key. This function 
// would be invoked by the comparison function during addition/deletion 
// of TaskEntry in the deferq_.
int TaskEntry::GetTaskDeferEntrySeqno() const {
    if(waitq_.size()) {
        const Task *task = &(*waitq_.begin());
        return task->GetSeqno();
    }
    return -1;
}

////////////////////////////////////////////////////////////////////////////
// Implementation for class Task
////////////////////////////////////////////////////////////////////////////
Task::Task(int task_id, int task_instance) : task_id_(task_id),
    task_instance_(task_instance), task_impl_(NULL), state_(INIT),
    tbb_state_(TBB_INIT), seqno_(0), task_recycle_(false), task_cancel_(false),
    enqueue_time_(0), schedule_time_(0), execute_delay_(0), schedule_delay_(0) {
}

Task::Task(int task_id) : task_id_(task_id),
    task_instance_(-1), task_impl_(NULL), state_(INIT), tbb_state_(TBB_INIT),
    seqno_(0), task_recycle_(false), task_cancel_(false), enqueue_time_(0),
    schedule_time_(0), execute_delay_(0), schedule_delay_(0) {
}

// Start execution of task
void Task::StartTask(TaskScheduler *scheduler) {
    if (enqueue_time_ != 0) {
        schedule_time_ = ClockMonotonicUsec();
        if ((schedule_time_ - enqueue_time_) >
            scheduler->schedule_delay(this)) {
            TASK_TRACE(scheduler, this, "Schedule delay(in usec) ",
                       (schedule_time_ - enqueue_time_));
        }
    }
    assert(task_impl_ == NULL);
    SetState(RUN);
    SetTbbState(TBB_ENQUEUED);
    task_impl_ = new (task::allocate_root())TaskImpl(this);
    if (scheduler->use_spawn()) {
        task::spawn(*task_impl_);
    } else {
        task::enqueue(*task_impl_);
    }
}

Task *Task::Running() {
    TaskInfo::reference running = task_running.local();
    return running;
}

ostream& operator<<(ostream& out, const Task &t) {
    out <<  "Task <" << t.task_id_ << "," << t.task_instance_ << ":" 
        << t.seqno_ << "> ";
    return out;
}

////////////////////////////////////////////////////////////////////////////
// Implementation for sandesh APIs for Task
////////////////////////////////////////////////////////////////////////////
void TaskEntry::GetSandeshData(SandeshTaskEntry *resp) const {
    resp->set_instance_id(task_instance_);
    resp->set_tasks_created(stats_.enqueue_count_);
    resp->set_total_tasks_completed(stats_.total_tasks_completed_);
    resp->set_tasks_running(run_count_);
    resp->set_waitq_size(waitq_.size());
    resp->set_deferq_size(deferq_->size());
}
void TaskGroup::GetSandeshData(SandeshTaskGroup *resp, bool summary) const {
    if (total_run_time_)
        resp->set_total_run_time(duration_usecs_to_string(total_run_time_));

    std::vector<SandeshTaskEntry> list;
    TaskEntry *task_entry = QueryTaskEntry(-1);
    if (task_entry) {
        SandeshTaskEntry entry_resp;
        task_entry->GetSandeshData(&entry_resp);
        list.push_back(entry_resp);
    }
    for (TaskEntryList::const_iterator it = task_entry_db_.begin();
         it != task_entry_db_.end(); ++it) {
        task_entry = *it;
        if (task_entry) {
            SandeshTaskEntry entry_resp;
            task_entry->GetSandeshData(&entry_resp);
            list.push_back(entry_resp);
        }
    }
    resp->set_task_entry_list(list);

    if (summary)
        return;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    std::vector<SandeshTaskPolicyEntry> policy_list;
    for (TaskGroupPolicyList::const_iterator it = policy_.begin();
         it != policy_.end(); ++it) {
        SandeshTaskPolicyEntry policy_entry;
        policy_entry.set_task_name(scheduler->GetTaskName((*it)->task_id_));
        policy_entry.set_tasks_running((*it)->run_count_);
        policy_list.push_back(policy_entry);
    }
    resp->set_task_policy_list(policy_list);
}

void TaskScheduler::GetSandeshData(SandeshTaskScheduler *resp, bool summary) {
    tbb::mutex::scoped_lock lock(mutex_);

    resp->set_running(running_);
    resp->set_use_spawn(use_spawn_);
    resp->set_total_count(seqno_);
    resp->set_thread_count(hw_thread_count_);

    std::vector<SandeshTaskGroup> list;
    for (TaskIdMap::const_iterator it = id_map_.begin(); it != id_map_.end();
         it++) {
        SandeshTaskGroup resp_group;
        TaskGroup *group = QueryTaskGroup(it->second);
        resp_group.set_task_id(it->second);
        resp_group.set_name(it->first);
        if (group)
            group->GetSandeshData(&resp_group, summary);
        list.push_back(resp_group);
    }
    resp->set_task_group_list(list);
}
