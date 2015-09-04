/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <assert.h>
#include <fstream>
#include <map>
#include <iostream>
#include <boost/intrusive/set.hpp>

#include "tbb/task.h"
#include "tbb/enumerable_thread_specific.h"
#include "base/logging.h"
#include "base/task.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
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
    void TaskExited(Task *t, TaskGroup *group);
    TaskStats *GetTaskStats();
    void ClearTaskStats();
    void ClearQueues();
    int GetTaskDeferEntrySeqno() const;
    int GetTaskId() const { return task_id_; }
    int GetTaskInstance() const { return task_instance_; }
    int GetRunCount() const { return run_count_; }
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
class TaskGroup {
public:
    TaskGroup(int task_id);
    ~TaskGroup();

    TaskEntry *QueryTaskEntry(int task_instance) const;
    TaskEntry *GetTaskEntry(int task_instance);
    void AddPolicy(TaskGroup *group);
    void AddToDeferQ(TaskEntry *entry);
    void DeleteFromDeferQ(TaskEntry &entry);
    TaskGroup *ActiveGroupInPolicy();
    bool DeferOnPolicyFail(TaskEntry *entry, Task *t);
    bool IsWaitQEmpty();
    int  TaskRunCount() const {return run_count_;};
    void RunDeferQ();
    void TaskExited(Task *t);
    void PolicySet();
    void TaskStarted() {run_count_++;};
    TaskStats *GetTaskGroupStats();
    TaskStats *GetTaskStats();
    TaskStats *GetTaskStats(int task_instance);
    void ClearTaskGroupStats();
    void ClearTaskStats();
    void ClearTaskStats(int instance_id);
    void GetSandeshData(SandeshTaskGroup *resp) const;

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

    TaskGroupPolicyList     policy_;    // Policy rules for the group
    TaskDeferList           deferq_;    // Tasks deferred till run_count_ is 0
    TaskEntry               *task_entry_;// Task entry for instance(-1)
    TaskEntryList           task_entry_db_;  // task-entries in this group

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
    try {
        bool is_complete = parent_->Run();
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

        LOG(DEBUG, "!!!! ERROR !!!! Task caught fatal exception: " << what
            << " TaskImpl: " << this);
        assert(0);
    } catch (...) {
        LOG(DEBUG, "!!!! ERROR !!!! Task caught fatal unknown exception"
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

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskScheduler 
////////////////////////////////////////////////////////////////////////////

// TaskScheduler constructor.
// TBB assumes it can use the "thread" invoking tbb::scheduler can be used
// for task scheduling. But, in our case we dont want "main" thread to be
// part of tbb. So, initialize TBB with one thread more than its default
TaskScheduler::TaskScheduler(int task_count) : 
    task_scheduler_(GetThreadCount(task_count) + 1),
    running_(true), seqno_(0), id_max_(0), enqueue_count_(0), done_count_(0),
    cancel_count_(0) {
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

void TaskScheduler::Initialize() {
    assert(singleton_.get() == NULL);
    singleton_.reset(new TaskScheduler());
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
    // Ensure that task is enqueued only once.
    assert(t->GetSeqno() == 0);
    enqueue_count_++;
    t->SetSeqNo(++seqno_);
    TaskGroup *group = GetTaskGroup(t->GetTaskId());

    TaskEntry *entry = GetTaskEntry(t->GetTaskId(), t->GetTaskInstance());
    entry->stats_.enqueue_count_++;
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
        assert(entry->WaitQSize());
        // Get the first entry in the waitq_
        Task *first_wait_task = &(*entry->waitq_.begin());
        assert(entry->DeleteFromWaitQ(t) == true);
        // If the waitq_ is empty, then remove the TaskEntry from the deferq.
        if (!entry->WaitQSize()) {
            if (entry->deferq_task_group_) {
                assert(entry->deferq_task_entry_ == NULL);
                entry->deferq_task_group_->DeleteFromDeferQ(*entry);
            } else if (entry->deferq_task_entry_) {
                entry->deferq_task_entry_->DeleteFromDeferQ(*entry);
            } else {
                assert(0);
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
            } else {
                assert(0);
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
    t->state_ = Task::INIT;
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
// there are no running tasks.
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
    for (int i = 0; i < 10000; i++) {
        if (IsEmpty()) break;
        usleep(1000);
    }
    assert(IsEmpty());
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

////////////////////////////////////////////////////////////////////////////
// Implementation for class TaskGroup 
////////////////////////////////////////////////////////////////////////////

TaskGroup::TaskGroup(int task_id) : task_id_(task_id), policy_set_(false), 
    run_count_(0) {
    task_entry_db_.resize(TaskGroup::kVectorGrowSize);
    task_entry_ = new TaskEntry(task_id);
    memset(&stats_, 0, sizeof(stats_));
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
    waitq_(), deferq_task_entry_(NULL), deferq_task_group_(NULL) {
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
    deferq_task_entry_(NULL), deferq_task_group_(NULL) {
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

    t->StartTask();
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
    task_instance_(task_instance), task_impl_(NULL), state_(INIT), seqno_(0),
    task_recycle_(false), task_cancel_(false) {
}

Task::Task(int task_id) : task_id_(task_id),
    task_instance_(-1), task_impl_(NULL), state_(INIT), seqno_(0),
    task_recycle_(false), task_cancel_(false) {
}

// Start execution of task
void Task::StartTask() {
    assert(task_impl_ == NULL);
    state_ = RUN;
    task_impl_ = new (task::allocate_root())TaskImpl(this);
    task::spawn(*task_impl_);
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
void TaskGroup::GetSandeshData(SandeshTaskGroup *resp) const {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    std::vector<SandeshTaskEntry> list;
    TaskEntry *task_entry = QueryTaskEntry(-1);
    if (task_entry) {
        SandeshTaskEntry entry_resp;
        task_entry->GetSandeshData(&entry_resp);
        list.push_back(entry_resp);
    }

    std::vector<SandeshTaskEntry> entry_list;
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

void TaskScheduler::GetSandeshData(SandeshTaskScheduler *resp) {
    tbb::mutex::scoped_lock lock(mutex_);

    resp->set_running(running_);
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
            group->GetSandeshData(&resp_group);
        list.push_back(resp_group);
    }
    resp->set_task_group_list(list);
}
