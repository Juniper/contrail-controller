/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// All agents come back up but do not subscribe to any instance
TEST_P(GracefulRestartTest, GracefulRestart_Flap_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        n_flipped_agents_.push_back(GRTestParams(agent));
    }
    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        n_flipped_peers_.push_back(GRTestParams(peer));
    }
    GracefulRestartTestRun();
}
