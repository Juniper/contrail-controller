/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/graceful_restart_test.cc"

// All agents come back up and subscribe to all instances and sends all routes
// Agent session tcp down event is not detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_5) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes,
                                                    TcpSession::CLOSE));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes,
                                                TcpSession::CLOSE));
    }
    GracefulRestartTestRun();
}
