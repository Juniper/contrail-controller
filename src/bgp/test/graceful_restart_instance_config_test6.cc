/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). Some of the other agents go down and then come
// back up advertising some of the routes again (not all). During this GR,
// additional instances are deleted
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_6) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }

        // agents from 2nd quarter flip with gr
        test::NetworkAgentMock *agent = xmpp_agents_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }

        // peers from 2nd quarter flip with gr
        BgpPeerTest *peer = bgp_peers_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}
