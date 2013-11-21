/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

class EventManagerTest : public ::testing::Test {
protected:
    EventManagerTest() : thread_(&evm_) { }

    virtual void SetUp() {
        thread_.Start();
    }

    virtual void TearDown() {
        evm_.Shutdown();
        thread_.Join();
    }

    EventManager evm_;
    ServerThread thread_;
};

typedef EventManagerTest EventManagerDeathTest;

TEST_F(EventManagerDeathTest, Poll) {
    usleep(10000);
    EXPECT_EXIT(evm_.Poll(),
        ::testing::KilledBySignal(SIGABRT), ".*Poll.*");
}

TEST_F(EventManagerDeathTest, RunOnce) {
    usleep(10000);
    EXPECT_EXIT(evm_.RunOnce(),
        ::testing::KilledBySignal(SIGABRT), ".*RunOnce.*");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
