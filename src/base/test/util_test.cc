/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/time.h>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "testing/gunit.h"
#include "base/util.h"
#include "base/logging.h"

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

TEST_F(UtilTest, UTCCorrectness) {
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

template<typename NumberType>
inline bool ParseIntegerSafe(const std::string &input, NumberType &num) {
    char *endptr;
    errno = 0; // To distinguish success/failure after call
    unsigned long value(strtoul(input.c_str(), &endptr, 10));
    // Check for various possible errors
    if ((errno == ERANGE && value == ULONG_MAX) ||
        (errno != 0 && value == 0)) {
        return false;
    }
    if (endptr == input.c_str()) {
        return false;
    }
    if (*endptr != '\0') {
        return false;
    }
    if (value > std::numeric_limits<NumberType>::max()) {
        return false;
    }
    num = (NumberType)value;
    return true;
}

template<typename NumberType>
inline bool ParseLongLongSafe(const std::string &input, NumberType &num) {
    char *endptr;
    errno = 0; // To distinguish success/failure after call
    unsigned long long value(strtoull(input.c_str(), &endptr, 10));
    // Check for various possible errors
    if ((errno == ERANGE && value == ULLONG_MAX) ||
        (errno != 0 && value == 0)) {
        return false;
    }
    if (endptr == input.c_str()) {
        return false;
    }
    if (*endptr != '\0') {
        return false;
    }
    num = (NumberType)value;
    return true;
}

template<typename NumberType>
inline bool ParseInteger(const std::string &input, NumberType &num) {
    char *endptr;
    num = strtoul(input.c_str(), &endptr, 10);
    return endptr[0] == '\0';
}

template <typename NumberType>
inline bool ParseIntegerStream(const std::string& str, NumberType &num) {
    std::stringstream ss(str);
    ss >> num;
    return !ss.fail();
}

// int8_t must be handled properly because stringstream sees int8_t
// as a text type instead of an integer type
template <>
inline bool ParseIntegerStream<>(const std::string& str, int8_t &num) {
    int16_t tmp;
    std::stringstream ss(str);
    ss >> tmp;
    if (ss.fail()) {
        return false;
    }
    num = (int8_t)tmp;
    return true;
}

TEST_F(UtilTest, DISABLED_StoiPerfStream) {
    std::string s_integer("123456789");
    int integer;
    for (int i = 0; i < 1000000; i++) {
       ParseIntegerStream(s_integer, integer);
    }
}

TEST_F(UtilTest, DISABLED_StoiPerfStrtoulSafe) {
    std::string s_integer("123456789");
    int integer;
    for (int i = 0; i < 1000000; i++) {
        ParseIntegerSafe(s_integer, integer);
    }
}

TEST_F(UtilTest, DISABLED_StoiPerfStrtoul) {
    std::string s_integer("123456789");
    int integer;
    for (int i = 0; i < 1000000; i++) {
        ParseInteger(s_integer, integer);
    }
}
} // namespace

int main(int argc, char *argv[]) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
