/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BASE_TIME_UTIL_H__
#define BASE_TIME_UTIL_H__

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

/* timestamp - returns usec since epoch */
static inline uint64_t UTCTimestampUsec() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        assert(0);
    }

    return ts.tv_sec * 1000000 + ts.tv_nsec/1000;
}

// Monotonically increasing timer starting from an arbitrary value
// 10x more efficient than UTCTimestampUsec
static inline uint64_t ClockMonotonicUsec() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        assert(0);
    }

    return ts.tv_sec * 1000000 + ts.tv_nsec/1000;
}

static inline boost::posix_time::ptime UTCUsecToPTime(uint64_t tusec) {
    boost::posix_time::ptime pt(boost::gregorian::date(1970, 1, 1), 
                   boost::posix_time::time_duration(0, 0, 
                   tusec/1000000, 
                   boost::posix_time::time_duration::ticks_per_second()/1000000*(tusec%1000000)));
    return pt;
}

static inline const std::string duration_usecs_to_string(const uint64_t usecs) {
    std::ostringstream os;
    boost::posix_time::time_duration duration;

    duration = boost::posix_time::microseconds(usecs);
    os << duration;
    return os.str();
}

#endif  // BASE_TIME_UTIL_H__
