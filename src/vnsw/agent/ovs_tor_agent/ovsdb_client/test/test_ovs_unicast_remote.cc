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
#include "oper/physical_device.h"
#include "oper/physical_device_vn.h"
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

extern "C" {
#include <ovsdb_wrapper.h>
};

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "ovs_tor_agent/ovsdb_client/physical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/logical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/physical_port_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/vrf_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/unicast_mac_remote_ovsdb.h"
#include "test_ovs_agent_init.h"
#include "test-xml/test_xml.h"
#include "test-xml/test_xml_oper.h"
#include "test_xml_physical_device.h"
#include "test_xml_ovsdb.h"

#include <ovsdb_types.h>

using namespace pugi;
using namespace OVSDB;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

class UnicastRemoteTest : public ::testing::Test {
protected:
    UnicastRemoteTest() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        peer_manager_ = init_->ovs_peer_manager();
        WAIT_FOR(100, 10000,
                 ((tcp_session_ = static_cast<OvsdbClientTcpSession *>
                  (init_->ovsdb_client()->NextSession(NULL))) != NULL)
                  && (tcp_session_->client_idl() != NULL) &&
                  (tcp_session_->status() == string("Established")));
        client->WaitForIdle();
        WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
        client->WaitForIdle();
    }

    virtual void TearDown() {
    }

    void LoadAndRun(const std::string &file_name) {
        AgentUtXmlTest test(file_name);
        OvsdbTestSetSessionContext(tcp_session_);
        AgentUtXmlOperInit(&test);
        AgentUtXmlPhysicalDeviceInit(&test);
        AgentUtXmlOvsdbInit(&test);
        if (test.Load() == true) {
            test.ReadXml();
            string str;
            test.ToString(&str);
            cout << str << endl;
            test.Run();
        }
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpSession *tcp_session_;
};

TEST_F(UnicastRemoteTest, UnicastRemoteEmptyVrfAddDel) {
    LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/"
            "test/xml/unicast-remote-empty-vrf.xml");
}

TEST_F(UnicastRemoteTest, TunnelIpChange) {
    // Add VN
    VnAddReq(2, "test-vn1");
    // Add VRF
    agent_->vrf_table()->CreateVrfReq("test-vrf1", MakeUuid(2));
    // Add Physical Device
    AddPhysicalDevice("test-router", 1);
    client->WaitForIdle();

    // Add DevVN
    AddPhysicalDeviceVn(agent_, 1, 2, true);
    client->WaitForIdle();

    MacAddress mac("00:00:00:01:00:01");
    BridgeTunnelRouteAdd(bgp_peer_, std::string("test-vrf1"),
                         (1 << TunnelType::VXLAN), "10.0.0.1",
                         101, mac, "0.0.0.0", 32);
    client->WaitForIdle();

    VrfOvsdbObject *table = tcp_session_->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(MakeUuid(2)));
    VrfOvsdbEntry *vrf_entry;
    WAIT_FOR(100, 10000,
             (vrf_entry =
              static_cast<VrfOvsdbEntry *>(table->Find(&vrf_key))) != NULL);
    KSyncEntry::KSyncEntryPtr ucast_mac = NULL;
    if (vrf_entry != NULL) {
        UnicastMacRemoteTable *u_table = vrf_entry->route_table();
        UnicastMacRemoteEntry key(u_table, "00:00:00:01:00:01");
        UnicastMacRemoteEntry *entry;
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
        // take reference of ucast mac to hold the oper entry as well
        ucast_mac = entry;

        OvsdbVrfReq *vrf_req = new OvsdbVrfReq();
        vrf_req->HandleRequest();
        client->WaitForIdle();
        vrf_req->Release();

        OvsdbUnicastMacRemoteReq *ucast_req = new OvsdbUnicastMacRemoteReq();
        ucast_req->set_logical_switch(vrf_entry->logical_switch_name());
        ucast_req->HandleRequest();
        client->WaitForIdle();
        ucast_req->Release();

    }

    // Delete route
    Ip4Address zero_ip;
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac, zero_ip, 32, 0, NULL);
    client->WaitForIdle();

    // Add Route with new tunnel dest
    BridgeTunnelRouteAdd(bgp_peer_, std::string("test-vrf1"),
                         (1 << TunnelType::VXLAN), "10.0.0.2",
                         101, mac, "0.0.0.0", 32);
    client->WaitForIdle();

    if (vrf_entry != NULL) {
        UnicastMacRemoteTable *u_table = vrf_entry->route_table();
        UnicastMacRemoteEntry key(u_table, "00:00:00:01:00:01");
        UnicastMacRemoteEntry *entry;
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
        struct ovsdb_idl_row *ovs_entry = entry->ovs_entry();
        EXPECT_TRUE(ovs_entry != NULL &&
                    string("10.0.0.2") ==
                    string(ovsdb_wrapper_ucast_mac_remote_dst_ip(ovs_entry)));
    }

    // Delete route
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac, zero_ip, 32, 0, NULL);
    client->WaitForIdle();

    // Add new route for same mac but with receive route to have empty dest ip
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(bgp_peer_,
                                   std::string("test-vrf1"), mac,
                                   zero_ip, 32, 0));
    req.data.reset(new L2ReceiveRoute(std::string("test-vn1"), 0, 101,
                                      PathPreference(), 0));
    if (agent_->fabric_evpn_table()) {
        agent_->fabric_evpn_table()->Enqueue(&req);
    }
    client->WaitForIdle();

    if (vrf_entry != NULL) {
        UnicastMacRemoteTable *u_table = vrf_entry->route_table();
        UnicastMacRemoteEntry key(u_table, "00:00:00:01:00:01");
        UnicastMacRemoteEntry *entry;
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
        // verify entry deleted from ovsdb
        WAIT_FOR(100, 10000, NULL == entry->ovs_entry());
    }

    // release reference of ucast mac
    ucast_mac = NULL;
    // Delete route
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac, zero_ip, 32, 0, NULL);
    client->WaitForIdle();

    // Delete DevVN
    DelPhysicalDeviceVn(agent_, 1, 2, false);
    client->WaitForIdle();

    DeletePhysicalDevice("test-router");
    client->WaitForIdle();

    agent_->vrf_table()->DeleteVrfReq("test-vrf1");
    VnDelReq(2);
    client->WaitForIdle();

    // Validate Logical switch deleted
    LogicalSwitchTable *l_table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry l_key(table, UuidToString(MakeUuid(2)));
    WAIT_FOR(100, 10000, (l_table->Find(&l_key) == NULL));
}

TEST_F(UnicastRemoteTest, LogicalSwitchDeleteOnRefRelease) {
    // Add VN
    VnAddReq(2, "test-vn1");
    // Add VRF
    agent_->vrf_table()->CreateVrfReq("test-vrf1", MakeUuid(2));
    // Add Physical Device
    AddPhysicalDevice("test-router", 1);
    client->WaitForIdle();

    // Add DevVN
    AddPhysicalDeviceVn(agent_, 1, 2, true);
    client->WaitForIdle();

    MacAddress mac("00:00:00:01:00:01");
    BridgeTunnelRouteAdd(bgp_peer_, std::string("test-vrf1"),
                         (1 << TunnelType::VXLAN), "10.0.0.1",
                         101, mac, "0.0.0.0", 32);
    client->WaitForIdle();

    VrfOvsdbObject *table = tcp_session_->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(MakeUuid(2)));
    VrfOvsdbEntry *vrf_entry;
    WAIT_FOR(100, 10000,
             (vrf_entry =
              static_cast<VrfOvsdbEntry *>(table->Find(&vrf_key))) != NULL);
    if (vrf_entry != NULL) {
        UnicastMacRemoteTable *u_table = vrf_entry->route_table();
        UnicastMacRemoteEntry key(u_table, "00:00:00:01:00:01");
        UnicastMacRemoteEntry *entry;
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
    }

    // Delete DevVN first to trigger delete for logical switch
    // which will go to del defer due to reference and wait for
    // delete of unicast remote mac entry
    DelPhysicalDeviceVn(agent_, 1, 2, false);
    client->WaitForIdle();

    DeletePhysicalDevice("test-router");
    client->WaitForIdle();

    // Delete route
    Ip4Address zero_ip;
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac, zero_ip, 32, 0, NULL);
    client->WaitForIdle();
    agent_->vrf_table()->DeleteVrfReq("test-vrf1");
    VnDelReq(2);
    client->WaitForIdle();

    // Validate Logical switch deleted
    LogicalSwitchTable *l_table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry l_key(table, UuidToString(MakeUuid(2)));
    WAIT_FOR(100, 10000, (l_table->Find(&l_key) == NULL));
}

TEST_F(UnicastRemoteTest, VrfNotifyWithIdlDeleted) {
    Agent *agent = Agent::GetInstance();
    TestTaskHold *hold =
        new TestTaskHold(TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0);
    tcp_session_->TriggerClose();
    VnAddReq(1, "test-vn1");
    agent->vrf_table()->CreateVrfReq("test-vrf1", MakeUuid(1));
    delete hold;
    client->WaitForIdle();
    agent->vrf_table()->DeleteVrfReq("test-vrf1");
    VnDelReq(1);
    client->WaitForIdle();
}

TEST_F(UnicastRemoteTest, PhysicalLocatorCreateWait) {
    // Add VN
    VnAddReq(2, "test-vn1");
    // Add VRF
    agent_->vrf_table()->CreateVrfReq("test-vrf1", MakeUuid(2));
    // Add Physical Device
    AddPhysicalDevice("test-router", 1);
    client->WaitForIdle();

    // Add DevVN
    AddPhysicalDeviceVn(agent_, 1, 2, true);
    client->WaitForIdle();

    uint64_t txn_failures = tcp_session_->client_idl()->stats().txn_failed;
    // hold Db task to execute both route add requests in single db task run
    TestTaskHold *hold =
        new TestTaskHold(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    MacAddress mac1("00:00:00:01:00:01");
    MacAddress mac2("00:00:00:01:00:02");
    BridgeTunnelRouteAdd(bgp_peer_, std::string("test-vrf1"),
                         (1 << TunnelType::VXLAN), "10.0.1.1",
                         101, mac1, "0.0.0.0", 32);
    BridgeTunnelRouteAdd(bgp_peer_, std::string("test-vrf1"),
                         (1 << TunnelType::VXLAN), "10.0.1.1",
                         101, mac2, "0.0.0.0", 32);
    delete hold;
    hold = NULL;
    client->WaitForIdle();

    VrfOvsdbObject *table = tcp_session_->client_idl()->vrf_ovsdb();
    VrfOvsdbEntry vrf_key(table, UuidToString(MakeUuid(2)));
    VrfOvsdbEntry *vrf_entry;
    WAIT_FOR(100, 10000,
             (vrf_entry =
              static_cast<VrfOvsdbEntry *>(table->Find(&vrf_key))) != NULL);
    if (vrf_entry != NULL) {
        UnicastMacRemoteTable *u_table = vrf_entry->route_table();
        UnicastMacRemoteEntry key(u_table, "00:00:00:01:00:01");
        UnicastMacRemoteEntry *entry;
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
        UnicastMacRemoteEntry key1(u_table, "00:00:00:01:00:02");
        // Validate mac programmed in OVSDB
        WAIT_FOR(100, 10000,
                 ((entry =
                  static_cast<UnicastMacRemoteEntry *>(u_table->Find(&key1))) != NULL
                  && entry->GetState() == KSyncEntry::IN_SYNC));
    }

    // there should not be any more transaction failures
    EXPECT_EQ(txn_failures, tcp_session_->client_idl()->stats().txn_failed);
    // Delete route
    Ip4Address zero_ip;
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac1, zero_ip, 32, 0, NULL);
    EvpnAgentRouteTable::DeleteReq(bgp_peer_,
                                   std::string("test-vrf1"),
                                   mac2, zero_ip, 32, 0, NULL);
    client->WaitForIdle();

    // Delete DevVN
    DelPhysicalDeviceVn(agent_, 1, 2, false);
    client->WaitForIdle();

    DeletePhysicalDevice("test-router");
    client->WaitForIdle();

    agent_->vrf_table()->DeleteVrfReq("test-vrf1");
    VnDelReq(2);
    client->WaitForIdle();

    // Validate Logical switch deleted
    LogicalSwitchTable *l_table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry l_key(table, UuidToString(MakeUuid(2)));
    WAIT_FOR(100, 10000, (l_table->Find(&l_key) == NULL));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    // override with true to initialize ovsdb server and client
    ksync_init = true;
    client = OvsTestInit(init_file, ksync_init);
    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    int ret = RUN_ALL_TESTS();
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    return ret;
}
