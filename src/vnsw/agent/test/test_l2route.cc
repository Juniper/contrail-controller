//
//  test_l2route.cc
//  vnsw/agent/test
//
//  Created by Praveen K V
//  Copyright (c) 2012 Contrail Systems. All rights reserved.
//
#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
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
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;
std::string eth_itf;

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class RouteTest : public ::testing::Test {
public:
    static void SetTunnelType(TunnelType::Type type) {
        TunnelType::SetDefaultType(type);
        type_ = type;
    }
    static TunnelType::Type GetTunnelType() {
        return type_;
    }

protected:
    RouteTest() : vrf_name_("vrf1"), eth_name_(eth_itf) {
        default_dest_ip_ = Ip4Address::from_string("0.0.0.0");
        agent_ =Agent::GetInstance();

        if (agent_->router_id_configured()) {
            vhost_ip_ = agent_->router_id();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        server1_ip_ = Ip4Address::from_string("10.1.1.11");
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        local_vm_mac_ = MacAddress::FromString("00:00:01:01:01:10");
        remote_vm_mac_ = MacAddress::FromString("00:00:01:01:01:11");
    }

    ~RouteTest() {
    }

    virtual void SetUp() {
        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        PhysicalInterface::CreateReq(agent_->interface_table(),
                                eth_name_,
                                agent_->fabric_vrf_name(), false);
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        TestRouteTable table1(1);
        WAIT_FOR(100, 100, (table1.Size() == 0));
        EXPECT_EQ(table1.Size(), 0U);

        TestRouteTable table2(2);
        WAIT_FOR(100, 100, (table2.Size() == 0));
        EXPECT_EQ(table2.Size(), 0U);

        TestRouteTable table3(3);
        WAIT_FOR(100, 100, (table3.Size() == 0));
        EXPECT_EQ(table3.Size(), 0U);

        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
    }

    void AddRemoteVmRoute(MacAddress &remote_vm_mac,
                          const Ip4Address &server_ip,
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any toher peer than localvmpeer

        Layer2TunnelRouteAdd(agent_->local_peer(), vrf_name_,
                             bmap, server_ip, label, remote_vm_mac, local_vm_ip_, 32);
        client->WaitForIdle();
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        agent_->fabric_inet4_unicast_table()->AddResolveRoute(
                agent_->fabric_vrf_name(), server_ip, plen);
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac) {
        Layer2AgentRouteTable::DeleteReq(peer, vrf_name_,
            remote_vm_mac, 0, NULL);
        client->WaitForIdle();
        while (L2RouteFind(vrf_name, remote_vm_mac) == true) {
            client->WaitForIdle();
        }
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;
    Ip4Address  local_vm_ip_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  vhost_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;
    MacAddress  local_vm_mac_;
    MacAddress  remote_vm_mac_;
    static TunnelType::Type type_;
    Agent *agent_;
};
TunnelType::Type RouteTest::type_;

#if 0
TEST_F(RouteTest, EvpnStatusOff) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_FALSE(L2RouteFind(vrf_name_, *local_vm_mac_));
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}
#endif

TEST_F(RouteTest, LocalVmRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    client->Reset();
    CreateL2VmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortL2Active(input, 0));
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->layer2_forwarding() == true);
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));
    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    uint32_t label = rt->GetActiveLabel();
    MplsLabelKey key(MplsLabel::MCAST_NH, label);
    MplsLabel *mpls = 
        static_cast<MplsLabel *>(agent_->mpls_table()->Find(&key, true));

    EXPECT_TRUE(mpls->nexthop() == nh);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    EXPECT_TRUE(intf_nh->GetFlags() == InterfaceNHFlags::LAYER2);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(L2RouteFind(vrf_name_, local_vm_mac_) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, LocalVmRoute_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    VxLanNetworkIdentifierMode(true);
    client->WaitForIdle();
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));
    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    uint32_t label = rt->GetActiveLabel();
    MplsLabelKey key(MplsLabel::MCAST_NH, label);
    MplsLabel *mpls = 
        static_cast<MplsLabel *>(agent_->mpls_table()->Find(&key, true));

    EXPECT_TRUE(mpls->nexthop() == nh);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    EXPECT_TRUE(intf_nh->GetFlags() == InterfaceNHFlags::LAYER2);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(L2RouteFind(vrf_name_, local_vm_mac_) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, Mpls_sandesh_check_with_l2route) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    client->Reset();
    CreateL2VmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortL2Active(input, 0));
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->layer2_forwarding() == true);
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));

    MplsReq *mpls_list_req = new MplsReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    mpls_list_req->HandleRequest();
    client->WaitForIdle();
    mpls_list_req->Release();
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(L2RouteFind(vrf_name_, local_vm_mac_) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_1) {
    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();

    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::MPLS_GRE;
    AddRemoteVmRoute(remote_vm_mac_, server1_ip_, MplsTable::kStartLabel, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, remote_vm_mac_));

    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_);
    EXPECT_TRUE(rt->GetActiveLabel() == MplsTable::kStartLabel);

    Layer2RouteReq *l2_req = new Layer2RouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(1);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();

    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_VxLan_auto) {
    struct PortInfo input[] = {
        {"vnet2", 2, "2.2.2.10", "00:00:02:02:02:10", 2, 2},
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    RouteTest::SetTunnelType(TunnelType::VXLAN);
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    CreateVmportEnv(input, 2);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));

    MacAddress vxlan_vm_mac("00:00:01:01:01:10");
    Layer2RouteEntry *vnet1_rt = L2RouteGet(vrf_name_, vxlan_vm_mac);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    InetUnicastRouteEntry *inet_rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(inet_rt->GetActivePath()->GetActiveLabel() !=
                MplsTable::kInvalidLabel);

    vxlan_vm_mac = MacAddress::FromString("00:00:02:02:02:22");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddRemoteVmRoute(vxlan_vm_mac, server1_ip_, 1, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, vxlan_vm_mac));

    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, vxlan_vm_mac);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::VXLAN);
    EXPECT_TRUE(rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());
    Layer2RouteReq *l2_req = new Layer2RouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(1);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();

    DeleteRoute(agent_->local_peer(), vrf_name_, vxlan_vm_mac);
    client->WaitForIdle();

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_VxLan_config) {
    struct PortInfo input[] = {
        {"vnet2", 2, "2.2.2.10", "00:00:02:02:02:10", 2, 2},
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    RouteTest::SetTunnelType(TunnelType::VXLAN);
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    VxLanNetworkIdentifierMode(true);
    client->WaitForIdle();
    CreateVmportEnv(input, 2);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));

    MacAddress vxlan_vm_mac = MacAddress::FromString("00:00:01:01:01:10");
    Layer2RouteEntry *vnet1_rt = L2RouteGet(vrf_name_, vxlan_vm_mac);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetActiveLabel() == 101);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);

    vxlan_vm_mac = MacAddress::FromString("00:00:02:02:02:22");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddRemoteVmRoute(vxlan_vm_mac, server1_ip_, 1, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, vxlan_vm_mac));
    Layer2RouteReq *l2_req = new Layer2RouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(1);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();

    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, vxlan_vm_mac);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::VXLAN);
    EXPECT_TRUE(rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteRoute(agent_->local_peer(), vrf_name_, vxlan_vm_mac);
    client->WaitForIdle();

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, Layer2_route_key) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    AddL2Vn("vn1", 1);
    AddVrf("vrf1");
    AddVrf("vrf2");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    client->Reset();
    CreateL2VmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortL2Active(input, 0));
    Layer2RouteEntry *vnet1_rt = L2RouteGet(vrf_name_, local_vm_mac_);
    Layer2RouteKey new_key(agent_->local_vm_peer(), "vrf2", local_vm_mac_, 0);
    vnet1_rt->SetKey(&new_key);
    EXPECT_TRUE(vnet1_rt->vrf()->GetName() == "vrf2");
    EXPECT_TRUE(MacAddress(new_key.ToString()) == MacAddress("00:00:01:01:01:10"));
    Layer2RouteKey restore_key(agent_->local_vm_peer(), "vrf1",
                               local_vm_mac_, 0);
    vnet1_rt->SetKey(&restore_key);
    EXPECT_TRUE(vnet1_rt->vrf()->GetName() == "vrf1");
    EXPECT_TRUE(MacAddress(restore_key.ToString()) == MacAddress("00:00:01:01:01:10"));

    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, Sandesh_chaeck_with_invalid_vrf) {
    Layer2RouteReq *l2_req = new Layer2RouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(100);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();
}

TEST_F(RouteTest, Vxlan_basic) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    RouteTest::SetTunnelType(TunnelType::VXLAN);
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    VxLanNetworkIdentifierMode(true);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_));

    MacAddress vxlan_vm_mac("00:00:01:01:01:10");
    Layer2RouteEntry *vnet1_rt = L2RouteGet(vrf_name_, vxlan_vm_mac);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetActiveLabel() == 101);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    VxLanIdKey vxlan_id_key(101);
    VxLanId *vxlan_id =
        static_cast<VxLanId *>(agent_->vxlan_table()->FindActiveEntry(&vxlan_id_key));
    VxLanIdKey new_vxlan_id_key(102);
    vxlan_id->SetKey(&new_vxlan_id_key);
    EXPECT_TRUE(vxlan_id->vxlan_id() == 102);
    vxlan_id->SetKey(&vxlan_id_key);
    EXPECT_TRUE(vxlan_id->vxlan_id() == 101);
    DBEntryBase::KeyPtr key = vxlan_id->GetDBRequestKey();
    VxLanIdKey *db_key = static_cast<VxLanIdKey *>(key.get());
    EXPECT_TRUE(vxlan_id->vxlan_id() == db_key->vxlan_id());

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, vxlan_network_id_change_for_non_l2_interface) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    //TODO stop the walk before cancel
    VxLanNetworkIdentifierMode(true);
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    InetUnicastRouteEntry *rt1 = RouteGet(vrf_name_, local_vm_ip_, 32);
    WAIT_FOR(100, 1000, (RouteGet(vrf_name_, local_vm_ip_, 32) != NULL));
    WAIT_FOR(100, 1000, (L2RouteGet(vrf_name_, local_vm_mac_) != NULL));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    WAIT_FOR(100, 1000, (L2RouteGet(vrf_name_, local_vm_mac_) == NULL));
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, Enqueue_l2_route_add_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    ComponentNHKeyList component_nh_key_list;
    Layer2AgentRouteTable::AddRemoteVmRouteReq(agent_->local_vm_peer(),
                                               vrf_name_, local_vm_mac_,
                                               local_vm_ip_, 0, 32, NULL);

    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, Enqueue_l2_route_del_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Layer2AgentRouteTable::DeleteReq(agent_->local_vm_peer(), vrf_name_,
                                     local_vm_mac_, 0, NULL);
    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    eth_itf = Agent::GetInstance()->fabric_interface_name();

    RouteTest::SetTunnelType(TunnelType::MPLS_GRE);
    int ret = RUN_ALL_TESTS();
    RouteTest::SetTunnelType(TunnelType::MPLS_UDP);
    ret += RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
