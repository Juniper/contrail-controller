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

#include "ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h"
#include "ovs_tor_agent/ovsdb_client/physical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/logical_switch_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/physical_port_ovsdb.h"
#include "ovs_tor_agent/ovsdb_client/multicast_mac_local_ovsdb.h"
#include "test_ovs_agent_init.h"
#include "test_ovs_agent_util.h"
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

class MulticastLocalRouteTest : public ::testing::Test {
protected:
    MulticastLocalRouteTest() {
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
    }

    virtual void TearDown() {
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpTest *tcp_server_;
    OvsdbClientTcpSession *tcp_session_;
};

TEST_F(MulticastLocalRouteTest, MulticastLocalBasic) {
    // set current session in test context
    OvsdbTestSetSessionContext(tcp_session_);
    LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
               "client/test/xml/ucast-local-test-setup.xml");
    client->WaitForIdle();

    LogicalSwitchTable *table =
        tcp_session_->client_idl()->logical_switch_table();
    LogicalSwitchEntry key(table, UuidToString(MakeUuid(1)));
    LogicalSwitchEntry *entry = static_cast<LogicalSwitchEntry *>
        (table->Find(&key));
    EXPECT_TRUE((entry != NULL));
    if (entry != NULL) {
        WAIT_FOR(100, 10000,
                 (true == add_mcast_mac_local(entry->name(),
                                              "unknow-dst",
                                              "11.11.11.11")));
        // Add mcast route for another ToR IP
        WAIT_FOR(100, 10000,
                 (true == add_mcast_mac_local(entry->name(),
                                              "unknow-dst",
                                              "11.11.11.12")));

        client->WaitForIdle();
        MulticastMacLocalEntry *mcast_entry;
        // Wait for entry to add
        WAIT_FOR(1000, 10000,
                 (NULL != (mcast_entry = FindMcastLocal(entry->name())) &&
                  mcast_entry->IsResolved()));
        client->WaitForIdle();

        Ip4Address tor_ip_1 = Ip4Address::from_string("11.11.11.11");
        Ip4Address tor_ip_2 = Ip4Address::from_string("11.11.11.12");
        MacAddress mac("ff:ff:ff:ff:ff:ff");
        // Validate both the routes exported to EVPN table
        WAIT_FOR(500, 10000,
                 (NULL != EvpnRouteGet("vrf1", mac, tor_ip_1, 100)));
        WAIT_FOR(500, 10000,
                 (NULL != EvpnRouteGet("vrf1", mac, tor_ip_2, 100)));

        OvsdbMulticastMacLocalReq *req = new OvsdbMulticastMacLocalReq();
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();

        // retract and validate removal of mcast route individualy
        WAIT_FOR(100, 10000,
                 (true == del_mcast_mac_local(entry->name(),
                                              "unknow-dst", "11.11.11.11")));
        WAIT_FOR(500, 10000,
                 (NULL == EvpnRouteGet("vrf1", mac, tor_ip_1, 100)));
        WAIT_FOR(500, 10000,
                 (NULL != EvpnRouteGet("vrf1", mac, tor_ip_2, 100)));
        WAIT_FOR(500, 10000,
                 (true == del_mcast_mac_local(entry->name(),
                                              "unknow-dst", "11.11.11.12")));
        WAIT_FOR(500, 10000,
                 (NULL == EvpnRouteGet("vrf1", mac, tor_ip_1, 100)));
        WAIT_FOR(500, 10000,
                 (NULL == EvpnRouteGet("vrf1", mac, tor_ip_2, 100)));
        // Wait for entry to del
        WAIT_FOR(500, 10000,
                 (NULL == FindMcastLocal(entry->name())));
    }

    // set current session in test context
    LoadAndRun("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_"
               "client/test/xml/ucast-local-test-teardown.xml");
    client->WaitForIdle();
}

TEST_F(MulticastLocalRouteTest, MulticastLocal_add_mcroute_without_vrf_vn_link_present) {
    //Add vrf
    VrfAddReq("vrf1");
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    //Add VN
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    //Add device
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    WAIT_FOR(100, 10000,
             (agent_->physical_device_table()->Find(MakeUuid(1)) != NULL));
    //Add device_vn
    AddPhysicalDeviceVn(agent_, 1, 1, true);

    client->WaitForIdle();

    //Initialization done, now delete VRF VN link and then update VXLAN id in
    //VN.
    TestClient::WaitForIdle();
    VxLanNetworkIdentifierMode(true);
    TestClient::WaitForIdle();

    std::string ls_name = UuidToString(MakeUuid(1));
    WAIT_FOR(100, 10000,
             (true == add_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    TestClient::WaitForIdle();
    MulticastMacLocalEntry *mcast_entry;
    // Wait for entry to add
    WAIT_FOR(100, 10000,
             (NULL != (mcast_entry = FindMcastLocal(ls_name))));
    TestClient::WaitForIdle();

    WAIT_FOR(100, 10000, mcast_entry->IsResolved());

    WAIT_FOR(100, 10000,
             (true == del_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    // Wait for entry to del
    WAIT_FOR(100, 10000,
             (NULL == FindMcastLocal(ls_name)));

    //Delete
    DelPhysicalDeviceVn(agent_, 1, 1, true);
    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
    WAIT_FOR(1000, 10000,
             (agent_->physical_device_table()->
              Find(MakeUuid(1)) == NULL));
    VrfDelReq("vrf1");
    VnDelReq(1);
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));
}

TEST_F(MulticastLocalRouteTest, MulticastLocal_on_del_vrf_del_mcast) {
    //Add vrf
    VrfAddReq("vrf1");
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    //Add VN
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    //Add device
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    WAIT_FOR(100, 10000,
             (agent_->physical_device_table()->Find(MakeUuid(1)) != NULL));
    //Add device_vn
    AddPhysicalDeviceVn(agent_, 1, 1, true);
    TestClient::WaitForIdle();

    std::string ls_name = UuidToString(MakeUuid(1));
    WAIT_FOR(100, 10000,
             (true == add_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    TestClient::WaitForIdle();
    MulticastMacLocalEntry *mcast_entry;
    // Wait for entry to add
    WAIT_FOR(100, 10000,
             (NULL != (mcast_entry = FindMcastLocal(ls_name))));
    TestClient::WaitForIdle();

    WAIT_FOR(100, 10000, mcast_entry->IsResolved());

    //Delete
    VrfDelReq("vrf1");
    //To remove vrf from VN, add another non-existent vrf.
    VnAddReq(1, "vn1", "vrf2");
    //Since VRF is gone from VN even though VN is not gone, mcast entry shud be
    //gone.
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) == NULL));

    WAIT_FOR(100, 10000,
             (true == del_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    // Wait for entry to del
    WAIT_FOR(100, 10000,
             (NULL == FindMcastLocal(ls_name)));

    //Delete
    DelPhysicalDeviceVn(agent_, 1, 1, true);
    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
    WAIT_FOR(1000, 10000,
             (agent_->physical_device_table()->
              Find(MakeUuid(1)) == NULL));
    VnDelReq(1);
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));
}

TEST_F(MulticastLocalRouteTest, MulticastLocal_on_del_vrf_vn_link) {
    //Add vrf
    VrfAddReq("vrf1");
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    //Add VN
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    //Add device
    DBRequest device_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    device_req.key.reset(new PhysicalDeviceKey(MakeUuid(1)));
    device_req.data.reset(new PhysicalDeviceData(agent_, "test-router",
                                                 "test-router", "",
                                                 Ip4Address::from_string("1.1.1.1"),
                                                 Ip4Address::from_string("2.2.2.2"),
                                                 "OVS", NULL));
    agent_->physical_device_table()->Enqueue(&device_req);
    WAIT_FOR(100, 10000,
             (agent_->physical_device_table()->Find(MakeUuid(1)) != NULL));
    //Add device_vn
    AddPhysicalDeviceVn(agent_, 1, 1, true);
    TestClient::WaitForIdle();

    std::string ls_name = UuidToString(MakeUuid(1));
    WAIT_FOR(100, 10000,
             (true == add_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    TestClient::WaitForIdle();
    MulticastMacLocalEntry *mcast_entry;
    // Wait for entry to add
    WAIT_FOR(100, 10000,
             (NULL != (mcast_entry = FindMcastLocal(ls_name))));
    TestClient::WaitForIdle();

    WAIT_FOR(100, 10000, mcast_entry->IsResolved());

    //To remove vrf from VN, add another non-existent vrf.
    VnAddReq(1, "vn1", "vrf2");
    //Since VRF is gone from VN even though VN is not gone, mcast entry shud be
    //gone.
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) == NULL));
    WAIT_FOR(100, 10000, !mcast_entry->IsResolved());
    //Add back the VRF
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) != NULL));
    WAIT_FOR(100, 10000, mcast_entry->IsResolved());

    OvsdbMulticastMacLocalReq *mcast_req = new OvsdbMulticastMacLocalReq();
    mcast_req->HandleRequest();
    client->WaitForIdle();
    mcast_req->Release();

    WAIT_FOR(100, 10000,
             (true == del_mcast_mac_local(ls_name, "unknow-dst",
                                          "11.11.11.11")));
    // Wait for entry to del
    WAIT_FOR(100, 10000,
             (NULL == FindMcastLocal(ls_name)));

    //Delete
    DelPhysicalDeviceVn(agent_, 1, 1, true);
    DBRequest del_dev_req(DBRequest::DB_ENTRY_DELETE);
    del_dev_req.key.reset(new PhysicalDeviceVnKey(MakeUuid(1),
                                                  MakeUuid(1)));
    agent_->physical_device_table()->Enqueue(&del_dev_req);
    WAIT_FOR(1000, 10000,
             (agent_->physical_device_table()->
              Find(MakeUuid(1)) == NULL));
    VrfDelReq("vrf1");
    VnDelReq(1);
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));
}

TEST_F(MulticastLocalRouteTest, tunnel_nh_ovs_multicast) {
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

    IpAddress server = Ip4Address::from_string("1.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);
    EXPECT_TRUE(peer->export_to_controller());

    AddEncapList("MPLSoUDP", "VXLAN", NULL);
    client->WaitForIdle();
    AddVrf("vrf1", 1);
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    AddVn("vn1", 1, true);
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    MacAddress mac("ff:ff:ff:ff:ff:ff");
    Ip4Address tor_ip = Ip4Address::from_string("111.111.111.111");
    Ip4Address tsn_ip = Ip4Address::from_string("127.0.0.1");

    VrfEntry *vrf = VrfGet("vrf1");
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf->GetEvpnRouteTable());
    WAIT_FOR(100, 100, (vrf->GetBridgeRouteTable() != NULL));
    WAIT_FOR(100, 100, (L2RouteFind("vrf1", mac)));
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", mac, IpAddress(), 100);
    EXPECT_TRUE(evpn_rt == NULL);

    //Add OVS path
    table->AddOvsPeerMulticastRouteReq(peer, 100, "dummy", tsn_ip, tor_ip);
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));
    client->WaitForIdle();
    evpn_rt = EvpnRouteGet("vrf1", mac, tor_ip, 100);
    EXPECT_TRUE(((BgpPeer *)bgp_peer_)->
                GetRouteExportState(evpn_rt->get_table_partition(), evpn_rt));

    const AgentPath *path = evpn_rt->FindPath(peer);
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(path->nexthop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);
    EXPECT_TRUE(*nh->GetSip() == tsn_ip);

    AddEncapList("MPLSoUDP", NULL, NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));

    evpn_rt = EvpnRouteGet("vrf1", mac, tor_ip, 100);
    path = evpn_rt->FindPath(peer);
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    nh = dynamic_cast<const TunnelNH *>(path->nexthop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);
    EXPECT_TRUE(*nh->GetSip() == tsn_ip);

    AddEncapList("MPLSoUDP", "VXLAN", NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));

    table->DeleteOvsPeerMulticastRouteReq(peer, 100, tor_ip);
    client->WaitForIdle();

    // Change tunnel-type order
    peer_manager_->Free(peer);
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));

    // enable reconnect to Ovsdb Server
    tcp_server_->set_enable_connect(true);
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
