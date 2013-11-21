/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <fstream>
#include "tbb/atomic.h"
#include "io/test/event_manager_test.h"
#include "base/test/task_test_util.h"
#include "base/logging.h"
#include "base/timer.h"
#include "testing/gunit.h"

using namespace std;
using namespace tbb;

TaskScheduler       *scheduler;
atomic<int> timer_count_;
atomic<bool> timer_hold_;

class TimerTest : public Timer {
public:
    TimerTest(boost::asio::io_service &service, const std::string &name,
              int task_id, int task_instance)
        : Timer(service, name, task_id, task_instance) {
        TimerManager::AddTimer(this);
        count_++;
    }

    TimerTest(boost::asio::io_service &service, const std::string &name)
        : Timer(service, name, Timer::GetTimerTaskId(),
                Timer::GetTimerInstanceId()) {
        TimerManager::AddTimer(this);
        count_++;
    }

    virtual ~TimerTest() {
        count_--;
    }

    static atomic<uint32_t> count_;
};
atomic<uint32_t> TimerTest::count_;

class TimerUT : public ::testing::Test {
public:
    TimerUT() : evm_(new EventManager()) { };

    ~TimerUT() {
        TASK_UTIL_EXPECT_EQ(0, TimerTest::count_);
    }

    virtual void SetUp() {
        thread_.reset(new ServerThread(evm_.get()));
        thread_->Start();		// Must be called after initialization
        timer_count_ = 0;
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    auto_ptr<ServerThread> thread_;
    auto_ptr<EventManager> evm_;
};

bool TimerCb() {
    timer_count_.fetch_and_increment();
    return false;
}

bool PeriodicTimerCb() {
    timer_count_.fetch_and_decrement();
    return timer_count_ != 0;
}

bool TimerCbSleep() {
    timer_hold_ = true;
    while (timer_hold_ != false) {
        usleep(1000);
    }
    timer_count_.fetch_and_increment();
    return false;
}

void ValidateTimerCount(int count, int delay) {
    usleep(delay * 1000);
    TASK_UTIL_EXPECT_EQ(count, timer_count_);
    return;
}

TEST_F(TimerUT, basic_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Basic-1");
    TimerTest *timer2 = new TimerTest(*evm_->io_service(), "Basic-2");
    TimerTest *timer3 = new TimerTest(*evm_->io_service(), "Basic-3");
    TimerTest *timer4 = new TimerTest(*evm_->io_service(), "Basic-4");
    TimerTest *timer5 = new TimerTest(*evm_->io_service(), "Basic-5");
    timer1->Start(100, TimerCb);
    timer2->Start(100, TimerCb);
    timer3->Start(100, TimerCb);
    timer4->Start(100, TimerCb);
    timer5->Start(100, TimerCb);
    ValidateTimerCount(5, 100);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
    EXPECT_TRUE(TimerManager::DeleteTimer(timer2));
    EXPECT_TRUE(TimerManager::DeleteTimer(timer3));
    EXPECT_TRUE(TimerManager::DeleteTimer(timer4));
    EXPECT_TRUE(TimerManager::DeleteTimer(timer5));
}

TEST_F(TimerUT, basic_reuse_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Basic-1");

    timer_count_ = 100;
    timer1->Start(1, PeriodicTimerCb);
    ValidateTimerCount(0, 100);

    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, basic_periodic) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Basic-1");
    TimerTest *timer2 = new TimerTest(*evm_->io_service(), "Basic-2");
    timer1->Start(100, TimerCb);
    timer2->Start(100, TimerCb);
    ValidateTimerCount(2, 100);

    timer1->Start(100, TimerCb);
    timer2->Start(100, TimerCb);
    ValidateTimerCount(4, 100);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
    EXPECT_TRUE(TimerManager::DeleteTimer(timer2));
}

TEST_F(TimerUT, start_multiple_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "StartMultiple-1");
    timer1->Start(10, TimerCb);
    ValidateTimerCount(1, 20);
    timer1->Start(10, TimerCb);
    ValidateTimerCount(2, 20);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, restart_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Restart-1");
    timer1->Start(10, TimerCb);
    timer1->Start(20, TimerCb);
    ValidateTimerCount(1, 50);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, cancel_running_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Cancel-1");
    timer1->Start(10, TimerCb);
    timer1->Cancel();
    usleep(100);
    ValidateTimerCount(0, 100);

    timer1->Start(10, TimerCb);
    ValidateTimerCount(1, 10);
    task_util::WaitForIdle();

    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

// Cancel a fired job
TEST_F(TimerUT, cancel_fired_1) {
    timer_hold_ = false;
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Cancel-1");
    timer1->Start(10, TimerCbSleep);
    usleep(10*1000);
    while (timer_hold_ != true) {
        usleep(1000);
    }
    EXPECT_FALSE(timer1->Cancel());
    timer_hold_ = false;
    ValidateTimerCount(1, 100);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, cancel_running_2) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Init-1");
    TaskScheduler::GetInstance()->Stop();
    timer1->Start(10, TimerCb);
    usleep(50);
    TASK_UTIL_EXPECT_TRUE(timer1->Cancel());
    ValidateTimerCount(0, 20);
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, destroy_init_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Init-1");
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, destroy_running_1) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Init-1");
    timer1->Start(10, TimerCb);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
    ValidateTimerCount(0, 20);
}

TEST_F(TimerUT, destroy_fired_1) {
    timer_hold_ = false;
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "Cancel-1");
    timer1->Start(10, TimerCbSleep);
    usleep(10*1000);
    while (timer_hold_ != true) {
        usleep(1000);
    }
    EXPECT_FALSE(TimerManager::DeleteTimer(timer1));
    timer_hold_ = false;
    ValidateTimerCount(1, 100);
    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}

TEST_F(TimerUT, cancel_fired) {
    TimerTest *timer1 = new TimerTest(*evm_->io_service(), "cancel-fired-1");

    timer_count_ = 1000;
    for (int i = 0; i < 1000; i++) {
        timer1->Start(1, PeriodicTimerCb);
        usleep(1000);
        timer1->Cancel();
    }

    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, timer_count_);

    task_util::WaitForIdle();
    EXPECT_TRUE(TimerManager::DeleteTimer(timer1));
}


int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    scheduler = TaskScheduler::GetInstance();
    LoggingInit();
    return RUN_ALL_TESTS();
}
