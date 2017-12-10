/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// Some agents come back up but do not subscribe to any instance
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        n_flipped_agents_.push_back(GRTestParams(agent));
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        n_flipped_peers_.push_back(GRTestParams(peer));
    }
    GracefulRestartTestRun();
}
