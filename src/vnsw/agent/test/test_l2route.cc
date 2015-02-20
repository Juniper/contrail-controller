//
//  test_l2route.cc
//  vnsw/agent/test
//
//  Created by Praveen K V
//  Copyright (c) 2012 Contrail Systems. All rights reserved.
//
#include "base/os.h"
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

        strcpy(local_vm_mac_str_, "00:00:01:01:01:10");
        local_vm_mac_ = MacAddress::FromString(local_vm_mac_str_);
        strcpy(local_vm_ip4_str_, "1.1.1.10");
        local_vm_ip4_ = Ip4Address::from_string(local_vm_ip4_str_);
        strcpy(local_vm_ip6_str_, "fdff::10");
        local_vm_ip6_ = Ip6Address::from_string(local_vm_ip6_str_);

        strcpy(remote_vm_mac_str_, "00:00:01:01:01:11");
        remote_vm_mac_ = MacAddress::FromString(remote_vm_mac_str_);
        strcpy(remote_vm_ip4_str_, "1.1.1.11");
        remote_vm_ip4_ = Ip4Address::from_string(remote_vm_ip4_str_);
        strcpy(remote_vm_ip6_str_, "fdff::11");
        remote_vm_ip6_ = Ip6Address::from_string(remote_vm_ip6_str_);

        strcpy(local_vm_mac_2_str_, "00:00:02:02:02:10");
        local_vm_mac_2_ = MacAddress::FromString(local_vm_mac_2_str_);
        strcpy(local_vm_ip4_2_str_, "2.2.2.10");
        local_vm_ip4_2_ = Ip4Address::from_string(local_vm_ip4_2_str_);
        strcpy(local_vm_ip6_2_str_, "fdff::20");
        local_vm_ip6_2_ = Ip6Address::from_string(local_vm_ip6_2_str_);

        strcpy(remote_vm_mac_2_str_, "00:00:02:02:02:11");
        remote_vm_mac_2_ = MacAddress::FromString(remote_vm_mac_2_str_);
        strcpy(remote_vm_ip4_2_str_, "2.2.2.11");
        remote_vm_ip4_2_ = Ip4Address::from_string(remote_vm_ip4_2_str_);
        strcpy(remote_vm_ip6_2_str_, "fdff::21");
        remote_vm_ip6_2_ = Ip6Address::from_string(remote_vm_ip6_2_str_);
    }

    ~RouteTest() {
    }

    virtual void SetUp() {
        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        PhysicalInterface::CreateReq(agent_->interface_table(),
                                eth_name_,
                                agent_->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
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

    void AddRemoteVmRoute(MacAddress &remote_vm_mac, const IpAddress &ip_addr,
                          const Ip4Address &server_ip,
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any other peer than localvmpeer
        BridgeTunnelRouteAdd(agent_->local_peer(), vrf_name_, bmap, server_ip,
                             label, remote_vm_mac, ip_addr, 32);
        client->WaitForIdle();
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        InetInterfaceKey vhost_key(agent_->vhost_interface()->name());
        agent_->fabric_inet4_unicast_table()->AddResolveRoute(
                agent_->local_peer(),
                agent_->fabric_vrf_name(), server_ip, plen, vhost_key,
                0, false, "", SecurityGroupList());
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac, const IpAddress &ip_addr) {
        EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                        ip_addr, 0);
        client->WaitForIdle();
        while (L2RouteFind(vrf_name, remote_vm_mac, ip_addr) == true) {
            client->WaitForIdle();
        }
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;

    char local_vm_mac_str_[100];
    MacAddress  local_vm_mac_;
    char local_vm_ip4_str_[100];
    Ip4Address  local_vm_ip4_;
    char local_vm_ip6_str_[100];
    IpAddress  local_vm_ip6_;

    char remote_vm_mac_str_[100];
    MacAddress  remote_vm_mac_;
    char remote_vm_ip4_str_[100];
    Ip4Address  remote_vm_ip4_;
    char remote_vm_ip6_str_[100];
    IpAddress  remote_vm_ip6_;

    char local_vm_mac_2_str_[100];
    MacAddress  local_vm_mac_2_;
    char local_vm_ip4_2_str_[100];
    Ip4Address  local_vm_ip4_2_;
    char local_vm_ip6_2_str_[100];
    IpAddress  local_vm_ip6_2_;

    char remote_vm_mac_2_str_[100];
    MacAddress  remote_vm_mac_2_;
    char remote_vm_ip4_2_str_[100];
    Ip4Address  remote_vm_ip4_2_;
    char remote_vm_ip6_2_str_[100];
    IpAddress  remote_vm_ip6_2_;

    Ip4Address  vhost_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;

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

    WAIT_FOR(1000, 100, (VmPortL2Active(input, 0) == true));
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->bridging() == true);
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == true));
    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    uint32_t label = rt->GetActiveLabel();
    MplsLabelKey key(MplsLabel::MCAST_NH, label);
    MplsLabel *mpls = 
        static_cast<MplsLabel *>(agent_->mpls_table()->Find(&key, true));

    EXPECT_TRUE(mpls->nexthop() == nh);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    EXPECT_TRUE(intf_nh->GetFlags() == InterfaceNHFlags::BRIDGE);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == false));
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

    WAIT_FOR(1000, 100, (VmPortActive(input, 0) == true));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_));
    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    uint32_t label = rt->GetActiveLabel();
    MplsLabelKey key(MplsLabel::MCAST_NH, label);
    MplsLabel *mpls = 
        static_cast<MplsLabel *>(agent_->mpls_table()->Find(&key, true));

    EXPECT_TRUE(mpls->nexthop() == nh);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    EXPECT_TRUE(intf_nh->GetFlags() == InterfaceNHFlags::BRIDGE);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == false));
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

    WAIT_FOR(1000, 100, (VmPortL2Active(input, 0) == true));
    MulticastGroupObject *obj = 
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj != NULL);
    EXPECT_TRUE(obj->bridging() == true);
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_));

    MplsReq *mpls_list_req = new MplsReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1,
                                               result));
    mpls_list_req->HandleRequest();
    client->WaitForIdle();
    mpls_list_req->Release();
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == false));
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
    AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                     MplsTable::kStartLabel, bmap);
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_,
                                      remote_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveLabel() == MplsTable::kStartLabel);

    BridgeRouteReq *l2_req = new BridgeRouteReq();
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

    DeleteRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
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

    WAIT_FOR(100, 100, (VmPortActive(input, 0) == true));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_));

    BridgeRouteEntry *vnet1_rt = L2RouteGet(vrf_name_, local_vm_mac_,
                                            local_vm_ip4_);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    InetUnicastRouteEntry *inet_rt = RouteGet(vrf_name_, local_vm_ip4_, 32);
    EXPECT_TRUE(inet_rt->GetActivePath()->GetActiveLabel() !=
                MplsTable::kInvalidLabel);

    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddRemoteVmRoute(remote_vm_mac_2_, remote_vm_ip4_2_, server1_ip_, 1,
                     bmap);
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, remote_vm_mac_2_, remote_vm_ip4_2_)
              == true));

    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_2_,
                                      remote_vm_ip4_2_);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::VXLAN);
    EXPECT_TRUE(rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());
    BridgeRouteReq *l2_req = new BridgeRouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(1);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();

    DeleteRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_2_,
                remote_vm_ip4_2_);
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

    WAIT_FOR(1000, 100, (VmPortActive(input, 0) == true));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_));

    BridgeRouteEntry *vnet1_rt = L2RouteGet(vrf_name_, local_vm_mac_,
                                            local_vm_ip4_);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetActiveLabel() == 101);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);

    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddRemoteVmRoute(remote_vm_mac_2_, remote_vm_ip4_2_, server1_ip_, 1, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, remote_vm_mac_2_, remote_vm_ip4_2_));
    BridgeRouteReq *l2_req = new BridgeRouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    l2_req->set_vrf_index(1);
    l2_req->HandleRequest();
    client->WaitForIdle();
    l2_req->Release();
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_2_,
                                      remote_vm_ip4_2_);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh);
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::VXLAN);
    EXPECT_TRUE(rt->GetActivePath()->GetActiveLabel() == 1);
    EXPECT_TRUE(rt->GetActivePath()->GetTunnelType() ==
                TunnelType::VXLAN);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_2_,
                remote_vm_ip4_2_);
    client->WaitForIdle();

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, Bridge_route_key) {
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
    BridgeRouteEntry *vnet1_rt = L2RouteGet(vrf_name_, local_vm_mac_,
                                            local_vm_ip4_);
    WAIT_FOR(1000, 100, (vnet1_rt != NULL));
    BridgeRouteKey new_key(agent_->local_vm_peer(), "vrf2", local_vm_mac_,
                           0);
    vnet1_rt->SetKey(&new_key);
    EXPECT_TRUE(vnet1_rt->vrf()->GetName() == "vrf2");
    EXPECT_TRUE(MacAddress(new_key.ToString()) == local_vm_mac_);
    BridgeRouteKey restore_key(agent_->local_vm_peer(), "vrf1",
                               local_vm_mac_, 0);
    vnet1_rt->SetKey(&restore_key);
    EXPECT_TRUE(vnet1_rt->vrf()->GetName() == "vrf1");
    EXPECT_TRUE(MacAddress(restore_key.ToString()) == local_vm_mac_);

    DelVrf("vrf2");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();
}

TEST_F(RouteTest, Sandesh_chaeck_with_invalid_vrf) {
    BridgeRouteReq *l2_req = new BridgeRouteReq();
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

    WAIT_FOR(1000, 100, (VmPortActive(input, 0) == true));
    EXPECT_TRUE(L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_));

    BridgeRouteEntry *vnet1_rt = L2RouteGet(vrf_name_, local_vm_mac_,
                                            local_vm_ip4_);
    WAIT_FOR(1000, 100, (vnet1_rt != NULL));
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
    WAIT_FOR(1000, 100, (RouteGet(vrf_name_, local_vm_ip4_, 32) != NULL));
    WAIT_FOR(1000, 100,
             (L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_) != NULL));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    WAIT_FOR(1000, 100,
             (L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_) == NULL));
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
    EvpnAgentRouteTable::AddRemoteVmRouteReq(agent_->local_vm_peer(),
                                             vrf_name_, local_vm_mac_,
                                             local_vm_ip4_, 0, NULL);

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
    EvpnAgentRouteTable::DeleteReq(agent_->local_vm_peer(), vrf_name_,
                                     local_vm_mac_, local_vm_ip4_, 0);
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
