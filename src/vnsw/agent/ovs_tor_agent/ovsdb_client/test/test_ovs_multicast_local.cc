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
#include "oper/physical_device_vn.h"
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

class OvsBaseTest : public ::testing::Test {
protected:
    OvsBaseTest() {
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
    }

    virtual void TearDown() {
    }

    Agent *agent_;
    TestOvsAgentInit *init_;
    OvsPeerManager *peer_manager_;
    OvsdbClientTcpSession *tcp_session_;
};

TEST_F(OvsBaseTest, MulticastLocalBasic) {
    AgentUtXmlTest
        test("controller/src/vnsw/agent/ovs_tor_agent/ovsdb_client/test/xml/multicast-local-base.xml");
    // set current session in test context
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

TEST_F(OvsBaseTest, MulticastLocal_add_mcroute_without_vrf_vn_link_present) {
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

    //Initialization done, now delete VRF VN link and then update VXLAN id in
    //VN.
    TestClient::WaitForIdle();
    VxLanNetworkIdentifierMode(true);
    TestClient::WaitForIdle();

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

TEST_F(OvsBaseTest, MulticastLocal_on_del_vrf_del_mcast) {
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

    //Delete
    VrfDelReq("vrf1");
    //To remove vrf from VN, add another non-existent vrf.
    VnAddReq(1, "vn1", "vrf2");
    //Since VRF is gone from VN even though VN is not gone, mcast entry shud be
    //gone.
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) == NULL));

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

TEST_F(OvsBaseTest, MulticastLocal_on_del_vrf_vn_link) {
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

    //To remove vrf from VN, add another non-existent vrf.
    VnAddReq(1, "vn1", "vrf2");
    //Since VRF is gone from VN even though VN is not gone, mcast entry shud be
    //gone.
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) == NULL));
    //Add back the VRF
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(1000, 10000, (L2RouteGet("vrf1", MacAddress::BroadcastMac()) != NULL));

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

TEST_F(OvsBaseTest, tunnel_nh_ovs_multicast) {
    agent_->set_tor_agent_enabled(true);
    IpAddress server = Ip4Address::from_string("1.1.1.1");
    OvsPeer *peer = peer_manager_->Allocate(server);
    EXPECT_TRUE(peer->export_to_controller());

    AddEncapList("MPLSoUDP", "VXLAN", NULL);
    client->WaitForIdle();
    VrfAddReq("vrf1");
    WAIT_FOR(100, 10000, (VrfGet("vrf1", false) != NULL));
    VnAddReq(1, "vn1", "vrf1");
    WAIT_FOR(100, 10000, (VnGet(1) != NULL));

    MacAddress mac("ff:ff:ff:ff:ff:ff");
    Ip4Address tor_ip = Ip4Address::from_string("111.111.111.111");
    Ip4Address tsn_ip = Ip4Address::from_string("127.0.0.1");

    VrfEntry *vrf = VrfGet("vrf1");
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf->GetBridgeRouteTable());
    WAIT_FOR(100, 100, (vrf->GetBridgeRouteTable() != NULL));
    WAIT_FOR(100, 100, (L2RouteFind("vrf1", mac)));
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet("vrf1", mac);
    EXPECT_FALSE(((BgpPeer *)bgp_peer_)->
                 GetRouteExportState(rt->get_table_partition(), rt));

    //Add OVS path
    table->AddOvsPeerMulticastRouteReq(peer, 100, "dummy", tsn_ip, tor_ip);
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));
    client->WaitForIdle();
    EXPECT_TRUE(((BgpPeer *)bgp_peer_)->
                GetRouteExportState(rt->get_table_partition(), rt));

    rt = L2RouteGet("vrf1", mac);
    const AgentPath *path = rt->FindPath(peer);
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(path->nexthop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);
    EXPECT_TRUE(*nh->GetSip() == tsn_ip);

    AddEncapList("MPLSoUDP", NULL, NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));

    rt = L2RouteGet("vrf1", mac);
    path = rt->FindPath(peer);
    EXPECT_TRUE(path->tunnel_dest() == tor_ip);
    nh = dynamic_cast<const TunnelNH *>(path->nexthop());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(*nh->GetDip() == tor_ip);
    EXPECT_TRUE(*nh->GetSip() == tsn_ip);

    AddEncapList("MPLSoUDP", "VXLAN", NULL);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (L2RouteGet("vrf1", mac) != NULL));

    table->DeleteOvsPeerMulticastRouteReq(peer, 100);
    client->WaitForIdle();

    // Change tunnel-type order
    peer_manager_->Free(peer);
    client->WaitForIdle();
    VrfDelReq("vrf1");
    VnDelReq(1);
    WAIT_FOR(1000, 10000, (VrfGet("vrf1", true) == NULL));
    WAIT_FOR(1000, 10000, (VnGet(1) == NULL));
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
