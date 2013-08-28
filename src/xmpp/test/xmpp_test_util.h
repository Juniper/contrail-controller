/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#ifndef __XMPP_TEST_UTIL_H__
#define __XMPP_TEST_UTIL_H__

// This macro checks for 'Cond' to be true for upto 'Times' retries while
// waiting for 'Unit' useconds between each try before giving up.
// Flag = 0 means failure is silently ignored.
#define WAIT_FOR_(Times, Unit, Cond, flag, __FILE__, __LINE__)             \
    do {                                                                   \
        bool val = false;                                                  \
        for (int i = 0; i < (Times); i++) {                                \
            val = (Cond);                                                  \
            if (val) break;                                                \
            usleep(Unit);                                                  \
        }                                                                  \
        if (flag != 0 && (Cond) == false)                                  \
            LOG(DEBUG, "Test failed at " << __FILE__ << ": " << __LINE__); \
        if (flag != 0)                                                     \
            EXPECT_TRUE(Cond);                                             \
    } while (0)

#define WAIT_FOR(times, unit, cond) \
    WAIT_FOR_(times, unit, cond, 1,  __FILE__, __LINE__)

#define CHECK_ONCE(unit, cond) \
    WAIT_FOR_(1, unit, (cond), 0, __FILE__, __LINE__)

#endif // __XMPP_TEST_UTIL_H__
