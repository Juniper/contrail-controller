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
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"

#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "ovs_tor_agent/ovsdb_client/physical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/logical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/physical_port_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/unicast_mac_local_ovsdb.h"
#include "test_ovs_agent_init.h"
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"
#include "test_xml_physical_device.h"
#include "test_xml_ovsdb.h"

#include "test_ovs_agent_util.h"

#include <ovsdb_types.h>

using namespace pugi;
using namespace OVSDB;

class OvsRouteTest : public ::testing::Test {
protected:
    OvsRouteTest() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        peer_manager_ = init_->ovs_peer_manager();
        client->Reset();

        WAIT_FOR(100, 10000,
                 (tcp_session_ = static_cast<OvsdbClientTcpSession *>
                  (init_->ovsdb_client()->NextSession(NULL))) != NULL);
        WAIT_FOR(100, 10000,
                 (tcp_session_->client_idl() != NULL));
        WAIT_FOR(100, 10000, (tcp_session_->status() == string("Established")));
        client->WaitForIdle();
        WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
        client->WaitForIdle();

        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
                   "client/test/xml/ucast-local-test-setup.xml");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
                   "client/test/xml/ucast-local-test-teardown.xml");
        client->WaitForIdle();
        //Clean up any pending routes in conrol node mock
        EXPECT_EQ(peer_manager_->Size(), 1U);
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpSession *tcp_session_;
};

TEST_F(OvsRouteTest, OvsPeer_1) {
    IpAddress server = Ip4Address::from_string("10.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);
    EXPECT_TRUE(peer->export_to_controller());
    peer_manager_->Free(peer);
    client->WaitForIdle();
}

TEST_F(OvsRouteTest, DelPeerBeforeProcessingDelReq) {
    IpAddress server = Ip4Address::from_string("10.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);

    VrfEntry *vrf = VrfGet("vrf1");
    VnEntry *vn = vrf->vn();
    std::string vn_name = vn->GetName();
    int vxlan_id = vn->GetVxLanId();
    boost::system::error_code err;
    Ip4Address dest = Ip4Address::from_string("10.1.1.1", err);
    Ip4Address server_ip = Ip4Address::from_string("0.0.0.0", err);
    MacAddress mac("00:00:00:00:01:01");
    peer->AddOvsRoute(vrf, vxlan_id, vn_name, mac, dest);
    client->WaitForIdle();
    WAIT_FOR(1000, 100,
             (EvpnRouteGet("vrf1", mac, server_ip, vxlan_id) != NULL));

    TestTaskHold *hold =
        new TestTaskHold(TaskScheduler::GetInstance()->GetTaskId("db::DBTableStop"), 0);

    peer->DeleteOvsRoute(vrf, vxlan_id, mac);
    peer_manager_->Free(peer);

    delete hold;
    client->WaitForIdle();
    WAIT_FOR(1000, 100,
             (EvpnRouteGet("vrf1", mac, server_ip, vxlan_id) == NULL));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    // override with true to initialize ovsdb server and client
    ksync_init = true;
    client = OvsTestInit(init_file, ksync_init);

    // create a task exclusion policy to hold db::DBTable task
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    TaskPolicy policy;
    int task_id = scheduler->GetTaskId("db::DBTable");
    policy.push_back(TaskExclusion(task_id));
    scheduler->SetPolicy(scheduler->GetTaskId("db::DBTableStop"), policy);
    client->WaitForIdle();

    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    int ret = RUN_ALL_TESTS();
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    return ret;
}
