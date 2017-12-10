/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// Some agents come back up and subscribe to all instances but sends no routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_3_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // None of the flipped agents sends EoR.
        n_flipped_agents_[i].send_eor = false;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // None of the flipped peers sends EoR.
        n_flipped_peers_[i].send_eor = false;
    }
    GracefulRestartTestRun();
}
