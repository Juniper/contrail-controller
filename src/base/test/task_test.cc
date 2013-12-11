/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>
#include "tbb/task.h"
#include "base/task.h"
#include "base/logging.h"
#include "testing/gunit.h"

void TestWait(int max);

/*
 * Tests to add:
 * 1. Test with test_id > 16, 32
 * 2. Test with test_instance > 16, 32
 */
using namespace std;

class TestTask;

enum TestTaskState {
    NOT_STARTED = 1,
    STARTED = 2,
    FINISHED = 4,
};

#define START_OR_FINISH (STARTED|FINISHED)
#define ANY (NOT_STARTED|STARTED|FINISHED)

bool                test_done;
int                 test_id;
int                 task_count;
int                 run_count;
bool                result;
TestTaskState       task_state[16];
TestTask            *task_ptr[16];
bool                task_result[16];
TaskScheduler       *scheduler;
tbb::mutex          m1;
int                 expected_state[16][16];
vector<TestTask *>  task_start_seq_actual;
vector<TestTask *>  task_start_seq_expected;

class TestUT : public ::testing::Test {
public:
    TestUT() { cout << "Creating TestTask" << endl; };
    void TestBody() {};
};

class TestTask : public Task {
public:
    TestTask() : Task(0, 0) {
        cout << "Creating TestTask" << endl; 
        scheduler->ClearTaskStats(0, 0);
    };
    TestTask(int id, int val);
    TestTask(int id, int inst, int val);
    TestTask(int id, int inst, int val, int sleep_time);
    TestTask(int id, int inst, int val, int sleep_time, int num_runs);
    ~TestTask() { };

    int task_id_;
    int task_instance_;
    int val_;
    int sleep_time_;
    int num_runs_;

    bool Run();
    void Validate();
    void ValidateTaskStartSeq();
    void ValidateTaskRun();

private:
    void TestTaskInternal(int id, int inst, int val, int sleep_time, int num_runs);
};

TestTask::TestTask(int id, int val) : Task(id) {
    TestTaskInternal(id, -1, val, 1, 1);
};

TestTask::TestTask(int id, int inst, int val) : Task(id, inst) {
    TestTaskInternal(id, inst, val, 1, 1);
};

TestTask::TestTask(int id, int inst, int val, int sleep_time) : Task(id, inst) {
    TestTaskInternal(id, inst, val, sleep_time, 1);
};

TestTask::TestTask(int id, int inst, int val, int sleep_time, int num_runs) : 
    Task(id, inst) {
    TestTaskInternal(id, inst, val, sleep_time, num_runs);
}

void TestTask::TestTaskInternal(int id, int inst, int val, 
                           int sleep_time, int num_runs) {
    task_id_ = id; 
    task_instance_ = inst; 
    val_ = val;
    sleep_time_ = sleep_time;
    num_runs_ = num_runs;
    scheduler->ClearTaskGroupStats(id);
    scheduler->ClearTaskStats(id, inst);
    scheduler->ClearTaskStats(id);
}

bool TestTask::Run() {
    EXPECT_EQ(this, Task::Running());
    cout << "Running task <" << task_id_ << ", " << task_instance_ 
        << " : " << val_ << ">" << endl;
    switch (test_id) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
            Validate();
            break;
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
            ValidateTaskStartSeq();
            break;
        case 32:
            break;
        case 33:
            ValidateTaskRun();
            break;

        default:
            assert(0);
            break;
    }

    if (--num_runs_) {
        return false;
    } 

    return true;
};

static void
InitPolicy (TaskExclusion *rule, int count, TaskPolicy *policy)
{
    int i;

    for (i = 0; i < count; i++) {
        policy->push_back(rule[i]);
    }

}

void
TestWait(int max)
{
    int i = 0;

    while (i < (max * 10)) {
        usleep(100000);
        {
            tbb::mutex::scoped_lock lock(m1);
            if (test_done == true) {
                EXPECT_TRUE(scheduler->IsEmpty());
                break;
            }
        }
        EXPECT_FALSE(scheduler->IsEmpty());
        i++;
    }

    if (!(test_done && result)) {
        cout << "Test failed. Test-done " << test_done << ". is " << result << endl;
    }

    EXPECT_TRUE(test_done && result);
    return;
}

void 
TestInit(int id, int count, int expects[16][16])
{
    int i;
    int j;
    tbb::mutex::scoped_lock lock(m1);

    for (i = 0; i < 16; i++) {
        task_state[i] = NOT_STARTED;
        task_ptr[i] = 0;
        task_result[i] = false;
    }

    for (i = 0; i < count; i++) {
        for (j = 0; j < count; j++) {
            expected_state[i][j] = expects[i][j];
        }
    }

    test_id = id;
    task_count = count;
    run_count = 0;
    test_done = false;
    result = false;
}

void
TestInit(int id, int count, TestTask *expects[16])
{
    tbb::mutex::scoped_lock lock(m1);

    task_start_seq_actual.clear();
    task_start_seq_expected.clear();

    for (int i = 0; i < count; i++) {
        task_start_seq_expected.push_back(expects[i]);
    }

    test_id = id;
    task_count = count;
    run_count = 0;
    test_done = false;
    result = false;
}

void TestTask::Validate() {
    int         i;

    task_state[val_] = STARTED;
    sleep(sleep_time_);

    task_result[val_] = true;
    for (i = 0; i < task_count; i++) {
        if ((expected_state[val_][i] & task_state[i]) == 0) {
            tbb::mutex::scoped_lock lock(m1);
            cout << "Expect state fail for task " << val_ << " index "
                << i << ". Expected " << expected_state[val_][i] 
                << " Got " << task_state[i] << endl;
            task_result[val_] = false;
        }
    }

    usleep(10000);
    task_state[val_] = FINISHED;

    {
        tbb::mutex::scoped_lock lock(m1);
        run_count++;
        if (run_count < task_count) {
            return;
        }
    }

    result = true;
    for (i = 0; i < task_count; i++) {
        if (task_result[i] != true) {
            result = false;
            break;
        }
    }

    test_done = true;
    cout << "Final result is " << test_done << ". Result is " << result << endl;
    return;
}

void TestTask::ValidateTaskStartSeq()
{
    int i;
    vector<TestTask *>::iterator it_exp;
    vector<TestTask *>::iterator it_act;

    {
        tbb::mutex::scoped_lock lock(m1);
        task_start_seq_actual.push_back(this);
    }

    sleep(sleep_time_);

    {
        tbb::mutex::scoped_lock lock(m1);
        run_count++;
        if (run_count < task_count) {
            return;
        }
    }

    EXPECT_EQ(task_count, task_start_seq_actual.size());

    result = true;
    for (i = 0, it_exp = task_start_seq_expected.begin(),
         it_act = task_start_seq_actual.begin();
         i < task_count; i++, it_exp++, it_act++) {
        if (*it_exp != *it_act) {
            cout << "Sequence mismatch. Expected <" << 
            (*it_exp)->task_id_ << ", " << (*it_exp)->task_instance_ 
            << "> Got <" << 
            (*it_act)->task_id_ << ", " << (*it_act)->task_instance_ << ">";
            result = false;
            break;
        }
    }

    test_done = true;
    cout << "Final result is " << test_done << ". Result is " << result << endl;
}

void TestTask::ValidateTaskRun()
{
    vector<TestTask *>::iterator it_exp;
    vector<TestTask *>::iterator it_act;

    {
        tbb::mutex::scoped_lock lock(m1);
        task_start_seq_actual.push_back(this);
    }

    sleep(sleep_time_);

    {
        tbb::mutex::scoped_lock lock(m1);
        run_count++;
        if (run_count < task_count) {
            return;
        }
    }

    EXPECT_EQ(task_count, task_start_seq_actual.size());

    result = true;
    test_done = true;
    cout << "Final result is " << test_done << ". Result is " << result << endl;
}

void MatchStats(int task_id, int task_instance, int run_count, int defer_count, 
                int wait_count) {
    TaskStats *stats;

    if (task_instance != -1)
        stats = scheduler->GetTaskStats(task_id, task_instance);
    else
        stats = scheduler->GetTaskStats(task_id);

    if (run_count != -1) {
        EXPECT_EQ(run_count, stats->run_count_);
    }

    if (defer_count != -1) {
        EXPECT_EQ(defer_count, stats->defer_count_);
    }

    if (wait_count != -1) {
        EXPECT_EQ(wait_count, stats->wait_count_);
    }
}

void MatchGroupStats(int task_id, int defer_count) {
    TaskStats *stats;

    stats = scheduler->GetTaskGroupStats(task_id);
    EXPECT_EQ(defer_count, stats->defer_count_);
}

// Task <1, 1> <1, 2> <2, 1> <3, 1> can run in parallel with no policy
TEST_F(TestUT, test1_1) 
{
    int   test_expected_state[16][16] = {
        {STARTED,           ANY,                ANY},
        {START_OR_FINISH,   STARTED,            ANY},
        {START_OR_FINISH,   START_OR_FINISH,    STARTED},
    };

    TestInit(1, 3, test_expected_state);

    task_ptr[0] = new TestTask(1, 1, 0);
    task_ptr[1] = new TestTask(1, 2, 1);
    task_ptr[2] = new TestTask(2, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(1, 1, 1, 0, 0);
    EXPECT_EQ(NULL, Task::Running());

    scheduler->Enqueue(task_ptr[1]);
    MatchStats(1, 2, 1, 0, 0);
    EXPECT_EQ(NULL, Task::Running());

    scheduler->Enqueue(task_ptr[2]);
    MatchStats(2, 1, 1, 0, 0);
    EXPECT_EQ(NULL, Task::Running());

    TestWait(10);
}

// Task <1, 1> <1, 1> <2, 1> are started.
// Only one Task of <1, 1> can run at a time
TEST_F(TestUT, test1_2) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           NOT_STARTED,    ANY},
        {FINISHED,          STARTED,        ANY},
        {START_OR_FINISH,   ANY,            STARTED},
    };

    TestInit(2, 3, test_expected_state);

    task_ptr[0] = new TestTask(1, 1, 0);
    task_ptr[1] = new TestTask(1, 1, 1);
    task_ptr[2] = new TestTask(2, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(1, 1, 1, 0, 0);

    scheduler->Enqueue(task_ptr[1]);
    MatchStats(1, 1, 1, 1, 1);

    scheduler->Enqueue(task_ptr[2]);
    MatchStats(2, 1, 1, 0, 0);

    TestWait(10);
}

// Task <1, 1> <1, 1> <1, 1> <1, 1> are started.
// Only one Task of <1, 1> can run at a time
TEST_F(TestUT, test1_3) 
{
    int    test_expected_state[16][16] = {
        {STARTED,   NOT_STARTED,    NOT_STARTED},
        {FINISHED,  STARTED,        NOT_STARTED},
        {FINISHED,  FINISHED,       STARTED},
    };

    TestInit(3, 3, test_expected_state);
    task_ptr[0] = new TestTask(1, 1, 0);
    task_ptr[1] = new TestTask(1, 1, 1);
    task_ptr[2] = new TestTask(1, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(1, 1, 1, 0, 0);

    scheduler->Enqueue(task_ptr[1]);
    MatchStats(1, 1, 1, 1, 1);

    scheduler->Enqueue(task_ptr[2]);
    MatchStats(1, 1, 1, 1, 2);

    TestWait(10);
}

// Task <4, 1> <4, 2> <4, 3> can run in parallel with no matching policy
TEST_F(TestUT, test2_1) 
{
    int    test_expected_state[16][16] = {
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
    };
    TaskExclusion       rule[] = {
        TaskExclusion(5),
        TaskExclusion(6),
        TaskExclusion(7, 2)
    };
    TaskPolicy          policy;

    InitPolicy(rule, sizeof(rule)/ sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(2, policy);
    scheduler->SetPolicy(3, policy);
    TestInit(4, 3, test_expected_state);

    task_ptr[0] = new TestTask(4, 1, 0);
    task_ptr[1] = new TestTask(4, 2, 1);
    task_ptr[2] = new TestTask(4, 3, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(4, 1, 1, 0, 0);

    scheduler->Enqueue(task_ptr[1]);
    MatchStats(4, 2, 1, 0, 0);

    scheduler->Enqueue(task_ptr[2]);
    MatchStats(4, 3, 1, 0, 0);

    TestWait(10);
}

// Task <5, 1> <6, 2> <7, 1> can run in parallel with policy but no task running
TEST_F(TestUT, test2_2) 
{
    int    test_expected_state[16][16] = {
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
    };

    TestInit(5, 3, test_expected_state);

    task_ptr[0] = new TestTask(5, 1, 0);
    task_ptr[1] = new TestTask(6, 2, 1);
    task_ptr[2] = new TestTask(7, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(5, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(6, 2, 1, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(7, 1, 1, 0, 0);
    TestWait(10);
}

// Task <8, 2> cannot run when <10, 1> is running
// Task <10, 2> can run when <10, 1> is running
TEST_F(TestUT, test3_0) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           NOT_STARTED,    ANY},
        {FINISHED,          STARTED,        FINISHED},
        {START_OR_FINISH,   NOT_STARTED,    STARTED},
    };
    TaskExclusion       rule[] = {
        TaskExclusion(10), TaskExclusion(11),
        TaskExclusion(12, 2)
    };
    TaskPolicy          policy;

    InitPolicy(rule, sizeof(rule)/ sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(8, policy);
    scheduler->SetPolicy(9, policy);

    TestInit(6, 3, test_expected_state);

    task_ptr[0] = new TestTask(10, 1, 0);
    task_ptr[1] = new TestTask(8, 2, 1);
    task_ptr[2] = new TestTask(10, 2, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(10, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchGroupStats(10, 1);
    MatchStats(8, 2, 0, 0, 1);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(10, 2, 1, 0, 0);
    TestWait(10);
}

// Task <8, 2> cannot run when <12, 2> is running
// Task <8, 1> can run when <12, 1> is running
TEST_F(TestUT, test3_1) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           NOT_STARTED,    ANY},
        {FINISHED,          STARTED,        ANY},
        {START_OR_FINISH,   ANY,            STARTED},
    };

    TestInit(7, 3, test_expected_state);

    task_ptr[0] = new TestTask(12, 2, 0);
    task_ptr[1] = new TestTask(8, 2, 1);
    task_ptr[2] = new TestTask(8, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(12, 2, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(12, 2, 1, 1, 0);
    MatchStats(8, 2, 0, 0, 1);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(8, 1, 1, 0, 0);

    TestWait(10);
}

// Task <12, 2> cannot run when <8, 2> is running
// Task <12, 1> can run when <8, 2> is running
TEST_F(TestUT, test3_2) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           NOT_STARTED,    ANY},
        {FINISHED,          STARTED,        ANY},
        {START_OR_FINISH,   ANY,    STARTED},
    };

    TestInit(8, 3, test_expected_state);

    task_ptr[0] = new TestTask(8, 2, 0);
    task_ptr[1] = new TestTask(12, 2, 1);
    task_ptr[2] = new TestTask(12, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(8, 2, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(12, 2, 0, 0, 1);
    MatchStats(8, 2, 1, 1, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(12, 1, 1, 0, 0);
    TestWait(10);
}

// Task <8, 2> cannot run when <12, 2> or <10, 1> is running
TEST_F(TestUT, test3_3) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           ANY,    NOT_STARTED},
        {START_OR_FINISH,   STARTED,            NOT_STARTED},
        {FINISHED,          FINISHED,           STARTED},
    };

    TestInit(9, 3, test_expected_state);

    task_ptr[0] = new TestTask(12, 2, 0);
    task_ptr[1] = new TestTask(10, 1, 1);
    task_ptr[2] = new TestTask(8, 2, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(12, 2, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(10, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(8, 2, 0, 0, 1);
    MatchGroupStats(10, 1);
    TestWait(10);
}

// Task <10, 5> cannot run when <8, 2> or <8, 1> is running
TEST_F(TestUT, test3_4) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           ANY,            NOT_STARTED},
        {START_OR_FINISH,   STARTED,        NOT_STARTED},
        {FINISHED,          FINISHED,       STARTED},
    };

    TestInit(10, 3, test_expected_state);

    task_ptr[0] = new TestTask(8, 2, 0);
    task_ptr[1] = new TestTask(8, 1, 1);
    task_ptr[2] = new TestTask(10, 5, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(8, 2, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(8, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(10, 5, 0, 0, 1);
    MatchGroupStats(8, 1);
    TestWait(10);
}

// Task <8, 2> cannot run when <10, 1> or <11, 1> is running
TEST_F(TestUT, test3_5) 
{
    int    test_expected_state[16][16] = {
        {STARTED,           ANY,            NOT_STARTED},
        {START_OR_FINISH,   STARTED,        NOT_STARTED},
        {FINISHED,          FINISHED,       STARTED},
    };

    TestInit(11, 3, test_expected_state);

    task_ptr[0] = new TestTask(10, 1, 0, 1);
    task_ptr[1] = new TestTask(11, 1, 1, 2);
    task_ptr[2] = new TestTask(8, 2, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(10, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(11, 1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(8, 2, 0, 0, 1);
    MatchGroupStats(10, 1);
    TestWait(10);
}

// Multiple instances of Task <20, -1> can be run simultaneously
TEST_F(TestUT, test4_0) 
{
    int    test_expected_state[16][16] = {
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
        {ANY,   ANY,    ANY},
    };
    TaskExclusion       rule[] = {
        TaskExclusion(21),
        TaskExclusion(22),
        TaskExclusion(23, 3)
    };
    TaskPolicy          policy;

    InitPolicy(rule, sizeof(rule)/ sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(20, policy);

    TestInit(12, 3, test_expected_state);

    task_ptr[0] = new TestTask(20, 0);
    task_ptr[1] = new TestTask(20, 1);
    task_ptr[2] = new TestTask(20, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(20, -1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(20, -1, 2, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(20, -1, 3, 0, 0);
    TestWait(10);
}

// Multiple instances of Task <21, -1> are running. Task <20, 1> is run only
// after both <21, -1> exit
TEST_F(TestUT, test4_1) 
{
    int    test_expected_state[16][16] = {
        {ANY,           ANY,        NOT_STARTED},
        {ANY,           ANY,        NOT_STARTED},
        {FINISHED,      FINISHED,   ANY},
    };

    TestInit(13, 3, test_expected_state);

    task_ptr[0] = new TestTask(21, 0);
    task_ptr[1] = new TestTask(21, 1);
    task_ptr[2] = new TestTask(20, 1, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(21, -1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(21, -1, 2, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchGroupStats(21, 1);
    TestWait(10);
}

// Task <20, -1> cannot run till <23, 3> is running
TEST_F(TestUT, test4_2) 
{
    int    test_expected_state[16][16] = {
        {STARTED,       NOT_STARTED,    NOT_STARTED},
        {FINISHED,      STARTED,        ANY},
        {FINISHED,      ANY,            STARTED}
    };

    TestInit(14, 3, test_expected_state);

    task_ptr[0] = new TestTask(23, 3, 0);
    task_ptr[1] = new TestTask(20, 1);
    task_ptr[2] = new TestTask(20, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(23, 3, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(20, -1, 0, 0, 1);
    MatchStats(23, 3, 1, 1, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(20, -1, 0, 0, 2);
    MatchStats(23, 3, 1, 1, 0);
    TestWait(10);
}

// Multiple instances of Task <20, -1> are running. Task <23, 3> is run only
// after both <20, -1> exit
TEST_F(TestUT, test4_3) 
{
    int    test_expected_state[16][16] = {
        {ANY,           ANY,        NOT_STARTED},
        {ANY,           ANY,        NOT_STARTED},
        {FINISHED,      FINISHED,   ANY},
    };

    TestInit(15, 3, test_expected_state);

    task_ptr[0] = new TestTask(20, 0);
    task_ptr[1] = new TestTask(20, 1);
    task_ptr[2] = new TestTask(23, 3, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(20, -1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(20, -1, 2, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(23, 3, 0, 0, 1);
    MatchStats(20, -1, 2, 1, 0);
    TestWait(10);
}

// Multiple instances of Task <20, -1> are running. Task <21, -1> is run only
// after both <20, -1> exit
TEST_F(TestUT, test4_4) 
{
    int    test_expected_state[16][16] = {
        {ANY,           ANY,        NOT_STARTED},
        {ANY,           ANY,        NOT_STARTED},
        {FINISHED,      FINISHED,   ANY},
    };

    TestInit(16, 3, test_expected_state);

    task_ptr[0] = new TestTask(20, 0);
    task_ptr[1] = new TestTask(20, 1);
    task_ptr[2] = new TestTask(21, 2);

    scheduler->Enqueue(task_ptr[0]);
    MatchStats(20, -1, 1, 0, 0);
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(20, -1, 2, 0, 0);
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(21, -1, 0, 0, 1);
    MatchGroupStats(20, 1);
    TestWait(10);
}

// Test start and stop
// Enqueue multiple instances of <30, -1> when stopped. On start they should
// executed
TEST_F(TestUT, test5_0) 
{
    int    test_expected_state[16][16] = {
        {ANY,           ANY,        ANY},
        {ANY,           ANY,        ANY},
        {ANY,           ANY,        ANY},
    };
    TaskExclusion       rule[] = {
        TaskExclusion(31),
        TaskExclusion(32),
        TaskExclusion(33, 3)
    };

    TaskPolicy          policy;

    InitPolicy(rule, sizeof(rule)/ sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(30, policy);

    TestInit(17, 3, test_expected_state);

    task_ptr[0] = new TestTask(30, 0);
    task_ptr[1] = new TestTask(30, 1);
    task_ptr[2] = new TestTask(30, 2);

    scheduler->Stop();
    scheduler->Enqueue(task_ptr[0]);
    MatchStats(30, -1, 0, 0, 1);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(30, -1, 0, 0, 2);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(30, -1, 0, 0, 3);
    EXPECT_FALSE(scheduler->IsEmpty());
    sleep(1);
    scheduler->Start();
    TestWait(10);
    MatchStats(30, -1, 3, 0, 3);
}

// Enqueue two instances of <30, -1> and <31, -1> when stopped. On start they 
// should executed
TEST_F(TestUT, test5_1) 
{
    int    test_expected_state[16][16] = {
        {ANY,               ANY,                NOT_STARTED},
        {ANY,               ANY,                NOT_STARTED},
        {START_OR_FINISH,   START_OR_FINISH,    ANY},
    };

    TestInit(18, 3, test_expected_state);

    task_ptr[0] = new TestTask(30, 0);
    task_ptr[1] = new TestTask(30, 1);
    task_ptr[2] = new TestTask(31, 2);

    scheduler->Stop();
    scheduler->Enqueue(task_ptr[0]);
    MatchStats(30, -1, 0, 0, 1);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(30, -1, 0, 0, 2);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(31, -1, 0, 0, 1);
    EXPECT_FALSE(scheduler->IsEmpty());
    sleep(1);
    scheduler->Start();
    MatchStats(30, -1, 2, 0, 2);
    MatchGroupStats(30, 1);
    TestWait(10);
    MatchStats(30, -1, 2, 0, 2);
    MatchStats(31, -1, 1, 0, 1);
    MatchGroupStats(30, 1);
}

// Enqueue two instances of <31, -1> and <30, -1> when stopped. On start they 
// should executed
TEST_F(TestUT, test5_2) 
{
    int    test_expected_state[16][16] = {
        {ANY,               ANY,                NOT_STARTED},
        {ANY,               ANY,                NOT_STARTED},
        {START_OR_FINISH,   START_OR_FINISH,    ANY},
    };

    TestInit(19, 3, test_expected_state);

    task_ptr[0] = new TestTask(31, 0);
    task_ptr[1] = new TestTask(31, 1);
    task_ptr[2] = new TestTask(30, 2);

    scheduler->Stop();
    scheduler->Enqueue(task_ptr[0]);
    MatchStats(31, -1, 0, 0, 1);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[1]);
    MatchStats(31, -1, 0, 0, 2);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[2]);
    MatchStats(30, -1, 0, 0, 1);
    EXPECT_FALSE(scheduler->IsEmpty());
    sleep(1);
    scheduler->Start();
    MatchStats(31, -1, 2, 0, 2);
    MatchGroupStats(31, 1);
    TestWait(10);
    MatchStats(31, -1, 2, 0, 2);
    MatchStats(30, -1, 1, 0, 1);
    MatchGroupStats(31, 1);
}

// <51, 1>, <52, 1> cannot run when <50, 1> is running
// <52, 1> cannot run when <51, 1> is running
// Order of enqueue => <50, 1>, <51, 1>, <52, 1>
// Expected order of execution with above policy => <50, 1>, <51, 1>, <52, 1>
//
// <50, 1> starts
// <51, 1> is added in the deferq_ of group <50>  
// <52, 1> is added in the deferq_ of entry <50, 1>
// <50, 1> exits => <51, 1> is started and <52, 1> is added to the deferq_ of <51, 1> 
TEST_F(TestUT, test6_0)
{
    TaskExclusion        rule1[] = {
        TaskExclusion(51),
        TaskExclusion(52, 1)
    };
    TaskExclusion        rule2[] = { TaskExclusion(52, 1) };
    TaskPolicy           policy1, policy2;

    InitPolicy(rule1, sizeof(rule1) / sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(50, policy1);
    
    InitPolicy(rule2, sizeof(rule2) / sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(51, policy1);


    task_ptr[0] = new TestTask(50, 1, 0, 2);
    task_ptr[1] = new TestTask(51, 1, 1);
    task_ptr[2] = new TestTask(52, 1, 2);

    TestTask *task_seq_expected[] = {task_ptr[0], task_ptr[1], task_ptr[2]};
    TestInit(20, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[2]);

    TestWait(10);
}

// <54, 1> and <55, 1> cannot run when <53, 1> is running
// <55, 1> cannot run when <54, 1> is running
// Order of enqueue => <53, 1>, <55, 1>, <54, 1>
// Expected order of execution with above policy => <53, 1>, <55, 1>, <54, 1>
//
// <53, 1> starts
// <55, 1> is added in the deferq_ of entry <53, 1>
// <54, 1> is added in the deferq_ of group <53>   
// <53, 1> exits => <55, 1> is started and <54, 1> is added to the deferq_ of <55, 1> 
TEST_F(TestUT, test6_1)
{
    TaskExclusion        rule1[] = {
        TaskExclusion(54),
        TaskExclusion(55, 1)
    };
    TaskExclusion        rule2[] = { TaskExclusion(55, 1) };
    TaskPolicy           policy1, policy2;

    InitPolicy(rule1, sizeof(rule1) / sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(53, policy1);
    
    InitPolicy(rule2, sizeof(rule2) / sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(54, policy2);
    
    task_ptr[0] = new TestTask(53, 1, 0, 2);
    task_ptr[1] = new TestTask(54, 1, 1);
    task_ptr[2] = new TestTask(55, 1, 2);

    TestTask *task_seq_expected[] = {task_ptr[0], task_ptr[2], task_ptr[1]};
    TestInit(21, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Enqueue(task_ptr[1]);

    TestWait(10);
}

// group->run_count_ non-zero
TEST_F(TestUT, test6_2)
{
    TaskExclusion        rule[] = {
        TaskExclusion(60),
        TaskExclusion(61, 1)
    };
    TaskPolicy           policy;

    InitPolicy(rule, sizeof(rule) / sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(59, policy);
    task_ptr[0] = new TestTask(59, 1, 0, 2);
    task_ptr[1] = new TestTask(59, 2, 1, 4);
    task_ptr[2] = new TestTask(60, 1, 2);
    task_ptr[3] = new TestTask(61, 1, 3);

    TestTask *task_seq_expected[] = {task_ptr[0], task_ptr[1], task_ptr[3], task_ptr[2]};
    TestInit(22, 4, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    sleep(1);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Enqueue(task_ptr[3]);

    TestWait(10);
}

// <63, 1>, <64, 1>, <65, 1> cannot run when <62, 1> is running
// <64, 1>, <65, 1> cannot run when <63, 1> is running
// <65, 1> cannot run when <64, 1> is running
// Order of enqueue => <62, 1>, <63, 1>, <64, 1>, <65, 1>
// Expected order of execution with above policy => <62, 1>, <63, 1>, <64, 1>, <65, 1>
//
// <62, 1> starts
// <63, 1>, <64, 1>, <65, 1> is added to the deferq_ of <62, 1>
// <62, 1> exits. <63, 1> starts and <64, 1>, <65, 1> is added to the deferq_ of <63, 1>
// <63, 1> exits. <64, 1> starts and <65, 1> is added to the deferq_ of <64, 1>
TEST_F(TestUT, test6_3)
{
    TaskExclusion        rule1[] = {
        TaskExclusion(63, 1),
        TaskExclusion(64, 1),
        TaskExclusion(65, 1)
    };
    TaskExclusion        rule2[] = {
        TaskExclusion(64, 1),
        TaskExclusion(65, 1)
    };
    TaskExclusion        rule3[] = { TaskExclusion(65, 1) };
    TaskPolicy           policy1, policy2, policy3;

    InitPolicy(rule1, sizeof(rule1) / sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(62, policy1);
    InitPolicy(rule2, sizeof(rule2) / sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(63, policy2);
    InitPolicy(rule3, sizeof(rule3) / sizeof(TaskExclusion), &policy3);
    scheduler->SetPolicy(64, policy3);

    task_ptr[0] = new TestTask(62, 1, 0);
    task_ptr[1] = new TestTask(63, 1, 1);
    task_ptr[2] = new TestTask(64, 1, 2);
    task_ptr[3] = new TestTask(65, 1, 3);

    TestTask *task_seq_expected[] = {task_ptr[0], task_ptr[1], task_ptr[2], task_ptr[3]};
    TestInit(23, 4, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Enqueue(task_ptr[3]);

    TestWait(10);
}

// <71, 1> cannot run when <70, 1> is running.
// <70, 1> and <71,1 > runs twice. The second run of <70, 1> is 
// scheduled only after <71, 1> finishes its first run and the
// second run of <71, 1> is scheduled only after <70, 1> completes
// its second run.
TEST_F(TestUT, test7_0)
{
    TaskExclusion rule[] = { TaskExclusion(71) };
    TaskPolicy policy;

    InitPolicy(rule, sizeof(rule)/sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(70, policy);

    task_ptr[0] = new TestTask(70, 1, 0, 2, 2);
    task_ptr[1] = new TestTask(71, 1, 1, 2, 2);
    
    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[1], 
                                      task_ptr[0], task_ptr[1] };
    TestInit(24, 4, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);

    TestWait(10);
}

// <72, 1> runs thrice. With no dependent task running, 
// <72, 1> should get rescheduled immediately. 
TEST_F(TestUT, test7_1)
{
    task_ptr[0] = new TestTask(72, 1, 0, 1, 3);
    
    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[0], task_ptr[0] };
    TestInit(25, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);

    TestWait(10);
}

// Cancel the task in INIT state
// Cancel the task in RUN state - task_recycle_ -> true
TEST_F(TestUT, test8_0)
{
    TaskExclusion rule[] = { TaskExclusion(81) };
    TaskPolicy policy;

    InitPolicy(rule, sizeof(rule)/sizeof(TaskExclusion), &policy);
    scheduler->SetPolicy(80, policy);
    
    task_ptr[0] = new TestTask(80, 1, 0, 1, 2);
    task_ptr[1] = new TestTask(81, 1, 1, 1, 2);
    task_ptr[2] = new TestTask(81, 1, 2, 1, 2);

    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[1], task_ptr[1] };
    TestInit(26, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    EXPECT_EQ(Task::RUN, task_ptr[0]->GetState());
    EXPECT_EQ(TaskScheduler::QUEUED, scheduler->Cancel(task_ptr[0]));
    scheduler->Enqueue(task_ptr[1]);
    EXPECT_EQ(Task::INIT, task_ptr[2]->GetState());
    EXPECT_EQ(TaskScheduler::FAILED, scheduler->Cancel(task_ptr[2]));
    delete task_ptr[2];

    TestWait(10);
}

// Cancel task in RUN state - task_recycle_ -> false
TEST_F(TestUT, test8_1) 
{
    task_ptr[0] = new TestTask(80, -1, 0, 1, 1);
    task_ptr[1] = new TestTask(81, -1, 1, 1, 2);
    
    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[1], task_ptr[1] };
    TestInit(27, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    EXPECT_EQ(Task::RUN, task_ptr[0]->GetState());
    EXPECT_EQ(TaskScheduler::QUEUED, scheduler->Cancel(task_ptr[0]));
    scheduler->Enqueue(task_ptr[1]);
    
    TestWait(10);
}

// Cancel task in WAIT state - waitq_ != 0 and waitq_ == 0
TEST_F(TestUT, test8_2)
{
    TaskExclusion rule1[] = { TaskExclusion(82) };
    TaskExclusion rule2[] = { TaskExclusion(82, 1) };
    TaskPolicy policy1, policy2;

    InitPolicy(rule1, sizeof(rule1)/sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(83, policy1);
    InitPolicy(rule2, sizeof(rule2)/sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(84, policy2);

    task_ptr[0] = new TestTask(82, 1, 0, 1, 2);
    task_ptr[1] = new TestTask(83, -1, 1, 1, 2);
    task_ptr[2] = new TestTask(83, -1, 2, 1, 1);
    task_ptr[3] = new TestTask(83, 2, 3, 1, 1);
    task_ptr[4] = new TestTask(84, 1, 4, 1, 1);
    task_ptr[5] = new TestTask(84, 1, 5, 1, 1);
    task_ptr[6] = new TestTask(82, 1, 6, 1, 1);
    task_ptr[7] = new TestTask(82, 1, 7, 1, 1);

    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[3], 
                                      task_ptr[6], task_ptr[0] };
    TestInit(28, 4, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Enqueue(task_ptr[3]);
    scheduler->Enqueue(task_ptr[4]);
    scheduler->Enqueue(task_ptr[5]);
    scheduler->Enqueue(task_ptr[6]);
    scheduler->Enqueue(task_ptr[7]);
    EXPECT_EQ(Task::WAIT, task_ptr[2]->GetState());
    EXPECT_EQ(TaskScheduler::CANCELLED, scheduler->Cancel(task_ptr[2]));
    EXPECT_EQ(Task::WAIT, task_ptr[5]->GetState());
    EXPECT_EQ(TaskScheduler::CANCELLED, scheduler->Cancel(task_ptr[5]));
    EXPECT_EQ(Task::WAIT, task_ptr[1]->GetState());
    EXPECT_EQ(TaskScheduler::CANCELLED, scheduler->Cancel(task_ptr[1]));
    EXPECT_EQ(Task::WAIT, task_ptr[4]->GetState());
    EXPECT_EQ(TaskScheduler::CANCELLED, scheduler->Cancel(task_ptr[4]));
    EXPECT_EQ(Task::WAIT, task_ptr[7]->GetState());
    EXPECT_EQ(TaskScheduler::CANCELLED, scheduler->Cancel(task_ptr[7]));

    TestWait(10);
}

// Cancel task when scheduler is stopped
TEST_F(TestUT, test8_3)
{
    task_ptr[0] = new TestTask(85, 1, 0, 1);
    task_ptr[1] = new TestTask(85, 2, 1, 1);
    task_ptr[2] = new TestTask(85, 1, 2, 1);
    task_ptr[3] = new TestTask(85, 1, 3, 1);

    TestTask *task_seq_expected[] = { task_ptr[3] };
    TestInit(29, 1, task_seq_expected);

    scheduler->Stop();
    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[2]);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Cancel(task_ptr[0]);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Cancel(task_ptr[2]);
    EXPECT_FALSE(scheduler->IsEmpty());
    scheduler->Cancel(task_ptr[1]);
    EXPECT_TRUE(scheduler->IsEmpty());
    scheduler->Enqueue(task_ptr[3]);
    scheduler->Start();
    
    TestWait(10);
}

// Cancel task which is a first entry in the waitq_ [Update deferq_task_group_]
TEST_F(TestUT, test8_4)
{
    TaskExclusion rule1[] = { TaskExclusion(86), TaskExclusion(87) };
    TaskExclusion rule2[] = { TaskExclusion(87), TaskExclusion(88) };
    TaskPolicy policy1, policy2;
    
    InitPolicy(rule1, sizeof(rule1)/sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(88, policy1);
    InitPolicy(rule2, sizeof(rule2)/sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(86, policy2);

    task_ptr[0] = new TestTask(86, 1, 0, 1);
    task_ptr[1] = new TestTask(87, 1, 1, 1);
    task_ptr[2] = new TestTask(87, 1, 2, 1);
    task_ptr[3] = new TestTask(88, 1, 3, 1);

    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[3], task_ptr[2] };
    TestInit(30, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[3]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Cancel(task_ptr[1]);

    TestWait(10);
}

// Cancel task which is a first entry in the waitq_ [Update deferq_task_entry_]
TEST_F(TestUT, test8_5)
{
    TaskExclusion rule1[] = { TaskExclusion(89, 2), TaskExclusion(90, 2) };
    TaskExclusion rule2[] = { TaskExclusion(90, 2), TaskExclusion(91, 2) };
    TaskPolicy policy1, policy2;

    InitPolicy(rule1, sizeof(rule1)/sizeof(TaskExclusion), &policy1);
    scheduler->SetPolicy(91, policy1);
    InitPolicy(rule2, sizeof(rule2)/sizeof(TaskExclusion), &policy2);
    scheduler->SetPolicy(89, policy2);

    task_ptr[0] = new TestTask(89, 2, 0, 1);
    task_ptr[1] = new TestTask(90, 2, 1, 1);
    task_ptr[2] = new TestTask(90, 2, 2, 1);
    task_ptr[3] = new TestTask(91, 2, 3, 1);

    TestTask *task_seq_expected[] = { task_ptr[0], task_ptr[3], task_ptr[2] };
    TestInit(31, 3, task_seq_expected);
    
    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Enqueue(task_ptr[3]);
    scheduler->Enqueue(task_ptr[2]);
    scheduler->Cancel(task_ptr[1]);

    TestWait(10);
}

/* Run a task recycled for n number of times and verify that scheduler IsEmpty 
 * never returns true till the task has run fully */
TEST_F(TestUT, test9_0)
{
#define TEST9_0_MAX_RUNS 2000
    task_ptr[0] = new TestTask(90, 1, 0, 2, TEST9_0_MAX_RUNS);
    TaskStats *stats;

    TestTask *task_seq_expected[] = { };
    TestInit(32, 0, task_seq_expected);
    scheduler->Enqueue(task_ptr[0]);

    stats = scheduler->GetTaskStats(90, 1);
    while ((stats->run_count_ < TEST9_0_MAX_RUNS) && !scheduler->IsEmpty()) {
        stats = scheduler->GetTaskStats(90, 1);
    } 

    EXPECT_TRUE(scheduler->IsEmpty()); 
    EXPECT_EQ(stats->run_count_, TEST9_0_MAX_RUNS);
    cout << "Finished test with total run of " << stats->run_count_ << endl;
}

/* Enqueue tasks which will be recycled. Task 0 and task 1 belong to same
 * taskgroup. Verify that run_count of group does not cause scheduler blockage,
 * if a task exits with its recycle set as true */
TEST_F(TestUT, test9_1)
{
    task_ptr[0] = new TestTask(92, 1, 0, 2, 2);
    task_ptr[1] = new TestTask(92, 1, 1, 2, 2);

    TestTask *task_seq_expected[] = { };
    TestInit(33, 3, task_seq_expected);

    scheduler->Enqueue(task_ptr[0]);
    scheduler->Enqueue(task_ptr[1]);
    scheduler->Cancel(task_ptr[0]);

    TestWait(10);
    EXPECT_TRUE(scheduler->IsEmpty());
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    scheduler = TaskScheduler::GetInstance();
    LoggingInit();
    return RUN_ALL_TESTS();
}
