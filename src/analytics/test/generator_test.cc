//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>
#include <analytics/generator.h>

namespace {

class SandeshGeneratorTest : public ::testing::Test {
};

// Test for GetDeferTimeMSec(event_time, last_expiry_time, last_defer_time)
TEST_F(SandeshGeneratorTest, DeferTime) {
    int initial_defer_time(SandeshGenerator::kInitialSmDeferTimeMSec);
    uint64_t event_time_usec(UTCTimestampUsec());
    // First time event
    int defer_time(GetDeferTimeMSec(event_time_usec, 0, 0));
    EXPECT_EQ(initial_defer_time, defer_time);
    // Event time same as last expiry time
    defer_time = GetDeferTimeMSec(event_time_usec, event_time_usec, 0);
    EXPECT_EQ(initial_defer_time, defer_time);
    int last_defer_time_usec(initial_defer_time * 1000);
    defer_time = GetDeferTimeMSec(event_time_usec, event_time_usec,
        last_defer_time_usec);
    EXPECT_EQ((last_defer_time_usec * 2)/1000, defer_time);
    // Event time is between last defer time and 2 * last defer time
    last_defer_time_usec = (initial_defer_time * 2) * 1000;
    defer_time = GetDeferTimeMSec(event_time_usec,
        event_time_usec - last_defer_time_usec, last_defer_time_usec);
    EXPECT_EQ((last_defer_time_usec * 2)/1000, defer_time);
    // Event time is between 2 * last defer time and 4 * last defer time
    last_defer_time_usec = (initial_defer_time * 3) * 1000;
    defer_time = GetDeferTimeMSec(event_time_usec,
        event_time_usec - (3 * last_defer_time_usec), last_defer_time_usec);
    EXPECT_EQ(last_defer_time_usec/1000, defer_time);
    // Event time is beyond 4 * last defer time
    last_defer_time_usec = (initial_defer_time * 2) * 1000;
    defer_time = GetDeferTimeMSec(event_time_usec,
        event_time_usec - (5 * last_defer_time_usec), last_defer_time_usec);
    EXPECT_EQ(initial_defer_time, defer_time);
    // Maximum defer time
    last_defer_time_usec = (SandeshGenerator::kMaxSmDeferTimeMSec * 1000)/2;
    defer_time = GetDeferTimeMSec(event_time_usec,
        event_time_usec - last_defer_time_usec, last_defer_time_usec);
    EXPECT_EQ(static_cast<int>(SandeshGenerator::kMaxSmDeferTimeMSec),
        defer_time);
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    int result = RUN_ALL_TESTS();
    return result;
}
