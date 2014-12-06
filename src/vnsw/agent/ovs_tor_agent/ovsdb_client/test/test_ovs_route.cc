/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <io/test/event_manager_test.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "openstack/instance_service_server.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "test_ovs_agent_init.h"

using namespace pugi;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

void StartControlNodeMock() {
    thread1 = new ServerThread(&evm1);
    bgp_peer1 = new test::ControlNodeMock(&evm1, "127.0.0.1");

    Agent::GetInstance()->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
    Agent::GetInstance()->set_controller_ifmap_xmpp_port(bgp_peer1->GetServerPort(), 0);
    Agent::GetInstance()->set_dns_server("", 0);
    Agent::GetInstance()->set_dns_server_port(bgp_peer1->GetServerPort(), 0);
    thread1->Start();
}

void StopControlNodeMock() {
    Agent::GetInstance()->controller()->DisConnect();
    client->WaitForIdle();
    TcpServerManager::DeleteServer(Agent::GetInstance()->controller_ifmap_xmpp_client(0));

    bgp_peer1->Shutdown();
    client->WaitForIdle();
    delete bgp_peer1;

    evm1.Shutdown();
    thread1->Join();
    delete thread1;

    client->WaitForIdle();
}

class OvsRouteTest : public ::testing::Test {
protected:
    OvsRouteTest() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        peer_manager_ = init_->ovs_peer_manager();
        client->Reset();
    }

    virtual void TearDown() {
        //Clean up any pending routes in conrol node mock
        bgp_peer1->Clear();
        EXPECT_EQ(peer_manager_->Size(), 0);
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
};

TEST_F(OvsRouteTest, OvsPeer_1) {
    IpAddress server = Ip4Address::from_string("1.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);
    EXPECT_TRUE(peer->export_to_controller());
    peer_manager_->Free(peer);
}

TEST_F(OvsRouteTest, RouteTest_1) {
}

int main(int argc, char *argv[]) {
    //::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = OvsTestInit(init_file, ksync_init);
    StartControlNodeMock();
    int ret = RUN_ALL_TESTS();
    StopControlNodeMock();

    return ret;
}
