//
// queue_task_test.cc
//
// Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
//

#include "testing/gunit.h"
#include <boost/bind.hpp>
#include "base/logging.h"
#include "base/queue_task.h"
#include "base/test/task_test_util.h"

class EnqueueTask : public Task {
public:
    EnqueueTask(WorkQueue<int> *queue, int task_id, size_t num_enqueues)
    : Task(task_id, -1),
      task_id_(task_id),
      queue_(queue),
      num_enqueues_(num_enqueues) {
    }
    bool Run() {
        size_t count = 0;
        while (count < num_enqueues_) {
            if (!queue_->Enqueue(enqueue_counter_++)) {
            }
            count++;
        }
        return true;
    }

private:
    int task_id_;
    WorkQueue<int> *queue_;
    size_t num_enqueues_;
    static int enqueue_counter_;
};

int EnqueueTask::enqueue_counter_ = 0;

class QueueTaskTest : public ::testing::Test {
public:
    QueueTaskTest() :
        wq_task_id_(TaskScheduler::GetInstance()->GetTaskId(
                        "::test::QueueTaskTest")),
        work_queue_(wq_task_id_, -1,
                    boost::bind(&QueueTaskTest::Dequeue, this, _1)),
        dequeues_(0),
        wm_cb_count_(0) {
        exit_callback_running_ = false;
        exit_callback_counter_ = 0;
        wm_callback_running_ = false;
    }

    virtual void SetUp() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }

    virtual void TearDown() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }

    bool Dequeue(int entry) {
        dequeues_++;
        return true;
    }
    bool StartRunnerAlways() {
        return true;
    }
    bool StartRunnerNever() {
        return false;
    }
    bool IsWorkQueueRunning() {
        return work_queue_.running_;
    }
    bool IsWorkQueueCurrentRunner() {
        return work_queue_.current_runner_ != NULL;
    }
    void SetWorkQueueMaxIterations(size_t niterations) {
        work_queue_.max_iterations_ = niterations;
    }
    void WaterMarkCallback(size_t wm_count) {
        wm_cb_count_ = wm_count;
    }
    void WaterMarkCallbackSleep1Sec(size_t wm_count, bool high,
        size_t vwm_size) {
        EXPECT_FALSE(wm_callback_running_);
        wm_callback_running_ = true;
        int count = 0;
        while (count++ < 10) {
            usleep(100000);
            if (high) {
                EXPECT_EQ(vwm_size, work_queue_.high_water_.size());
            } else {
                EXPECT_EQ(vwm_size, work_queue_.low_water_.size());
            }
        }
        wm_callback_running_ = false;
    }
    bool DequeueTaskReady(bool start_runner) {
        if (start_runner) {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&QueueTaskTest::StartRunnerAlways, this));
        } else {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&QueueTaskTest::StartRunnerNever, this));
       }
       return true;
    }
    void ExitCallbackSleep1Sec(bool done) {
        exit_callback_counter_++;
        EXPECT_FALSE(exit_callback_running_);
        exit_callback_running_ = true;
        int count = 0;
        while (count++ < 10) {
            usleep(100000);
            EXPECT_TRUE(exit_callback_running_);
        }
        exit_callback_running_ = false;
    }

    int wq_task_id_;
    WorkQueue<int> work_queue_;
    size_t dequeues_;
    size_t wm_cb_count_;
    tbb::atomic<int> exit_callback_counter_;
    tbb::atomic<bool> exit_callback_running_;
    tbb::atomic<bool> wm_callback_running_;
};

TEST_F(QueueTaskTest, StartRunnerBasic) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Always do start runner
    work_queue_.SetStartRunnerFunc(
            boost::bind(&QueueTaskTest::StartRunnerAlways, this));
    int enqueue_counter = 0;
    work_queue_.Enqueue(enqueue_counter++);
    task_util::WaitForIdle(1);
    // Verify dequeue happened
    EXPECT_EQ(1, dequeues_);
    // Verify task statistics
    TaskStats *tstats = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(1, tstats->run_count_);
    EXPECT_EQ(0, tstats->defer_count_);
    EXPECT_EQ(0, tstats->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(1, work_queue_.NumEnqueues());
    EXPECT_EQ(1, work_queue_.NumDequeues());
    EXPECT_EQ(0, work_queue_.Length());

    // Never do start runner
    work_queue_.SetStartRunnerFunc(
            boost::bind(&QueueTaskTest::StartRunnerNever, this));
    work_queue_.Enqueue(enqueue_counter++);
    task_util::WaitForIdle(1);
    // Verify dequeue did not happen
    EXPECT_EQ(1, dequeues_);
    // Verify task statistics
    tstats = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(1, tstats->run_count_);
    EXPECT_EQ(0, tstats->defer_count_);
    EXPECT_EQ(0, tstats->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(2, work_queue_.NumEnqueues());
    EXPECT_EQ(1, work_queue_.NumDequeues());
    EXPECT_EQ(1, work_queue_.Length());
}

TEST_F(QueueTaskTest, StartRunnerInternals) {
    // Stop scheduler, enqueue always do start runner and verify
    // WorkQueue internal state
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    int enqueue_counter = 0;
    work_queue_.SetStartRunnerFunc(
            boost::bind(&QueueTaskTest::StartRunnerAlways, this));
    work_queue_.Enqueue(enqueue_counter++);
    // Verify WorkQueue internal state
    EXPECT_TRUE(IsWorkQueueRunning());
    EXPECT_TRUE(IsWorkQueueCurrentRunner());
    EXPECT_EQ(0, work_queue_.NumDequeues());
    EXPECT_EQ(1, work_queue_.NumEnqueues());
    EXPECT_EQ(1, work_queue_.Length());
    scheduler->Start();
    task_util::WaitForIdle(1);
    // Verify WorkQueue internal state
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_FALSE(IsWorkQueueCurrentRunner());
    EXPECT_EQ(1, work_queue_.NumDequeues());
    EXPECT_EQ(1, work_queue_.NumEnqueues());
    EXPECT_EQ(0, work_queue_.Length());
}

TEST_F(QueueTaskTest, MaxIterationsTest) {
    int max_iterations = 5;
    SetWorkQueueMaxIterations(max_iterations);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    EnqueueTask *etask1 = new EnqueueTask(&work_queue_,
            scheduler->GetTaskId(
                "::test::QueueTaskTest::MaxIterationsTest1"),
            max_iterations);
    scheduler->Enqueue(etask1);
    task_util::WaitForIdle(1);
    // Verify task statistics
    TaskStats *tstats1 = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(1, tstats1->run_count_);
    EXPECT_EQ(0, tstats1->defer_count_);
    EXPECT_EQ(0, tstats1->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(max_iterations, work_queue_.NumEnqueues());
    EXPECT_EQ(max_iterations, work_queue_.NumDequeues());
    EXPECT_EQ(0, work_queue_.Length());
    scheduler->ClearTaskStats(wq_task_id_);
    EnqueueTask *etask2 = new EnqueueTask(&work_queue_,
            scheduler->GetTaskId(
                "::test::QueueTaskTest::MaxIterationsTest2"),
            max_iterations * 3);
    scheduler->Enqueue(etask2);
    task_util::WaitForIdle(1);
    // Verify task statistics
    TaskStats *tstats2 = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(3, tstats2->run_count_);
    EXPECT_EQ(0, tstats2->defer_count_);
    EXPECT_EQ(0, tstats2->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(max_iterations * 4, work_queue_.NumEnqueues());
    EXPECT_EQ(max_iterations * 4, work_queue_.NumDequeues());
    EXPECT_EQ(0, work_queue_.Length());
}

TEST_F(QueueTaskTest, WaterMarkTest) {
    // Setup watermarks
    WorkQueue<int>::WaterMarkInfo hwm1(5,
        boost::bind(&QueueTaskTest::WaterMarkCallback, this, _1));
    work_queue_.SetHighWaterMark(hwm1);
    WorkQueue<int>::WaterMarkInfo hwm2(8,
        boost::bind(&QueueTaskTest::WaterMarkCallback, this, _1));
    work_queue_.SetHighWaterMark(hwm2);
    WorkQueue<int>::WaterMarkInfo lwm1(4,
        boost::bind(&QueueTaskTest::WaterMarkCallback, this, _1));
    WorkQueue<int>::WaterMarkInfo lwm2(2,
        boost::bind(&QueueTaskTest::WaterMarkCallback, this, _1));
    std::vector<WorkQueue<int>::WaterMarkInfo> lwm;
    lwm.push_back(lwm1);
    lwm.push_back(lwm2);
    work_queue_.SetLowWaterMark(lwm);
    // Stop work queue dequeue
    work_queue_.SetStartRunnerFunc(
        boost::bind(&QueueTaskTest::StartRunnerNever, this));
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Enqueue 5 entries
    EnqueueTask *etask;
    etask = new EnqueueTask(&work_queue_,
                scheduler->GetTaskId(
                "::test::QueueTaskTest::WaterMarkTest"), 5);
    scheduler->Enqueue(etask);
    task_util::WaitForIdle(1);
    EXPECT_EQ(5, work_queue_.NumEnqueues());
    EXPECT_EQ(5, work_queue_.Length());
    EXPECT_EQ(0, wm_cb_count_);
    // Enqueue 2 entries
    etask = new EnqueueTask(&work_queue_,
                scheduler->GetTaskId(
                "::test::QueueTaskTest::WaterMarkTest"), 2);
    scheduler->Enqueue(etask);
    task_util::WaitForIdle(1);
    EXPECT_EQ(7, work_queue_.NumEnqueues());
    EXPECT_EQ(7, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_count_);
    // Enqueue 3 entries
    etask = new EnqueueTask(&work_queue_,
                scheduler->GetTaskId(
                "::test::QueueTaskTest::WaterMarkTest"), 3);
    scheduler->Enqueue(etask);
    task_util::WaitForIdle(1);
    EXPECT_EQ(10, work_queue_.NumEnqueues());
    EXPECT_EQ(10, work_queue_.Length());
    EXPECT_EQ(8, wm_cb_count_);
    // Dequeue 6 entries
    work_queue_.SetStartRunnerFunc(
        boost::bind(&QueueTaskTest::StartRunnerAlways, this));
    SetWorkQueueMaxIterations(6);
    work_queue_.SetExitCallback(
        boost::bind(&QueueTaskTest::DequeueTaskReady, this, false));
    work_queue_.MayBeStartRunner();
    task_util::WaitForIdle(1);
    EXPECT_EQ(4, work_queue_.Length());
    EXPECT_EQ(8, wm_cb_count_);
    // Dequeue 1 entry
    work_queue_.SetStartRunnerFunc(
        boost::bind(&QueueTaskTest::StartRunnerAlways, this));
    SetWorkQueueMaxIterations(1);
    work_queue_.SetExitCallback(
        boost::bind(&QueueTaskTest::DequeueTaskReady, this, false));
    work_queue_.MayBeStartRunner();
    task_util::WaitForIdle(1);
    EXPECT_EQ(3, work_queue_.Length());
    EXPECT_EQ(4, wm_cb_count_);
    // Empty the queue
    work_queue_.SetStartRunnerFunc(
        boost::bind(&QueueTaskTest::StartRunnerAlways, this));
    SetWorkQueueMaxIterations(10);
    work_queue_.MayBeStartRunner();
    task_util::WaitForIdle(1);
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_count_);
}

TEST_F(QueueTaskTest, WaterMarkParallelTest) {
    // Setup high watermarks
    std::vector<WorkQueue<int>::WaterMarkInfo> vhwm;
    WorkQueue<int>::WaterMarkInfo hwm(0,
        boost::bind(&QueueTaskTest::WaterMarkCallbackSleep1Sec,
                    this, _1, true, 1));
    vhwm.push_back(hwm);
    work_queue_.SetHighWaterMark(vhwm);
    // Enqueue so that watermark callback gets called
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    EnqueueTask *etask;
    etask = new EnqueueTask(&work_queue_,
                scheduler->GetTaskId(
                "::test::QueueTaskTest::WaterMarkParallelTest"), 1);
    scheduler->Enqueue(etask);
    // Wait till the watermark callback is running
    TASK_UTIL_EXPECT_TRUE(wm_callback_running_);
    // Clear the high watermarks
    work_queue_.ResetHighWaterMark();
    // Wait till the watermark callback is finished 
    TASK_UTIL_EXPECT_FALSE(wm_callback_running_);
}

TEST_F(QueueTaskTest, OnExitParallelTest) {
    // Set exit callback 
    work_queue_.SetExitCallback(
        boost::bind(&QueueTaskTest::ExitCallbackSleep1Sec, this, _1));
    // Set max iterations to 1 and then enqueue more than 1 entries to simulate
    // multiple dequeue task potentially trying to run in parallel.
    SetWorkQueueMaxIterations(1);
    int enqueue_counter = 0;
    work_queue_.Enqueue(enqueue_counter++);
    // Ensure that exit callback is running
    TASK_UTIL_EXPECT_EQ(exit_callback_counter_, 1);
    TASK_UTIL_EXPECT_TRUE(exit_callback_running_);
    // Now enqueue more entry
    work_queue_.Enqueue(enqueue_counter++);
    // Wait till the exit callback is finished
    TASK_UTIL_EXPECT_EQ(exit_callback_counter_, 2);
    TASK_UTIL_EXPECT_FALSE(exit_callback_running_);
}

TEST_F(QueueTaskTest, DisableEnableTest1) {
    // Disable the queue
    work_queue_.set_disable(true);

    // Create a task to enqueue a few entries
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    EnqueueTask *etask = new EnqueueTask(&work_queue_,
        scheduler->GetTaskId("::test::QueueTaskTest::EnableDisableTest"), 100);
    scheduler->Enqueue(etask);
    task_util::WaitForIdle(1);

    // Verify that the entries are still enqueued
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_EQ(100, work_queue_.Length());
    EXPECT_EQ(100, work_queue_.NumEnqueues());
    EXPECT_EQ(0, work_queue_.NumDequeues());
    EXPECT_EQ(0, dequeues_);

    // Enable the queue
    work_queue_.set_disable(false);
    task_util::WaitForIdle(1);

    // Verify that the entries have been dequeued
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(100, work_queue_.NumEnqueues());
    EXPECT_EQ(100, work_queue_.NumDequeues());
    EXPECT_EQ(100, dequeues_);
}

TEST_F(QueueTaskTest, DisableEnableTest2) {
    // Stop the scheduler
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();

    // Enqueue a few entries
    for (int idx = 0; idx < 100; ++idx) {
        work_queue_.Enqueue(idx);
    }

    // Disable the queue
    work_queue_.set_disable(true);

    // Start the scheduler
    scheduler->Start();
    task_util::WaitForIdle(1);

    // Verify that the entries are still enqueued
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_EQ(100, work_queue_.Length());
    EXPECT_EQ(100, work_queue_.NumEnqueues());
    EXPECT_EQ(0, work_queue_.NumDequeues());
    EXPECT_EQ(0, dequeues_);

    // Enable the queue
    work_queue_.set_disable(false);
    task_util::WaitForIdle(1);

    // Verify that the entries have been dequeued
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(100, work_queue_.NumEnqueues());
    EXPECT_EQ(100, work_queue_.NumDequeues());
    EXPECT_EQ(100, dequeues_);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
