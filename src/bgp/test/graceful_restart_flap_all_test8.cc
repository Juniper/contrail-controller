/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// None of the agents goes down or flip
TEST_P(GracefulRestartTest, GracefulRestart_Down_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();
    GracefulRestartTestRun();
}
