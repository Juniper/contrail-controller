/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/task_monitor.h"
#include "base/logging.h"

using namespace std;

class TaskMonitorTest : public ::testing::Test {
public:
    virtual void SetUp() {
        monitor_ = new TaskMonitor(NULL, 10, 1000, 20);
        RestLastActivity();
    }

    virtual void TearDown() {
        delete monitor_;
        monitor_ = NULL;
    }

    bool Monitor(uint64_t t, uint64_t enqueue_count, uint64_t done_count) {
        return monitor_->Monitor(t*1000, enqueue_count, done_count);
    }

    bool Monitor(TaskMonitor *m, uint64_t t, uint64_t enqueue_count,
                 uint64_t done_count) {
        return m->Monitor(t*1000, enqueue_count, done_count);
    }

    bool Validate(uint64_t t, uint64_t enqueue_count, uint64_t done_count) {
        EXPECT_EQ(monitor_->last_activity() / 1000, t);
        if (monitor_->last_activity() / 1000 != t) {
            return false;
        }

        EXPECT_EQ(monitor_->last_enqueue_count(), enqueue_count);
        if (monitor_->last_enqueue_count() != enqueue_count) {
            return false;
        }

        EXPECT_EQ(monitor_->last_done_count(), done_count);
        if (monitor_->last_done_count() != done_count) {
            return false;
        }

        return true;
    }

    void RestLastActivity() {
        monitor_->last_activity_ = 0;
    }

    void SetTimers(uint64_t keepawake_time, uint64_t inactivity_time,
                   uint64_t poll_interval) {
        monitor_->tbb_keepawake_time_msec_ = keepawake_time;
        monitor_->inactivity_time_usec_ = inactivity_time * 1000;
        monitor_->poll_interval_msec_ = poll_interval;
    }

    void UpdateTimers() {
        monitor_->UpdateTimers();
    }

    bool ValidateTimers(uint64_t inactivity, uint64_t poll) {
        EXPECT_EQ(monitor_->inactivity_time_msec(), inactivity);
        if (monitor_->inactivity_time_msec() != inactivity) {
            return false;
        }

        EXPECT_EQ(monitor_->poll_interval_msec(), poll);
        if (monitor_->poll_interval_msec() != poll) {
            return false;
        }

        return true;
    }


protected:
    TaskMonitor *monitor_;
};

// Test init for different values of enqueue_count and done_count
TEST_F(TaskMonitorTest, init_1) {
    EXPECT_TRUE(Monitor(1000, 0, 0));
}

TEST_F(TaskMonitorTest, init_2) {
    EXPECT_TRUE(Monitor(1000, 1, 0));
}

TEST_F(TaskMonitorTest, init_3) {
    EXPECT_TRUE(Monitor(1000, 0, 1));
}

TEST_F(TaskMonitorTest, init_4) {
    EXPECT_TRUE(Monitor(1000, 1, 1));
}

// Activity detected since done_count changing
TEST_F(TaskMonitorTest, activity_1) {
    // First invocation is for initialization
    EXPECT_TRUE(Monitor(1000, 0, 0));

    // Activity after timeout
    EXPECT_TRUE(Monitor(1001, 1, 1));
    EXPECT_TRUE(Validate(1001, 1, 1));

    EXPECT_TRUE(Monitor(3000, 10, 9));
    EXPECT_TRUE(Validate(3000, 10, 9));

    EXPECT_TRUE(Monitor(4000, 20, 19));
    EXPECT_TRUE(Validate(4000, 20, 19));
}

// Inactivity ignored since enqueue not changed
TEST_F(TaskMonitorTest, no_enqueue_1) {
    // First invocation is for initialization
    EXPECT_TRUE(Monitor(1000, 0, 0));

    // First activity
    EXPECT_TRUE(Monitor(1001, 0, 0));
    EXPECT_TRUE(Validate(1001, 0, 0));

    EXPECT_TRUE(Monitor(3000, 0, 0));
    EXPECT_TRUE(Validate(3000, 0, 0));

    // Force some activity
    EXPECT_TRUE(Monitor(4000, 1, 1));
    EXPECT_TRUE(Validate(4000, 1, 1));

    EXPECT_TRUE(Monitor(5000, 1, 1));
    EXPECT_TRUE(Validate(5000, 1, 1));
}

// Tests with done_count_ not changing
TEST_F(TaskMonitorTest, no_done_1) {
    // First invocation is for initialization
    EXPECT_TRUE(Monitor(1000, 0, 0));

    // Monitor succeeds since timeout not exceeded
    EXPECT_TRUE(Monitor(1001, 1, 0));
    EXPECT_TRUE(Validate(1000, 0, 0));

    EXPECT_TRUE(Monitor(1099, 2, 0));
    EXPECT_TRUE(Validate(1000, 0, 0));

    // Monitor succeeds with timeout exceeded
    EXPECT_FALSE(Monitor(2001, 3, 0));
    EXPECT_TRUE(Validate(1000, 0, 0));

    // Monitor succeeds since done_count changed
    EXPECT_TRUE(Monitor(3000, 3, 1));
    EXPECT_TRUE(Validate(3000, 3, 1));

    // Monitor succeeds since timout is equal and doesnt exceed
    EXPECT_TRUE(Monitor(4000, 4, 1));
    EXPECT_TRUE(Validate(3000, 3, 1));

    EXPECT_FALSE(Monitor(5001, 5, 1));
    EXPECT_TRUE(Validate(3000, 3, 1));

    EXPECT_TRUE(Monitor(6000, 10, 10));
    EXPECT_TRUE(Validate(6000, 10, 10));

    EXPECT_TRUE(Monitor(6000, 10, 11));
    EXPECT_TRUE(Validate(6000, 10, 11));
}

TEST_F(TaskMonitorTest, timer_update_1) {
    SetTimers(10, 100, 10);
    UpdateTimers();
    EXPECT_TRUE(ValidateTimers(1000, 20));

    SetTimers(20, 100, 10);
    UpdateTimers();
    EXPECT_TRUE(ValidateTimers(2000, 40));

    SetTimers(10, 50, 2);
    UpdateTimers();
    EXPECT_TRUE(ValidateTimers(1000, 20));

    SetTimers(10, 4000, 100);
    UpdateTimers();
    EXPECT_TRUE(ValidateTimers(5000, 100));

    SetTimers(10, 20000, 100);
    UpdateTimers();
    EXPECT_TRUE(ValidateTimers(20000, 100));
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
