/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/time.h>
#include "testing/gunit.h"
#include "base/util.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace {

static inline uint64_t UTCgettimeofday() {
    struct timeval tv;
    if (gettimeofday(&tv, (struct timezone *)0) != 0) {
        assert(0);
    }

    return tv.tv_sec * 1000000 + tv.tv_usec;
}

static boost::posix_time::ptime epoch_ptime(boost::gregorian::date(1970,1,1));
/* timestamp - returns usec since epoch */
static inline uint64_t UTCboost() {
    boost::posix_time::ptime t2(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::time_duration diff = t2 - epoch_ptime;
    return diff.total_microseconds();
}

class UtilTest : public ::testing::Test {
};

TEST_F(UtilTest, Correctness) {
    uint64_t utc_clock_gettime[5], utc_gettimeofday[5], utc_boost[5];
    for (int i = 0; i < 5; i++) {
        utc_clock_gettime[i] = UTCTimestampUsec(); 
        utc_gettimeofday[i] = UTCgettimeofday();
        utc_boost[i] = UTCboost();
    }
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE( (utc_clock_gettime[i]-utc_gettimeofday[i]) < 1000 ||
                    (utc_gettimeofday[i]-utc_clock_gettime[i]) < 1000);
        EXPECT_TRUE( (utc_gettimeofday[i]-utc_boost[i]) < 1000 ||
                    (utc_boost[i]-utc_gettimeofday[i]) < 1000);
    }
}

TEST_F(UtilTest, DISABLED_PerfUTCclock_gettime) {
    for (int i = 0; i < 1000000; i++) {
       uint64_t t = UTCTimestampUsec();
    }
}

TEST_F(UtilTest, DISABLED_PerfUTCgettimeofday) {
    for (int i = 0; i < 1000000; i++) {
        uint64_t t = UTCgettimeofday();
    }
}

TEST_F(UtilTest, DISABLED_PerfUTCboost) {
    for (int i = 0; i < 1000000; i++) {
        uint64_t t = UTCboost();
    }
}

} // namespace

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
