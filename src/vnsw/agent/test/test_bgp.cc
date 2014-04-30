/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include <controller/controller_peer.h>
#include <controller/controller_export.h> 
#include <controller/controller_vrf_export.h>

//  Test vnsw xmpp(bgp-peer) after launching control-node
//
//  1) Launch control-node  with following option
//      control-node --bgp-port xx --http-server-port yy --config-file=bgp_config.xml
//
//  <?xml version="1.0" encoding="utf-8"?>
//  <config>
//    <routing-instance name="vrf1">
//         <vrf-target>target:1:3</vrf-target>
//    </routing-instance>
//    <routing-instance name="vrf2">
//         <vrf-target>target:1:3</vrf-target>
//    </routing-instance>
// </config>
//

void RouterIdDepInit(Agent *agent) {
}

class VmPortTest : public ::testing::Test {
};

TEST_F(VmPortTest, XmppConnection) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    // create vrf/vn before xmpp channel Establishment
    client->Reset();
    VrfAddReq("vrf2");
    VnAddReq(2, "vn2", 0, "vrf2");

    AgentXmppChannel *peer = Agent::GetInstance()->GetAgentXmppChannel(0);
    WAIT_FOR(1000, 1000, (peer->GetXmppChannel()->GetPeerState() == xmps::READY));
    ASSERT_TRUE(peer->GetXmppChannel()->GetPeerState() == xmps::READY);

    //expect 1 cfg-subscribe + 2 subscribe messages sent out
    WAIT_FOR(1000, 1000, (AgentStats::GetInstance()->xmpp_out_msgs(0) == 3));
    // create vm-port, local vm-route
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //expect subscribe + route message sent out
    WAIT_FOR(1000, 1000, (AgentStats::GetInstance()->xmpp_out_msgs(0) == 6));

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", addr, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", addr, 32);
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");

    WAIT_FOR(2000, 1000, (AgentStats::GetInstance()->xmpp_in_msgs(0) == 2));
    ASSERT_TRUE(AgentStats::GetInstance()->xmpp_in_msgs(0) == 2);
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf2", addr, 32));
    Inet4UnicastRouteEntry *rt2 = RouteGet("vrf2", addr, 32);
    WAIT_FOR(100, 10000, (rt2->GetActivePath() != NULL));
    WAIT_FOR(100, 10000, rt2->dest_vn_name().size() > 0);
    EXPECT_STREQ(rt2->dest_vn_name().c_str(), "vn1");

    client->WaitForIdle();
    // delete vm-port, delete local-route
    IntfCfgDel(input, 0);
    VmDelReq(1);
    client->WaitForIdle();

    //expect delete message sent out
    WAIT_FOR(1000, 1000, (AgentStats::GetInstance()->xmpp_out_msgs(0) == 8));

    //expect route delete messages
    WAIT_FOR(2000, 1000, (AgentStats::GetInstance()->xmpp_in_msgs(0) == 4));
    WAIT_FOR(100, 10000, (RouteFind("vrf1", addr, 32) == false));
    WAIT_FOR(100, 10000, (RouteFind("vrf2", addr, 32) == false));
    //confirm vm-port is deleted
    EXPECT_FALSE(VmPortFind(input, 0));
}


int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->SetXmppServer("127.0.0.1", 0);

    Agent::GetInstance()->controller()->Connect();


    return RUN_ALL_TESTS();
}
