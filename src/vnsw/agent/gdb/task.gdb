#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

define ptask_scheduler
    set $Xscheduler=TaskScheduler::singleton_.px
    printf " Scheduler %p\n", $Xscheduler
    print $Xscheduler->id_map_
    print $Xscheduler->task_group_db_
end

define ptask_group
    set $Xgroup=(TaskGroup *)$arg0
    printf "Task-ID : %d\n", $Xgroup->task_id_
    printf "Running : %d\n", $Xgroup->run_count_
    printf "Summary : Wait ( %d ) Run ( %d ) Defer ( %d )\n", $Xgroup->stats_.wait_count_, $Xgroup->stats_.run_count_, $Xgroup->stats_.defer_count_
    printf "TaskEntry (instance=-1) : <%p, %d>\n", $Xgroup->task_entry_, $Xgroup->task_entry_->run_count_

    printf "TaskEntries : "
    set $Xstart = $Xgroup->task_entry_db_._M_impl._M_start
    set $Xsize = $Xgroup->task_entry_db_._M_impl._M_finish - $Xgroup->task_entry_db_._M_impl._M_start
    set $Xi = 0
    set $Xcnt = 0
    while $Xi < $Xsize
        set $Xentry = *(TaskEntry **)($Xstart + $Xi)
        if $Xentry != 0
            printf " <%p, %2d, %3d> ", $Xentry, $Xentry->task_instance_, $Xentry->run_count_
            set $Xcnt++
        end
        if $Xcnt == 4
            printf "\n         "
            set $Xcnt = 0
        end
        set $Xi++
    end
    printf "\n"


    printf "Policy : "
    set $Xstart = $Xgroup->policy_._M_impl._M_start
    set $Xsize = $Xgroup->policy_._M_impl._M_finish - $Xgroup->policy_._M_impl._M_start
    set $Xi = 0
    set $Xcnt = 0
    while $Xi < $Xsize
        set $XPgroup = *(TaskGroup **)($Xstart + $Xi)
        printf " <%p, %2d, %3d> ", $XPgroup, $XPgroup->task_id_, $XPgroup->run_count_
            set $Xcnt++
        if $Xcnt == 4
            printf "\n         "
            set $Xcnt = 0
        end
        set $Xi++
    end
    printf "\n"
end

define ptask_entry
    set $Xentry=(TaskEntry *)$arg0
    printf "Task-ID         : <%d , %d>\n", $Xentry->task_id_, $Xentry->task_instance_
    printf "Running         : %d\n", $Xentry->run_count_
    printf "RunTask         : %p\n", $Xentry->run_task_
    printf "DeferQTaskEntry : %p\n", $Xentry->deferq_task_entry_
    printf "DeferQTaskGroup : %p\n", $Xentry->deferq_task_group_
    printf "Summary         : Wait ( %d ) Run ( %d ) Defer ( %d )\n", $Xentry->stats_.wait_count_, $Xentry->stats_.run_count_, $Xentry->stats_.defer_count_

    printf "Task WaitQ      : \n"
    printf "    "
    set $Xstart = $Xentry->waitq_._M_impl._M_start
    set $Xsize = $Xentry->waitq_._M_impl._M_finish - $Xentry->waitq_._M_impl._M_start
    set $Xi = 0
    set $Xcnt = 0
    while $Xi < $Xsize
        set $Xtask = *(Task **)($Xstart + $Xi)
        printf " %p ", $Xtask
            set $Xcnt++
        if $Xcnt == 4
            printf "\n         "
            set $Xcnt = 0
        end
        set $Xi++
    end
    printf "\n"

    printf "Task PolicyList : \n"
    printf "    "
    set $Xstart = $Xentry->policyq_._M_impl._M_start
    set $Xsize = $Xentry->policyq_._M_impl._M_finish - $Xentry->policyq_._M_impl._M_start
    set $Xi = 0
    set $Xcnt = 0
    while $Xi < $Xsize
        set $XPentry = *(TaskEntry **)($Xstart + $Xi)
        printf " <%p, %2d, %2d, %3d> ", $XPentry, $XPentry->task_id_, $XPentry->task_instance_, $XPentry->run_count_
        set $Xcnt++
        if $Xcnt == 4
            printf "\n    "
            set $Xcnt = 0
        end
        set $Xi++
    end
    printf "\n"
end

define ptask
    set $Xtask=(Task *)$arg0
    printf "TaskId : %d Instance : %d state : %d\n", $Xtask->task_id_, $Xask->task_instance_, $Xtask->state_
    printf "tbb::task %p seqno : %d  recycle : %d  cancel %d\n", $Xtask->task_impl_, $Xtask->seqno_, $Xtask->task_recycle_, $Xtask->task_cancel_
end
