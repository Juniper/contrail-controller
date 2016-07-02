/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
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
using namespace boost::uuids;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

class UnicastLocalRouteTest : public ::testing::Test {
protected:
    UnicastLocalRouteTest() {
    }

    UnicastMacLocalEntry *FindUcastLocal(const string &logical_switch,
                                         const string &mac) {
        UnicastMacLocalOvsdb *table =
            tcp_session_->client_idl()->unicast_mac_local_ovsdb();
        UnicastMacLocalEntry key(table, logical_switch, mac);
        UnicastMacLocalEntry *entry =
            static_cast<UnicastMacLocalEntry *> (table->Find(&key));
        return entry;
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        peer_manager_ = init_->ovs_peer_manager();
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
        teardown_done_in_test_ = false;
        client->WaitForIdle();
    }

    virtual void TearDown() {
        if (teardown_done_in_test_) {
            client->WaitForIdle();
            teardown_done_in_test_ = false;
            return;
        }
        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
                   "client/test/xml/ucast-local-test-teardown.xml");
        client->WaitForIdle();
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpSession *tcp_session_;
    bool teardown_done_in_test_;
};

TEST_F(UnicastLocalRouteTest, UnicastLocalBasic) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == add_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01",
                                              "11.11.11.11")));
        // Wait for entry to add
        WAIT_FOR(100, 10000,
                 (NULL != FindUcastLocal(entry->name(), "00:00:00:00:01:01")));

        OvsdbUnicastMacLocalReq *req = new OvsdbUnicastMacLocalReq();
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();

        WAIT_FOR(10, 10000,
                 (true == del_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01")));
        // Wait for entry to del
        WAIT_FOR(100, 10000,
                 (NULL == FindUcastLocal(entry->name(), "00:00:00:00:01:01")));
    }
}

TEST_F(UnicastLocalRouteTest, UnicastLocalDelayLogicalSwitchDelete) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == add_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01",
                                              "11.11.11.11")));
        // Wait for entry to add
        WAIT_FOR(100, 10000,
                 (NULL != FindUcastLocal(entry->name(), "00:00:00:00:01:01")));

        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/"
                   "test/xml/ucast-local-test-ls-pending-teardown.xml");
        client->WaitForIdle();
        teardown_done_in_test_ = true;

        WAIT_FOR(1000, 1000,
                 (NULL != (entry = static_cast<LogicalSwitchEntry *>
                           (table->Find(&key)))));
        // entry should be waiting for Local MAC ref to go
        WAIT_FOR(1000, 1000, (true == entry->IsLocalMacsRef()));

        WAIT_FOR(10, 10000,
                 (true == del_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01")));
        // Wait for entry to del
        WAIT_FOR(1000, 1000, (NULL == table->Find(&key)));
    }
}

TEST_F(UnicastLocalRouteTest, UnicastLocalDelayLogicalSwitchDeleteAndRenew) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == add_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01",
                                              "11.11.11.11")));
        // Wait for entry to add
        WAIT_FOR(100, 10000,
                 (NULL != FindUcastLocal(entry->name(), "00:00:00:00:01:01")));

        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/"
                   "test/xml/ucast-local-test-ls-pending-teardown.xml");
        client->WaitForIdle();
        teardown_done_in_test_ = true;

        WAIT_FOR(1000, 1000,
                 (NULL != (entry = static_cast<LogicalSwitchEntry *>
                           (table->Find(&key)))));
        // entry should be waiting for Local MAC ref to go
        WAIT_FOR(1000, 1000, (true == entry->IsLocalMacsRef()));

        // readd setup config
        SetUp();

        WAIT_FOR(1000, 1000, (NULL != table->Find(&key)));

        WAIT_FOR(10, 10000,
                 (true == del_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01")));

        // entry should be active without Local MAC ref
        WAIT_FOR(1000, 1000, (false == entry->IsLocalMacsRef()));
    }
}

TEST_F(UnicastLocalRouteTest, ConnectionCloseWhileUnicastLocalPresent) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == add_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01",
                                              "11.11.11.11")));
        // Wait for entry to add
        WAIT_FOR(100, 10000,
                 (NULL != FindUcastLocal(entry->name(), "00:00:00:00:01:01")));

        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL);
    client->WaitForIdle();

    table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key1(table, UuidToString(MakeUuid(1)));
    entry = static_cast<LogicalSwitchEntry *> (table->Find(&key1));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == del_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01")));
        // Wait for entry to del
        WAIT_FOR(100, 10000,
                 (NULL == FindUcastLocal(entry->name(), "00:00:00:00:01:01")));
    }
}

TEST_F(UnicastLocalRouteTest, tunnel_nh_1) {
    IpAddress server = Ip4Address::from_string("1.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);
    EXPECT_TRUE(peer->export_to_controller());

    MacAddress mac("00:00:00:00:00:01");
    Ip4Address tor_ip = Ip4Address::from_string("2.2.2.1");
    Ip4Address server_ip = Ip4Address::from_string("0.0.0.0");
    AddVrf("vrf1", 1);
    client->WaitForIdle();

    VrfEntry *vrf = VrfGet("vrf1");
    WAIT_FOR(100, 100, (vrf->GetBridgeRouteTable() != NULL));
    peer->AddOvsRoute(vrf, 100, "dummy", mac, tor_ip);
    WAIT_FOR(1000, 100, (EvpnRouteGet("vrf1", mac, server_ip, 100) != NULL));
    client->WaitForIdle();

    EvpnRouteEntry *rt = EvpnRouteGet("vrf1", mac, server_ip, 100);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);

    AddEncapList("MPLSoUDP", "vxlan", NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (EvpnRouteGet("vrf1", mac, server_ip, 100) != NULL));

    rt = EvpnRouteGet("vrf1", mac, server_ip, 100);
    path = rt->GetActivePath();
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    nh = dynamic_cast<const TunnelNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);

    peer->DeleteOvsRoute(vrf, 100, mac);
    client->WaitForIdle();
    // Change tunnel-type order
    peer_manager_->Free(peer);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    // override with true to initialize ovsdb server and client
    ksync_init = true;
    client = OvsTestInit(init_file, ksync_init);

    // override signal handler to default for SIGCHLD, for system() api
    // to work and return exec status appropriately
    signal(SIGCHLD, SIG_DFL);

    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    int ret = RUN_ALL_TESTS();
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    return ret;
}
