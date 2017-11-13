/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// All agents go down permanently
TEST_P(GracefulRestartTest, GracefulRestart_Down_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    n_down_from_agents_ = xmpp_agents_;
    n_down_from_peers_ = bgp_peers_;
    GracefulRestartTestRun();
}
