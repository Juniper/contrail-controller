/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__TASK_TEST_UTIL_H__
#define __BASE__TASK_TEST_UTIL_H__

#include <sys/time.h>
#include <sys/resource.h>

#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <base/task_trigger.h>
#include "testing/gunit.h"

class EventManager;

namespace task_util {
void WaitForIdle(long wait_seconds = 30, bool running_only = false,
                 bool verify = true);
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

// Fire user routine through task-triger inline and return after the task is
// complete.
//
// Usage example:
// task_util::TaskFire(boost::bind(&Example::ExampleRun, this, args),
//                     "bgp::Config");
//
// Note: One cannot call task_util::wait_for_idle() inside ExampleRun() as that
// we will never reach idle state until the user callback is complete.
class TaskFire {
public:
    typedef boost::function<void(void)> FunctionPtr;
    typedef boost::function<void(const void *)> FunctionPtr1;
    TaskFire(FunctionPtr func, const std::string task_name, int instance = 0);

private:
    bool Run();
    FunctionPtr func_;
    std::string task_name_;
    boost::scoped_ptr<TaskTrigger> task_trigger_;
};

}

// Fork off python shell for pause. Use portable fork and exec instead of the
// platform specific system() call.
static inline void TaskUtilPauseTest() {
    static bool d_pause_ = getenv("TASK_UTIL_PAUSE_AFTER_FAILURE") != NULL;
    if (!d_pause_)
        return;
    std::cout << "Test PAUSED. Exit (Ctrl-d) python shell to resume";
    pid_t pid;
    if (!(pid = fork()))
        execl("/usr/bin/python", "/usr/bin/python", NULL);
    int status;
    waitpid(pid, &status, 0);
}

// Get all possible sub-sets of a given set of elements
template <typename T>
static std::vector<std::vector<T> > GetSubSets(const std::vector<T> &vector) {
    std::vector<std::vector<T> > subsets;

    for (size_t i = 0; i < (1 << vector.size()); i++) {
        std::vector<T> subset;
        for (size_t j = 0; j < vector.size(); j++) {
            if (i & (1 << j))
                subset.push_back(vector[j]);
        }
        subsets.push_back(subset);
    }
    return subsets;
}

#define TASK_UTIL_WAIT_EQ_NO_MSG(expected, actual, wait, retry, msg)           \
do {                                                                           \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) == (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_TRUE((expected) == (actual));                                   \
        if((expected) != (actual))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_NE_NO_MSG(expected, actual, wait, retry, msg)           \
do {                                                                           \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) != (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_TRUE((expected) != (actual));                                   \
        if((expected) == (actual))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_MSG(cnt, expected, actual, wait, type, msg)             \
    do {                                                                       \
        ostream << __FILE__ << ":" <<  __FUNCTION__ << "():" << __LINE__;      \
        ostream << ": " << msg << ": Waiting for " << (actual) << type;        \
        ostream << (expected) << "\n";                                         \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();               \
        LOG4CPLUS_DEBUG(logger,  ostream.str());                               \
    } while (false)

#define TASK_UTIL_WAIT_EQ(expected, actual, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) == (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((expected) == (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, expected, actual, wait, " to become ", msg);    \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_EQ(expected, actual);                                           \
        if((expected) != (actual))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_NE(expected, actual, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((expected) != (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((expected) != (actual)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, expected, actual, wait, " to not remain ", msg);\
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_NE(expected, actual);                                           \
        if((expected) == (actual))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_GT(object1, object2, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((object1) > (object2)) {                                           \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((object1) > (object2)) {                                           \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, object2, object1, wait, " to be greater than ", \
                           msg);                                               \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_GT(object1, object2);                                           \
        if((object1) <= (object2))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_GE(object1, object2, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((object1) >= (object2)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((object1) >= (object2)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, object2, object1, wait,                         \
                           " to be greater than or equal to ", msg);           \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_GE(object1, object2);                                           \
        if((object1) < (object2))                                              \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_LT(object1, object2, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((object1) < (object2)) {                                           \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((object1) < (object2)) {                                           \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, object2, object1, wait, " to be less than ",    \
                           msg);                                               \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_LT(object1, object2);                                           \
        if((object1) >= (object2))                                             \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_WAIT_LE(object1, object2, wait, retry, msg)                  \
do {                                                                           \
    std::ostringstream ostream;                                                \
                                                                               \
    size_t _j = 0;                                                             \
    bool _satisfied = false;                                                   \
    for (size_t _i = 0; !retry || _i < retry; _i++) {                          \
        if ((object1) <= (object2)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        usleep(wait);                                                          \
        _j++;                                                                  \
        if ((_j * wait < 2000000UL)) continue;                                 \
        if ((object1) <= (object2)) {                                          \
            _satisfied = true;                                                 \
            break;                                                             \
        }                                                                      \
        _j = 0;                                                                \
        TASK_UTIL_WAIT_MSG(_i, object2, object1, wait,                         \
                           " to be less than or equal to ", msg);              \
    }                                                                          \
    if (!_satisfied) {                                                         \
        EXPECT_LE(object1, object2);                                           \
        if((object1) > (object2))                                              \
            TaskUtilPauseTest();                                               \
    }                                                                          \
} while (false)

#define TASK_UTIL_EXPECT_VECTOR_EQ(actual, expected)             \
    do {                                                         \
        TASK_UTIL_EXPECT_EQ((expected).size(), (actual).size()); \
        for (int i = 0; i < (int)((expected).size()); i++) {            \
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

#define TASK_UTIL_EXPECT_GT(object1, object2) \
    TASK_UTIL_WAIT_GT(object1, object2, task_util_wait_time(), \
                      task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_GT_MSG(object1, object2, msg) \
    TASK_UTIL_WAIT_GT(object1, object2, task_util_wait_time(), \
                      task_util_retry_count(), msg)
#define TASK_UTIL_EXPECT_GT_PARAMS1(object1, object2, wait_time, msg) \
    TASK_UTIL_WAIT_GT(object1, object2, wait_time, task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_GE(object1, object2) \
    TASK_UTIL_WAIT_GE(object1, object2, task_util_wait_time(), \
                      task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_GE_MSG(object1, object2, msg) \
    TASK_UTIL_WAIT_GE(object1, object2, task_util_wait_time(), \
                      task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_LT(expected, actual) \
    TASK_UTIL_WAIT_LT(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_LT_MSG(expected, actual, msg) \
    TASK_UTIL_WAIT_LT(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), msg)

#define TASK_UTIL_EXPECT_LE(expected, actual) \
    TASK_UTIL_WAIT_LE(expected, actual, task_util_wait_time(), \
                      task_util_retry_count(), "")
#define TASK_UTIL_EXPECT_LE_MSG(expected, actual, msg) \
    TASK_UTIL_WAIT_LE(expected, actual, task_util_wait_time(), \
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

#define TASK_UTIL_EXPECT_DEATH(statement, regex)                               \
    do {                                                                       \
        rlimit current_core_limit;                                             \
        getrlimit(RLIMIT_CORE, &current_core_limit);                           \
        rlimit new_core_limit;                                                 \
        new_core_limit.rlim_cur = 0;                                           \
        new_core_limit.rlim_max = 0;                                           \
        setrlimit(RLIMIT_CORE, &new_core_limit);                               \
        EXPECT_DEATH(statement, regex);                                        \
        setrlimit(RLIMIT_CORE, &current_core_limit);                           \
    } while (false)

#define TASK_UTIL_EXPECT_EXIT(statement, type, regex)                          \
    do {                                                                       \
        rlimit current_core_limit;                                             \
        getrlimit(RLIMIT_CORE, &current_core_limit);                           \
        rlimit new_core_limit;                                                 \
        new_core_limit.rlim_cur = 0;                                           \
        new_core_limit.rlim_max = 0;                                           \
        setrlimit(RLIMIT_CORE, &new_core_limit);                               \
        EXPECT_EXIT(statement, type, regex);                                   \
        setrlimit(RLIMIT_CORE, &current_core_limit);                           \
    } while (false)


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

// Wrapper macro to launch a command using fork and exec safely wrt io service.
#define TASK_UTIL_EXEC_AND_WAIT(evm, cmd)                                      \
do {                                                                           \
    (evm).io_service()->notify_fork(boost::asio::io_service::fork_prepare);    \
    pid_t pid;                                                                 \
    if (!(pid = fork())) {                                                     \
        (evm).io_service()->notify_fork(boost::asio::io_service::fork_child);  \
        execl(cmd, cmd, NULL);                                                 \
    }                                                                          \
    (evm).io_service()->notify_fork(boost::asio::io_service::fork_parent);     \
    int status;                                                                \
    waitpid(pid, &status, 0);                                                  \
} while (false)


#endif // __BASE__TASK_TEST_UTIL_H__
