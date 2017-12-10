/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// Some agents go down permanently and some flip (which sends no routes)
TEST_P(GracefulRestartTest, GracefulRestart_Down_4) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size(); i++) {
        if (i <= xmpp_agents_.size()/2)
            n_down_from_agents_.push_back(xmpp_agents_[i]);
        else
            n_flipped_agents_.push_back(GRTestParams(xmpp_agents_[i]));
    }

    for (size_t i = 0; i < bgp_peers_.size(); i++) {
        if (i <= bgp_peers_.size()/2)
            n_down_from_peers_.push_back(bgp_peers_[i]);
        else
            n_flipped_peers_.push_back(GRTestParams(bgp_peers_[i]));
    }
    GracefulRestartTestRun();
}
