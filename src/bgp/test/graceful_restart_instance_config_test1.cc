/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// Some routing instances are first deleted. Subscribed agents remain up and
// running.. This is the common case which happens most of the time during
// normal functioning of the software.
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);
    GracefulRestartTestRun();
}
