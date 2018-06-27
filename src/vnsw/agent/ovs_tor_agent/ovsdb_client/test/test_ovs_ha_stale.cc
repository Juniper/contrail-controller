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
#include "ovs_tor_agent/ovsdb_client/multicast_mac_local_ovsdb.h"
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

AgentUtXmlTest xml_test("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
                        "client/test/xml/ha-stale-test.xml");

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

class HaStaleRouteTest : public ::testing::Test {
protected:
    HaStaleRouteTest() {
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

    MulticastMacLocalEntry *FindMcastLocal(const string &logical_switch) {
        MulticastMacLocalOvsdb *table =
            tcp_session_->client_idl()->multicast_mac_local_ovsdb();
        MulticastMacLocalEntry key(table, logical_switch);
        MulticastMacLocalEntry *entry =
            static_cast<MulticastMacLocalEntry *> (table->Find(&key));
        return entry;
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        agent_->set_tor_agent_enabled(true);
        init_ = static_cast<TestOvsAgentInit *>(client->agent_init());
        tcp_server_ =
            static_cast<OvsdbClientTcpTest *>(init_->ovsdb_client());
        peer_manager_ = init_->ovs_peer_manager();
        WAIT_FOR(100, 10000,
                 (tcp_session_ = static_cast<OvsdbClientTcpSession *>
                  (tcp_server_->NextSession(NULL))) != NULL);
        WAIT_FOR(100, 10000,
                 (tcp_session_->client_idl() != NULL));
        WAIT_FOR(100, 10000, (tcp_session_->status() == string("Established")));
        client->WaitForIdle();
        WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
        client->WaitForIdle();

        // set current session in test context
        OvsdbTestSetSessionContext(tcp_session_);
        EXPECT_TRUE(xml_test.Run("setup"));
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
        EXPECT_TRUE(xml_test.Run("teardown"));
        client->WaitForIdle();
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpTest *tcp_server_;
    OvsdbClientTcpSession *tcp_session_;
    bool teardown_done_in_test_;
};

TEST_F(HaStaleRouteTest, ConnectionCloseWhileUnicastLocalPresent) {
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
                                              "111.111.111.111")));
        // Wait for entry to add
        WAIT_FOR(100, 10000,
                 (NULL != FindUcastLocal(entry->name(), "00:00:00:00:01:01")));
        client->WaitForIdle();

        // execute OvsdbHaStaleDevVnExportReq request
        OvsdbHaStaleDevVnExportReq *dev_vn_req =
            new OvsdbHaStaleDevVnExportReq();
        dev_vn_req->set_dev_name("test-router");
        dev_vn_req->HandleRequest();
        client->WaitForIdle();
        dev_vn_req->Release();

        // execute OvsdbHaStaleL2RouteExportReq request
        OvsdbHaStaleL2RouteExportReq *l2_route_req =
            new OvsdbHaStaleL2RouteExportReq();
        l2_route_req->set_dev_name("test-router");
        l2_route_req->set_vn_uuid(entry->name());
        l2_route_req->HandleRequest();
        client->WaitForIdle();
        l2_route_req->Release();

        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after session close
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
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
    client->WaitForIdle();
}

TEST_F(HaStaleRouteTest, ConnectionCloseWhileMulticastLocalPresent) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    MacAddress mac("ff:ff:ff:ff:ff:ff");

    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(100, 10000,
                 (true == add_mcast_mac_local(entry->name(),
                                              "unknown-dst",
                                              "111.111.111.111")));
        client->WaitForIdle();
        // Wait for entry to add
        MulticastMacLocalEntry *mcast_entry;
        WAIT_FOR(1000, 10000,
                 (NULL != (mcast_entry = FindMcastLocal(entry->name())) &&
                  mcast_entry->IsResolved()));
        client->WaitForIdle();

        // execute OvsdbHaStaleDevVnExportReq request
        OvsdbHaStaleDevVnExportReq *dev_vn_req =
            new OvsdbHaStaleDevVnExportReq();
        dev_vn_req->set_dev_name("test-router");
        dev_vn_req->HandleRequest();
        client->WaitForIdle();
        dev_vn_req->Release();

        // execute OvsdbHaStaleL2RouteExportReq request
        OvsdbHaStaleL2RouteExportReq *l2_route_req =
            new OvsdbHaStaleL2RouteExportReq();
        l2_route_req->set_dev_name("test-router");
        l2_route_req->set_vn_uuid(entry->name());
        l2_route_req->HandleRequest();
        client->WaitForIdle();
        l2_route_req->Release();

        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            Ip4Address tor_ip = Ip4Address::from_string("111.111.111.111");
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, tor_ip, 32, 100);
            // stale route should be present even after session close
            EXPECT_TRUE(evpn_rt != NULL);
            EXPECT_TRUE(evpn_rt->GetActivePath()->path_preference().preference()
                        == PathPreference::HA_STALE);
        }

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
    client->WaitForIdle();

    table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key1(table, UuidToString(MakeUuid(1)));
    entry = static_cast<LogicalSwitchEntry *> (table->Find(&key1));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == del_mcast_mac_local(entry->name(), "unknown-dst",
                                              "111.111.111.111")));
        // Wait for entry to del
        WAIT_FOR(100, 10000,
                 (NULL == FindUcastLocal(entry->name(), "ff:ff:ff:ff:ff:ff")));
    }
    client->WaitForIdle();
}

TEST_F(HaStaleRouteTest, ToRRouteAddDelonBackupToRAgent) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        // after connection close add route to ovsdb-server
        // and simulate route addition via BGP to backup ToR
        WAIT_FOR(10, 10000,
                 (true == add_ucast_mac_local(UuidToString(MakeUuid(1)),
                                              "00:00:00:00:01:01",
                                              "111.111.111.111")));

        EXPECT_TRUE(xml_test.Run("tor-route-add"));
        client->WaitForIdle();
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after remote route delete
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
    client->WaitForIdle();

    table = tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key1(table, UuidToString(MakeUuid(1)));
    entry = static_cast<LogicalSwitchEntry *> (table->Find(&key1));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(10, 10000,
                 (true == del_ucast_mac_local(entry->name(),
                                              "00:00:00:00:01:01")));
    }
}

TEST_F(HaStaleRouteTest, HaStaleRouteDeleteOnVxLanIdChange) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        // after connection close simulate route addition via BGP to backup ToR
        EXPECT_TRUE(xml_test.Run("tor-route-add"));
        client->WaitForIdle();
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after remote route delete
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // trigger VxLanID change
        EXPECT_TRUE(xml_test.Run("vxlan-id-change"));
        client->WaitForIdle();
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            // stale route should be deleted on VxLan Id Change
            WAIT_FOR(1000, 1000,
                     (NULL == rt_table->FindRoute(mac, default_ip, 32, 100)));
        }

        // Revert VxLanID change
        EXPECT_TRUE(xml_test.Run("vxlan-id-revert"));
        client->WaitForIdle();

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
    client->WaitForIdle();
}

TEST_F(HaStaleRouteTest, HaStaleRouteDeleteToRIPChange) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        // after connection close simulate route addition via BGP to backup ToR
        EXPECT_TRUE(xml_test.Run("tor-route-add"));
        client->WaitForIdle();
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after remote route delete
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // trigger ToR IP change
        EXPECT_TRUE(xml_test.Run("tor-ip-change"));
        client->WaitForIdle();
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            // stale route should be deleted on ToR IP Change
            WAIT_FOR(1000, 1000,
                     (NULL == rt_table->FindRoute(mac, default_ip, 32, 100)));
        }

        // simulate route addition via BGP to backup ToR
        EXPECT_TRUE(xml_test.Run("tor-route-add"));
        client->WaitForIdle();

        // Revert ToR IP change
        EXPECT_TRUE(xml_test.Run("tor-ip-revert"));
        client->WaitForIdle();

        // Reverting ToR IP change should create a stale entry
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after remote route delete
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
    client->WaitForIdle();
}

TEST_F(HaStaleRouteTest, HaStaleRouteDeleteMACMove) {
    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        // Take reference to idl so that session object itself is not deleted.
        OvsdbClientIdlPtr tcp_idl = tcp_session_->client_idl();
        // disable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(false);
        tcp_session_->TriggerClose();
        client->WaitForIdle();

        // validate refcount to be 2 one from session and one locally held
        // to validate session closure, when we release refcount
        WAIT_FOR(1000, 1000, (2 == tcp_idl->refcount()));
        tcp_idl = NULL;
        client->WaitForIdle();

        // after connection close simulate route addition via BGP to backup ToR
        EXPECT_TRUE(xml_test.Run("tor-route-add"));
        client->WaitForIdle();
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        VrfTable *vrf_table = agent_->vrf_table();
        std::string vrf_name("vrf1");
        EvpnAgentRouteTable *rt_table = static_cast<EvpnAgentRouteTable*>
            (vrf_table->GetEvpnRouteTable(vrf_name));
        EXPECT_TRUE(rt_table != NULL);
        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            EvpnRouteEntry *evpn_rt = rt_table->FindRoute(mac, default_ip, 32, 100);
            // stale route should be present even after remote route delete
            EXPECT_TRUE(evpn_rt != NULL);
        }

        // trigger route add with different destination
        EXPECT_TRUE(xml_test.Run("tor-route-add-new-dest"));
        client->WaitForIdle();
        // Mac move to different ToR should have removed stale entry
        EXPECT_TRUE(xml_test.Run("tor-route-del"));
        client->WaitForIdle();

        if (rt_table != NULL) {
            MacAddress mac("00:00:00:00:01:01");
            Ip4Address default_ip;
            // stale route should be deleted on MAC Move
            WAIT_FOR(1000, 1000,
                     (NULL == rt_table->FindRoute(mac, default_ip, 32, 100)));
        }

        // enable reconnect to Ovsdb Server
        tcp_server_->set_enable_connect(true);
        client->WaitForIdle();
    }

    WAIT_FOR(100, 10000,
             (tcp_session_ = static_cast<OvsdbClientTcpSession *>
              (init_->ovsdb_client()->NextSession(NULL))) != NULL &&
              tcp_session_->client_idl() != NULL);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (!tcp_session_->client_idl()->IsMonitorInProcess()));
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
    bool xml_load = LoadXml(xml_test);
    EXPECT_TRUE(xml_load);
    int ret = 0;
    if (xml_load) {
        ret = RUN_ALL_TESTS();
    }
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    return ret;
}
