/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__TASK_TEST_UTIL_H__
#define __BASE__TASK_TEST_UTIL_H__

#include <boost/function.hpp>
#include "testing/gunit.h"

class EventManager;

namespace task_util {
void WaitForIdle(long wait_seconds = 30, bool running_only = false);
void WaitForCondition(EventManager *evm, boost::function<bool(void)> condition,
                      const int timeout);
void BusyWork(EventManager *evm, const int timeout);
void TaskSchedulerStop();
void TaskSchedulerStart();

class TaskSchedulerLock {
public:
    TaskSchedulerLock();
    ~TaskSchedulerLock();
};

}

#define TASK_UTIL_WAIT_EQ_NO_MSG(expected, actual, wait, retry, msg)           \
do {                                                                           \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) == (actual)) break;                                     \
        usleep(wait);                                                          \
    }                                                                          \
    EXPECT_TRUE((expected) == (actual));                                       \
} while (false)

#define TASK_UTIL_WAIT_NE_NO_MSG(expected, actual, wait, retry, msg)           \
do {                                                                           \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) != (actual)) break;                                     \
        usleep(wait);                                                          \
    }                                                                          \
    EXPECT_TRUE((expected) != (actual));                                       \
} while (false)

#define TASK_UTIL_WAIT_MSG(cnt, expected, actual, wait, type, msg)             \
    do {                                                                       \
        ostream << __FILE__ << ":" <<  __FUNCTION__ << "():" << __LINE__;      \
        ostream << ": " << msg << ": Waiting for " << actual << type;          \
        ostream << expected << "\n";                                           \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();               \
        LOG4CPLUS_DEBUG(logger,  ostream.str());                               \
    } while (false)

#define TASK_UTIL_WAIT_EQ(expected, actual, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) == (actual)) break;                                     \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((expected) == (actual)) break;                                     \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, expected, actual, wait, " to become ", msg);    \
    }                                                                          \
    EXPECT_EQ(expected, actual);                                               \
} while (false)

#define TASK_UTIL_WAIT_NE(expected, actual, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) != (actual)) break;                                     \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((expected) != (actual)) break;                                     \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, expected, actual, wait, " to not remain ", msg);\
    }                                                                          \
    EXPECT_NE(expected, actual);                                               \
} while (false)

#define TASK_UTIL_EXPECT_VECTOR_EQ(actual, expected)             \
    do {                                                         \
        TASK_UTIL_EXPECT_EQ((expected).size(), (actual).size()); \
        for (int i = 0; i < (expected).size(); i++) {            \
            TASK_UTIL_EXPECT_EQ((expected)[i], (actual)[i]);     \
        }                                                        \
    } while (false)

#define TASK_UTIL_DEFAULT_WAIT_TIME   1000 // us
#define TASK_UTIL_DEFAULT_RETRY_COUNT 5000

static inline unsigned long long int task_util_wait_time() {
    static bool init;
    static unsigned long long int wait = TASK_UTIL_DEFAULT_WAIT_TIME;

    if (!init) {
        init = true;
        char *str = getenv("TASK_UTIL_WAIT_TIME");
        if (str) {
            wait = strtoull(str, NULL, 0);
        }
    }

    return wait;
}

static inline unsigned long long int task_util_retry_count() {
    static bool init;
    static unsigned long long int retry = TASK_UTIL_DEFAULT_RETRY_COUNT;

    if (!init) {
        init = true;
        char *str = getenv("TASK_UTIL_RETRY_COUNT");
        if (str) {
            retry = strtoull(str, NULL, 0);
        }
    }

    return retry;
}

#define TASK_UTIL_EXPECT_EQ(expected, actual) \
    TASK_UTIL_WAIT_EQ(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), "")

#define TASK_UTIL_EXPECT_EQ_MSG(expected, actual, msg) \
    TASK_UTIL_WAIT_EQ(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_NE(expected, actual) \
    TASK_UTIL_WAIT_NE(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_NE_MSG(expected, actual, msg) \
    TASK_UTIL_WAIT_NE(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_TRUE(condition) \
    TASK_UTIL_WAIT_EQ_NO_MSG(true, condition, task_util_wait_time(), \
                             task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_TRUE_MSG(condition, msg) \
    TASK_UTIL_WAIT_EQ_NO_MSG(true, condition, task_util_wait_time(), \
                             task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_FALSE(condition) \
    TASK_UTIL_WAIT_EQ_NO_MSG(false, condition, task_util_wait_time(), \
                             task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_FALSE_MSG(condition, msg) \
    TASK_UTIL_WAIT_EQ_NO_MSG(false, condition, task_util_wait_time(), \
                             task_util_retry_count(), msg)

#define TASK_UTIL_ASSERT_EQ(expected, actual)                                  \
    do {                                                                       \
        TASK_UTIL_WAIT_EQ(expected, actual, task_util_wait_time(),             \
                          task_util_retry_count(), "");                        \
        ASSERT_EQ(expected, actual);                                           \
    } while (false)

#define TASK_UTIL_ASSERT_NE(expected, actual)                                  \
    do {                                                                       \
        TASK_UTIL_WAIT_NE(expected, actual, task_util_wait_time(),             \
                task_util_retry_count(), "");                                  \
        ASSERT_NE(expected, actual);                                           \
    } while (false)

#define TASK_UTIL_ASSERT_TRUE(condition)                                       \
    do {                                                                       \
        TASK_UTIL_WAIT_EQ_NO_MSG(true, condition, task_util_wait_time(),       \
                task_util_retry_count(), "");                                  \
        ASSERT_EQ(true, condition);                                            \
    } while (false)

#define TASK_UTIL_ASSERT_FALSE(condition)                                      \
    do {                                                                       \
        TASK_UTIL_WAIT_EQ_NO_MSG(false, condition, task_util_wait_time(),      \
                                 task_util_retry_count(), "");                 \
        ASSERT_EQ(false, condition);                                           \
    } while (false)

// Check for a match for c++ symbol type. Do a partial match in darwin, due to
// issue with symbol demangle.
#ifdef DARWIN
#define TASK_UTIL_EXPECT_EQ_TYPE_NAME(expected, actual)                        \
        EXPECT_NE(std::string::npos, (actual).find(expected))
#else
#define TASK_UTIL_EXPECT_EQ_TYPE_NAME(expected, actual)                        \
        EXPECT_EQ(expected, actual);
#endif

#endif // __BASE__TASK_TEST_UTIL_H__
