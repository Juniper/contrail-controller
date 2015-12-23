//
// queue_task_test.cc
//
// Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
//

#include <queue>

#include "testing/gunit.h"
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include "base/logging.h"
#include "base/queue_task.h"
#include "base/test/task_test_util.h"

class EnqueueTask : public Task {
public:
    EnqueueTask(WorkQueue<int> *queue, int task_id, int num_enqueues,
        int num_enqueues_in_iteration = -1)
    : Task(task_id, -1),
      task_id_(task_id),
      queue_(queue),
      num_enqueues_(num_enqueues),
      num_enqueues_in_iteration_(num_enqueues_in_iteration),
      count_(0) {
    }
    bool Run() {
        while (count_ < num_enqueues_) {
            if (!queue_->Enqueue(enqueue_counter_++)) {
            }
            count_++;
            if (num_enqueues_in_iteration_ != -1) {
                if ((count_ % num_enqueues_in_iteration_) == 0) {
                    usleep(10);
                }
            }
        }
        return true;
    }
    std::string Description() const { return "EnqueueTask"; }

private:
    int task_id_;
    WorkQueue<int> *queue_;
    int num_enqueues_;
    int num_enqueues_in_iteration_;
    int count_;
    static int enqueue_counter_;
};

int EnqueueTask::enqueue_counter_ = 0;

static bool StartRunnerAlways() {
    return true;
}

static bool StartRunnerNever() {
    return false;
}

struct WaterMarkTestCbType {
    enum type {
        INVALID,
        HWM1,
        HWM2,
        HWM3,
        LWM1,
        LWM2,
        LWM3,
    };
};

static void WaterMarkTestCb(size_t qsize, size_t *wm_cb_qsize,
    size_t *wm_cb_count, WaterMarkTestCbType::type cb_type,
    WaterMarkTestCbType::type *wm_cb_type) {
    *wm_cb_qsize = qsize;
    *wm_cb_count += 1;
    *wm_cb_type = cb_type;
}

class QueueTaskTest : public ::testing::Test {
public:
    QueueTaskTest() :
        wq_task_id_(TaskScheduler::GetInstance()->GetTaskId(
                        "::test::QueueTaskTest")),
        work_queue_(wq_task_id_, -1,
                    boost::bind(&QueueTaskTest::Dequeue, this, _1)),
        dequeues_(0),
        wm_cb_qsize_(0),
        wm_cb_count_(0),
        wm_cb_type_(WaterMarkTestCbType::INVALID) {
        exit_callback_running_ = false;
        exit_callback_counter_ = 0;
        wm_callback_running_ = false;
        shutdown_test_exit_callback_sleep_ = true;
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
    void WorkQueueWaterMarkIndexes(int *hwater_index, int *lwater_index) {
        work_queue_.GetWaterMarkIndexes(hwater_index, lwater_index);
    }
    bool IsWorkQueueRunning() {
        return work_queue_.running_;
    }
    bool IsWorkQueueCurrentRunner() {
        return work_queue_.current_runner_ != NULL;
    }
    bool IsWorkQueueShutdownScheduled() {
        return work_queue_.shutdown_scheduled_;
    }
    void SetWorkQueueMaxIterations(size_t niterations) {
        work_queue_.max_iterations_ = niterations;
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
    bool VerifyWaterMarkIndexes() {
        int hwater_index, lwater_index;
        work_queue_.GetWaterMarkIndexes(&hwater_index, &lwater_index);
        return (hwater_index == -1 && lwater_index == -1) ||
               (hwater_index + 1 == lwater_index);
    }
    void WaterMarkCallbackVerifyIndexes(size_t wm_count,
        WaterMarkTestCbType::type wm_cb) {
        bool success(VerifyWaterMarkIndexes());
        EXPECT_TRUE(success);
        wm_cb_type_ = wm_cb;
        wm_cb_qsize_ = wm_count;
    }
    bool DequeueTaskReady(bool start_runner) {
        if (start_runner) {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&StartRunnerAlways));
        } else {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&StartRunnerNever));
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
    void ShutdownTestExitCallback(bool done) {
        exit_callback_running_ = true;
        EXPECT_FALSE(done);
        while (shutdown_test_exit_callback_sleep_) {
            usleep(100000);
        }
        exit_callback_running_ = false;
    }
    void DequeueEntries(int count) {
        work_queue_.SetStartRunnerFunc(
            boost::bind(&StartRunnerAlways));
        SetWorkQueueMaxIterations(count);
        work_queue_.SetExitCallback(
            boost::bind(&QueueTaskTest::DequeueTaskReady, this, false));
        work_queue_.MayBeStartRunner();
        task_util::WaitForIdle(1);
    }
    void EnqueueEntries(int count) {
        work_queue_.SetStartRunnerFunc(
            boost::bind(&StartRunnerNever));
        for (int i = 0; i < count; i++) {
            work_queue_.Enqueue(i);
        }
    }

    int wq_task_id_;
    WorkQueue<int> work_queue_;
    size_t dequeues_;
    size_t wm_cb_qsize_;
    size_t wm_cb_count_;
    WaterMarkTestCbType::type wm_cb_type_;
    tbb::atomic<int> exit_callback_counter_;
    tbb::atomic<bool> exit_callback_running_;
    tbb::atomic<bool> wm_callback_running_;
    tbb::atomic<bool> shutdown_test_exit_callback_sleep_;
};

TEST_F(QueueTaskTest, StartRunnerBasic) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Always do start runner
    work_queue_.SetStartRunnerFunc(
            boost::bind(&StartRunnerAlways));
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
            boost::bind(&StartRunnerNever));
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
            boost::bind(&StartRunnerAlways));
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
    scheduler->Stop();
    int enqueue_counter = 0;
    for (int i = 0; i < max_iterations; i++) {
        work_queue_.Enqueue(enqueue_counter++);
    }
    scheduler->Start();
    task_util::WaitForIdle(1);
    // Verify task statistics
    TaskStats *tstats1 = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(1, tstats1->run_count_);
    EXPECT_EQ(0, tstats1->defer_count_);
    EXPECT_EQ(1, tstats1->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(max_iterations, work_queue_.NumEnqueues());
    EXPECT_EQ(max_iterations, work_queue_.NumDequeues());
    EXPECT_EQ(0, work_queue_.Length());
    scheduler->ClearTaskStats(wq_task_id_);
    scheduler->Stop();
    for (int i = 0; i < max_iterations * 3; i++) {
        work_queue_.Enqueue(enqueue_counter++);
    }
    scheduler->Start();
    task_util::WaitForIdle(1);
    // Verify task statistics
    TaskStats *tstats2 = scheduler->GetTaskStats(wq_task_id_);
    EXPECT_EQ(3, tstats2->run_count_);
    EXPECT_EQ(0, tstats2->defer_count_);
    EXPECT_EQ(1, tstats2->wait_count_);
    // Verify WorkQueue
    EXPECT_EQ(max_iterations * 4, work_queue_.NumEnqueues());
    EXPECT_EQ(max_iterations * 4, work_queue_.NumDequeues());
    EXPECT_EQ(0, work_queue_.Length());
}

TEST_F(QueueTaskTest, WaterMarkTest) {
    // Setup watermarks
    WaterMarkInfo hwm1(5,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM1, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm1);
    WaterMarkInfo hwm2(11,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM2, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm2);
    WaterMarkInfo hwm3(17,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM3, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm3);
    WaterMarkInfo lwm1(14,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM1, &wm_cb_type_));
    WaterMarkInfo lwm2(8,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM2, &wm_cb_type_));
    WaterMarkInfo lwm3(2,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM3, &wm_cb_type_));
    WaterMarkInfos lwm;
    lwm.push_back(lwm1);
    lwm.push_back(lwm2);
    lwm.push_back(lwm3);
    work_queue_.SetLowWaterMark(lwm);
    int hwater_index, lwater_index;
    EXPECT_EQ(WaterMarkTestCbType::INVALID, wm_cb_type_);
    EXPECT_EQ(0, wm_cb_qsize_);
    EXPECT_EQ(0, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(-1, lwater_index);
    // Enqueue 4 entries
    // Check that no new high water mark cb is called
    EnqueueEntries(4);
    EXPECT_EQ(4, work_queue_.Length());
    EXPECT_EQ(0, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::INVALID, wm_cb_type_);
    EXPECT_EQ(0, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(-1, lwater_index);
    // Dequeue 1 entry
    // Check that lwm2 cb is called
    DequeueEntries(1);
    EXPECT_EQ(3, work_queue_.Length());
    EXPECT_EQ(3, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(1, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Dequeue 1 entry
    // Check that lwm3 cb is called
    DequeueEntries(1);
    EXPECT_EQ(2, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(2, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
    // Dequeue 2 entries
    // Check that no new low water mark cb is called
    DequeueEntries(2);
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(2, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
    // Enqueue 5 entries
    // Check that hwm1 cb is called
    EnqueueEntries(5);
    EXPECT_EQ(5, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(3, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Enqueue 2 entries
    // Check that no new high water mark cb is called
    EnqueueEntries(2);
    EXPECT_EQ(7, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(3, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Enqueue 11 entries
    // Check that hwm2, hwm3 cb is called
    EnqueueEntries(11);
    EXPECT_EQ(18, work_queue_.Length());
    EXPECT_EQ(17, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(5, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(2, hwater_index);
    EXPECT_EQ(3, lwater_index);
    // Dequeue 6 entries
    // Check that lwm1 cb is called
    DequeueEntries(6);
    EXPECT_EQ(12, work_queue_.Length());
    EXPECT_EQ(14, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM1, wm_cb_type_);
    EXPECT_EQ(6, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(1, hwater_index);
    EXPECT_EQ(2, lwater_index);
    // Refill the queue
    // Enqueue 5 entries
    // Check that hwm3 cb is called
    EnqueueEntries(5);
    EXPECT_EQ(17, work_queue_.Length());
    EXPECT_EQ(17, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(7, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(2, hwater_index);
    EXPECT_EQ(3, lwater_index);
    // Dequeue 1 entry
    // Check that no new low water cb is called
    DequeueEntries(1);
    EXPECT_EQ(16, work_queue_.Length());
    EXPECT_EQ(17, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(7, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(2, hwater_index);
    EXPECT_EQ(3, lwater_index);
    // Dequeue 8 entries
    // Check that lwm1, lwm2 cb is called
    DequeueEntries(8);
    EXPECT_EQ(8, work_queue_.Length());
    EXPECT_EQ(8, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(9, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Refill the queue
    // Enqueue 3 entries
    // Check that hwm2 cb is called
    EnqueueEntries(3);
    EXPECT_EQ(11, work_queue_.Length());
    EXPECT_EQ(11, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM2, wm_cb_type_);
    EXPECT_EQ(10, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(1, hwater_index);
    EXPECT_EQ(2, lwater_index);
    // Dequeue 1 entry
    // Check that no new low water mark cb is called
    DequeueEntries(1);
    EXPECT_EQ(10, work_queue_.Length());
    EXPECT_EQ(11, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM2, wm_cb_type_);
    EXPECT_EQ(10, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(1, hwater_index);
    EXPECT_EQ(2, lwater_index);
    // Dequeue 3 entries
    // Check that lwm2 cb is called
    DequeueEntries(3);
    EXPECT_EQ(7, work_queue_.Length());
    EXPECT_EQ(8, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(11, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Enqueue 1 entry
    // Check that no new high water mark cb is called
    EnqueueEntries(1);
    EXPECT_EQ(8, work_queue_.Length());
    EXPECT_EQ(8, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(11, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Dequeue 7 entries
    // Check that lwm3 is called
    DequeueEntries(7);
    EXPECT_EQ(1, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(12, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
    // Dequeue 1 entry
    // Check that no new low water mark cb is called
    DequeueEntries(1);
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(12, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
    // Refill the queue
    // Enqueue 4 entries
    // Check that hwm1 is called
    EnqueueEntries(5);
    EXPECT_EQ(5, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(13, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Empty the queue
    // Check that lwm3 is called
    DequeueEntries(5);
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(14, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
    // Refill the queue
    // Enqueue 4 entries
    // Check that hwm1 is called
    EnqueueEntries(5);
    EXPECT_EQ(5, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(15, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Dequeue 2 entries
    // Check that no new low water mark cb is called
    DequeueEntries(2);
    EXPECT_EQ(3, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(15, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(0, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Enqueue 1 entry
    // Check that no new high water cb is called
    EnqueueEntries(1);
    EXPECT_EQ(4, work_queue_.Length());
    EXPECT_EQ(5, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(15, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(1, lwater_index);
    // Dequeue 3 entries
    // Check that lwm3 is called
    DequeueEntries(3);
    EXPECT_EQ(1, work_queue_.Length());
    EXPECT_EQ(2, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(16, wm_cb_count_);
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(0, lwater_index);
}

TEST_F(QueueTaskTest, WaterMarkParallelTest) {
    // Setup high watermarks
    WaterMarkInfos vhwm;
    WaterMarkInfo hwm(0,
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

TEST_F(QueueTaskTest, ScheduleShutdownTest) {
    // Stop the scheduler
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    // Enqueue entries
    for (int idx = 0; idx < 5; idx++) {
        work_queue_.Enqueue(idx);
    }
    EXPECT_EQ(5, work_queue_.Length());
    EXPECT_TRUE(IsWorkQueueRunning());
    EXPECT_TRUE(IsWorkQueueCurrentRunner());
    EXPECT_FALSE(IsWorkQueueShutdownScheduled());
    // Shutdown the work queue (should happen in the same context)
    work_queue_.ScheduleShutdown();
    // Verify
    EXPECT_EQ(0, work_queue_.Length());
    EXPECT_FALSE(IsWorkQueueRunning());
    EXPECT_FALSE(IsWorkQueueCurrentRunner());
    EXPECT_TRUE(IsWorkQueueShutdownScheduled());
    // Start the scheduler
    scheduler->Start();
}

TEST_F(QueueTaskTest, ProcessWaterMarksParallelTest) {
    // Setup watermarks
    WaterMarkInfo hwm1(90000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::HWM1));
    work_queue_.SetHighWaterMark(hwm1);
    WaterMarkInfo hwm2(50000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::HWM2));
    work_queue_.SetHighWaterMark(hwm2);
    WaterMarkInfo hwm3(10000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::HWM3));
    work_queue_.SetHighWaterMark(hwm3);
    WaterMarkInfo lwm1(75000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::LWM1));
    WaterMarkInfo lwm2(35000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::LWM2));
    WaterMarkInfo lwm3(5000,
        boost::bind(&QueueTaskTest::WaterMarkCallbackVerifyIndexes, this, _1,
            WaterMarkTestCbType::LWM3));
    WaterMarkInfos lwm;
    lwm.push_back(lwm1);
    lwm.push_back(lwm2);
    lwm.push_back(lwm3);
    work_queue_.SetLowWaterMark(lwm);
    int hwater_index, lwater_index;
    WorkQueueWaterMarkIndexes(&hwater_index, &lwater_index);
    EXPECT_EQ(-1, hwater_index);
    EXPECT_EQ(-1, lwater_index);
    TaskScheduler *scheduler(TaskScheduler::GetInstance());
    EnqueueTask *etask(new EnqueueTask(&work_queue_,
                scheduler->GetTaskId(
                "::test::QueueTaskTest::ProcessWaterMarksParallelTest"),
                100000));
    scheduler->Enqueue(etask);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(VerifyWaterMarkIndexes());
    EXPECT_TRUE((wm_cb_type_ == WaterMarkTestCbType::LWM3 &&
                 wm_cb_qsize_ == 5000) ||
                (wm_cb_type_ == WaterMarkTestCbType::INVALID &&
                 wm_cb_qsize_ == 0));
}

class QueueTaskShutdownTest : public ::testing::Test {
public:
    QueueTaskShutdownTest() :
        wq_task_id_(TaskScheduler::GetInstance()->GetTaskId(
                        "::test::QueueTaskShutdownTest")),
        work_queue_(wq_task_id_, -1,
                    boost::bind(&QueueTaskShutdownTest::DequeueCb, this, _1)) {
        dequeue_cb_sleep_ = true;
        dequeue_cb_running_ = false;
    }

    virtual void SetUp() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }

    virtual void TearDown() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }

    bool DequeueCb(int *entry) {
        dequeue_cb_running_ = true;
        while (dequeue_cb_sleep_) {
            usleep(100000);
        }
        delete entry;
        return true;
    }

    bool IsWorkQueueRunning() {
        return work_queue_.running_;
    }

    bool IsWorkQueueCurrentRunner() {
        return work_queue_.current_runner_ != NULL;
    }

    bool IsWorkQueueShutdownScheduled() {
        return work_queue_.shutdown_scheduled_;
    }

    int wq_task_id_;
    WorkQueue<int *> work_queue_;
    tbb::atomic<bool> dequeue_cb_sleep_;
    tbb::atomic<bool> dequeue_cb_running_;
};

template<>
struct WorkQueueDelete<int *> {
    template <typename QueueT>
    void operator()(QueueT &q, bool delete_entry) {
        EXPECT_FALSE(delete_entry);
        int *idx;
        while (q.try_pop(idx)) {
            delete idx;
        }
    }
};

TEST_F(QueueTaskShutdownTest, ScheduleShutdown) {
    // First disable the work queue
    work_queue_.set_disable(true);
    // Enqueue 2 entries
    for (int idx = 0; idx < 2; idx++) {
        work_queue_.Enqueue(new int(idx));
    }
    EXPECT_EQ(2, work_queue_.Length());
    // Now enable
    work_queue_.set_disable(false);
    EXPECT_TRUE(IsWorkQueueRunning());
    EXPECT_TRUE(IsWorkQueueCurrentRunner());
    EXPECT_FALSE(IsWorkQueueShutdownScheduled());
    TASK_UTIL_EXPECT_TRUE(dequeue_cb_running_);
    // Shutdown the work queue (should happen in dequeue context)
    work_queue_.ScheduleShutdown(false);
    // Verify
    EXPECT_EQ(1, work_queue_.Length());
    EXPECT_TRUE(IsWorkQueueRunning());
    EXPECT_TRUE(IsWorkQueueCurrentRunner());
    EXPECT_TRUE(IsWorkQueueShutdownScheduled());
    // Set bool to exit dequeue callback
    dequeue_cb_sleep_ = false;
    TASK_UTIL_EXPECT_FALSE(IsWorkQueueRunning());
    TASK_UTIL_EXPECT_FALSE(IsWorkQueueCurrentRunner());
    EXPECT_EQ(0, work_queue_.Length());
}

struct QWMTestEntry {
    QWMTestEntry() :
        size_(0) {
    }
    QWMTestEntry(size_t size) :
        size_(size) {
    }
    size_t size_;
};

class QueueTaskWaterMarkTest : public ::testing::Test {
 public:
    QueueTaskWaterMarkTest() :
        wq_task_id_(TaskScheduler::GetInstance()->GetTaskId(
                        "::test::QueueTaskWaterMarkTest")),
        work_queue_(wq_task_id_, -1,
                    boost::bind(&QueueTaskWaterMarkTest::Dequeue, this, _1),
                    32 * 1024),
        wm_cb_qsize_(0),
        wm_cb_count_(0),
        wm_cb_type_(WaterMarkTestCbType::INVALID),
        qsize_(0) {
    }
    virtual void SetUp() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }
    virtual void TearDown() {
        TaskScheduler::GetInstance()->ClearTaskStats(wq_task_id_);
    }
    bool Dequeue(QWMTestEntry &entry) {
        return true;
    }
    void SetWorkQueueMaxIterations(size_t niterations) {
        work_queue_.max_iterations_ = niterations;
    }
    bool DequeueTaskReady(bool start_runner) {
        if (start_runner) {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&StartRunnerAlways));
        } else {
            work_queue_.SetStartRunnerFunc(
                boost::bind(&StartRunnerNever));
       }
       return true;
    }
    void EnqueueEntries(int count, size_t size) {
        work_queue_.SetStartRunnerFunc(
            boost::bind(&StartRunnerNever));
        for (int i = 0; i < count; i++) {
            work_queue_.Enqueue(QWMTestEntry(size));
            qcount_.push(size);
            qsize_ += size;
        }
    }
    void DequeueEntries(int count) {
        work_queue_.SetStartRunnerFunc(
            boost::bind(&StartRunnerAlways));
        SetWorkQueueMaxIterations(count);
        work_queue_.SetExitCallback(
            boost::bind(&QueueTaskWaterMarkTest::DequeueTaskReady, this, false));
        work_queue_.MayBeStartRunner();
        task_util::WaitForIdle(1);
        for (int i = 0; i < count; i++) {
           qsize_ -= qcount_.front();
           qcount_.pop();
        }
    }

    int wq_task_id_;
    WorkQueue<QWMTestEntry> work_queue_;
    size_t wm_cb_qsize_;
    size_t wm_cb_count_;
    WaterMarkTestCbType::type wm_cb_type_;
    size_t qsize_;
    std::queue<size_t> qcount_;
};

template<>
size_t WorkQueue<QWMTestEntry>::AtomicIncrementQueueCount(
    QWMTestEntry *entry) {
    return count_.fetch_and_add(entry->size_) + entry->size_;
}

template<>
size_t WorkQueue<QWMTestEntry>::AtomicDecrementQueueCount(
    QWMTestEntry *entry) {
    return count_.fetch_and_add(0-entry->size_) - entry->size_;
}

TEST_F(QueueTaskWaterMarkTest, Basic) {
    // Setup watermarks
    WaterMarkInfo hwm1(4 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM1, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm1);
    WaterMarkInfo hwm2(8 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM2, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm2);
    WaterMarkInfo hwm3(12 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::HWM3, &wm_cb_type_));
    work_queue_.SetHighWaterMark(hwm3);
    WaterMarkInfo lwm1(10 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM1, &wm_cb_type_));
    WaterMarkInfo lwm2(6 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM2, &wm_cb_type_));
    WaterMarkInfo lwm3(2 * 1024,
        boost::bind(&WaterMarkTestCb, _1, &wm_cb_qsize_,
            &wm_cb_count_, WaterMarkTestCbType::LWM3, &wm_cb_type_));
    WaterMarkInfos lwm;
    lwm.push_back(lwm1);
    lwm.push_back(lwm2);
    lwm.push_back(lwm3);
    work_queue_.SetLowWaterMark(lwm);
    EXPECT_EQ(WaterMarkTestCbType::INVALID, wm_cb_type_);
    EXPECT_EQ(0, wm_cb_qsize_);
    EXPECT_EQ(0, wm_cb_count_);
    // Enqueue 4 entries 1024 bytes each
    // Check that hwm1 cb is called
    EnqueueEntries(4, 1024);
    EXPECT_EQ(4, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(4 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(1, wm_cb_count_);
    // Enqueue 2 entries 512 bytes each
    // Check that no new high water mark cb is called
    EnqueueEntries(2, 512);
    EXPECT_EQ(6, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(4 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(1, wm_cb_count_);
    // Enqueue 7 entries 2048 bytes each
    // Check that hwm2, hwm3 cb is called
    EnqueueEntries(7, 2048);
    EXPECT_EQ(13, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(13 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(3, wm_cb_count_);
    // Dequeue 8 entries -
    // 4 entries 1024 bytes each
    // 2 entries 512 bytes each
    // 2 entries 2048 bytes each
    // Check that lwm1 cb is called
    DequeueEntries(8);
    EXPECT_EQ(8, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(10 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM1, wm_cb_type_);
    EXPECT_EQ(4, wm_cb_count_);
    // Refill the queue
    // Enqueue 5 entries 1024 bytes each
    // Check that hwm3 cb is called
    EnqueueEntries(5, 1024);
    EXPECT_EQ(18, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(12 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(5, wm_cb_count_);
    // Dequeue 1 entry 2048 byte
    // Check that no new low water cb is called
    DequeueEntries(1);
    EXPECT_EQ(9, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(12 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(5, wm_cb_count_);
    // Dequeue 5 entries -
    // 4 entries 2048 bytes each
    // 1 entry 1024 byte
    // Check that lwm1, lwm2 cb is called
    DequeueEntries(5);
    EXPECT_EQ(14, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(5 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(7, wm_cb_count_);
    // Refill the queue
    // Enqueue 8 entries 512 bytes each
    // Check that hwm2 cb is called
    EnqueueEntries(8, 512);
    EXPECT_EQ(26, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(8 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM2, wm_cb_type_);
    EXPECT_EQ(8, wm_cb_count_);
    // Dequeue 1 entry 1024 byte
    // Check that no new low water mark cb is called
    DequeueEntries(1);
    EXPECT_EQ(15, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(8 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM2, wm_cb_type_);
    EXPECT_EQ(8, wm_cb_count_);
    // Dequeue 2 entries 1024 bytes each
    // Check that lwm2 cb is called
    DequeueEntries(2);
    EXPECT_EQ(17, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(6 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(9, wm_cb_count_);
    // Enqueue 1 entry 1024 byte
    // Check that no new high water mark cb is called
    EnqueueEntries(1, 1024);
    EXPECT_EQ(27, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(6 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM2, wm_cb_type_);
    EXPECT_EQ(9, wm_cb_count_);
    // Dequeue 7 entries -
    // 1 entry 1024 byte
    // 6 entries 512 bytes each
    // Check that lwm3 is called
    DequeueEntries(7);
    EXPECT_EQ(24, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(2 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(10, wm_cb_count_);
    // Dequeue 1 entry 512 byte
    // Check that no new low water mark cb is called
    DequeueEntries(1);
    EXPECT_EQ(25, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(2 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(10, wm_cb_count_);
    // Refill the queue
    // Enqueue 3 entries 1024 bytes each
    // Check that hwm1 is called
    EnqueueEntries(3, 1024);
    EXPECT_EQ(30, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ((4 * 1024) + 512, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(11, wm_cb_count_);
    // Empty the queue
    // Check that lwm3 is called
    DequeueEntries(5);
    EXPECT_EQ(30, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(2 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(12, wm_cb_count_);
    // Jump tests
    // Enqueue 1 entry 9K byte
    // Check that hwm2 is called
    EnqueueEntries(1, 9 * 1024);
    EXPECT_EQ(31, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(9 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM2, wm_cb_type_);
    EXPECT_EQ(13, wm_cb_count_);
    // Dequeue 1 entry 9K byte
    // Check that lwm3 is called
    DequeueEntries(1);
    EXPECT_EQ(31, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(0, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(14, wm_cb_count_);
    // Enqueue 1 entry 4K byte
    // Check that hwm1 is called
    EnqueueEntries(1, 4 * 1024);
    EXPECT_EQ(32, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(4 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM1, wm_cb_type_);
    EXPECT_EQ(15, wm_cb_count_);
    // Enqueue 1 entry 9K byte
    // Check that hwm3 is called
    EnqueueEntries(1, 9 * 1024);
    EXPECT_EQ(33, work_queue_.NumEnqueues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(13 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::HWM3, wm_cb_type_);
    EXPECT_EQ(16, wm_cb_count_);
    // Dequeue 1 entry 4k byte
    // Check that lwm1 is called
    DequeueEntries(1);
    EXPECT_EQ(32, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(9 * 1024, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM1, wm_cb_type_);
    EXPECT_EQ(17, wm_cb_count_);
    // Empty the queue
    // Check the lwm3 is called
    DequeueEntries(1);
    EXPECT_EQ(33, work_queue_.NumDequeues());
    EXPECT_EQ(qsize_, work_queue_.Length());
    EXPECT_EQ(0, wm_cb_qsize_);
    EXPECT_EQ(WaterMarkTestCbType::LWM3, wm_cb_type_);
    EXPECT_EQ(18, wm_cb_count_);
    EXPECT_EQ(0, qsize_);
    EXPECT_TRUE(qcount_.empty());
}

TEST_F(QueueTaskWaterMarkTest, Duplicates) {
    // Setup high watermarks
    WaterMarkInfo hwm3(12 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm3);
    WaterMarkInfo hwm1(4 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm1);
    WaterMarkInfo hwm2(8 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm2);
    WaterMarkInfo hwm5(8 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm5);
    WaterMarkInfo hwm4(4 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm4);
    WaterMarkInfo hwm6(12 * 1024, NULL);
    work_queue_.SetHighWaterMark(hwm6);
    // Verify that no duplicates exist and the watermarks are sorted
    WaterMarkInfos expected_hwms = boost::assign::list_of
        (WaterMarkInfo(4 * 1024, NULL))
        (WaterMarkInfo(8 * 1024, NULL))
        (WaterMarkInfo(12 * 1024, NULL));
    WaterMarkInfos actual_hwms = work_queue_.GetHighWaterMark();
    EXPECT_EQ(actual_hwms, expected_hwms);
    // Setup low watermarks
    WaterMarkInfo lwm1(10 * 1024, NULL);
    WaterMarkInfo lwm2(6 * 1024, NULL);
    WaterMarkInfo lwm3(2 * 1024, NULL);
    WaterMarkInfo lwm4(10 * 1024, NULL);
    WaterMarkInfo lwm5(6 * 1024, NULL);
    WaterMarkInfo lwm6(2 * 1024, NULL);
    WaterMarkInfos lwm;
    lwm.push_back(lwm1);
    lwm.push_back(lwm2);
    lwm.push_back(lwm3);
    lwm.push_back(lwm4);
    lwm.push_back(lwm5);
    lwm.push_back(lwm6);
    work_queue_.SetLowWaterMark(lwm);
    // Verify that no duplicates exist and the watermarks are sorted
    WaterMarkInfos expected_lwms = boost::assign::list_of
        (WaterMarkInfo(2 * 1024, NULL))
        (WaterMarkInfo(6 * 1024, NULL))
        (WaterMarkInfo(10 * 1024, NULL));
    WaterMarkInfos actual_lwms(work_queue_.GetLowWaterMark());
    EXPECT_EQ(actual_lwms, expected_lwms);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
