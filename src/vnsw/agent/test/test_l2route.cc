//
//  test_l2route.cc
//  vnsw/agent/test
//
//  Created by Praveen K V
//  Copyright (c) 2012 Contrail Systems. All rights reserved.
//
#include "base/os.h"
#include <base/logging.h>
#include <boost/shared_ptr.hpp>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "controller/controller_route_walker.h"
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

#include "xmpp/xmpp_init.h"
#include "xmpp_multicast_types.h"
#include <xmpp_unicast_types.h>
#include <controller/controller_export.h>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;
std::string eth_itf;

void RouterIdDepInit(Agent *agent) {
}

static void WalkDone(DBTableBase *base)
{
    return;
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class TestL2RouteState : public DBState {
public:
    int id_;
};

void TestL2RouteVrfNotify(DBTablePartBase *partition, DBEntryBase *e) {
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

        strcpy(flood_mac_str_, "ff:ff:ff:ff:ff:ff");
        flood_mac_ = MacAddress::FromString(flood_mac_str_);
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

    void DeleteBridgeRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac, const IpAddress &ip_addr) {
        const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
        EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                        ip_addr, 0,
                                        ((bgp_peer == NULL) ? NULL :
                                         (new ControllerVmRoute(bgp_peer))));
        client->WaitForIdle();
        //while (L2RouteFind(vrf_name, remote_vm_mac, ip_addr) == true) {
        //    client->WaitForIdle();
        //}
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

    char flood_mac_str_[100];
    MacAddress  flood_mac_;
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
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == true));
    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    uint32_t label = rt->GetActiveLabel();
    MplsLabelKey key(label);
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
    MplsLabelKey key(label);
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

TEST_F(RouteTest, RemoteVmRoute_label_change) {
    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();

    TunnelType::TypeBmap bmap;
    bmap = TunnelType::MplsType();
    AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                     MplsTable::kStartLabel, bmap);
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_,
                                      remote_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveLabel() == MplsTable::kStartLabel);

    AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                     MplsTable::kStartLabel + 1, bmap);
    rt = L2RouteGet(vrf_name_, remote_vm_mac_,
                    remote_vm_ip4_);
    EXPECT_TRUE(rt->GetActiveLabel() == (MplsTable::kStartLabel + 1));

    DeleteBridgeRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
    client->WaitForIdle();
    DelEncapList();
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

    DeleteBridgeRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_,
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

    DeleteBridgeRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_2_,
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

    DeleteBridgeRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_2_,
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

TEST_F(RouteTest, RemoteVxlanEncapRoute_1) {
    client->Reset();
    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    RouteTest::SetTunnelType(TunnelType::MPLS_GRE);
    DelEncapList();
    client->WaitForIdle();

    TunnelType::TypeBmap bmap = TunnelType::VxlanType();
    AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                     MplsTable::kStartLabel, bmap);
    WAIT_FOR(1000, 100,
             (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, remote_vm_mac_,
                                      remote_vm_ip4_);
    const AgentPath *path = rt->GetActivePath();
    EXPECT_TRUE(path->tunnel_type() != TunnelType::VXLAN);

    //Update VXLAN in encap prio
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    path = rt->GetActivePath();
    EXPECT_TRUE(path->tunnel_type() == TunnelType::VXLAN);
    const TunnelNH *nh =
        static_cast<const TunnelNH *>(path->nexthop());
    EXPECT_TRUE(nh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteBridgeRoute(agent_->local_peer(), vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
    client->WaitForIdle();
    DelEncapList();
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
                                     local_vm_mac_, local_vm_ip4_, 0, NULL);
    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, add_deleted_peer_to_multicast_route) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    TaskScheduler::GetInstance()->Stop();
    client->WaitForIdle();
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get()->GetAgentXmppChannel(),
                                                        xmps::NOT_READY);
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
    mc_handler->ModifyTorMembers(bgp_peer.get(),
                                 "vrf1",
                                 olist,
                                 10,
                                 1);
    client->WaitForIdle();
    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_FALSE(rt->FindPath(bgp_peer.get()));

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

//Add local VM - flood route gets added
//Send explicitly EVPN path addition
//Delete local VM - removes local path
//Send explicit delete for evpn
//Result - route should get deleted
TEST_F(RouteTest, all_evpn_routes_deleted_when_local_vms_are_gone) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    //Add local VM
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    //Send explicit evpn olist
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 olist,
                                 0,
                                 1);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt != NULL);

    //Delete VM, local vm path goes off
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    WAIT_FOR(1000, 1000, (rt->FindPath(agent_->local_vm_peer()) == NULL));
    EXPECT_TRUE(rt != NULL);

    //Send explicit delete of evpn olist
    TunnelOlist del_olist;
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 del_olist,
                                 0,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt->FindPath(bgp_peer_ptr) == NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, evpn_mcast_label_deleted) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    //Add VM
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    uint32_t evpn_mpls_label = rt->GetActivePath()->label();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    //Add EVPN olist
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 olist,
                                 0,
                                 1);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    //Label is retained
    EXPECT_TRUE(rt->GetActivePath()->label() == evpn_mpls_label);

    //Delete VM
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    //Because VN is pending.
    EXPECT_TRUE(agent_->mpls_table()->FindActiveEntry(new MplsLabelKey(evpn_mpls_label)) != NULL);
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    WAIT_FOR(1000, 1000, (rt->FindPath(agent_->local_vm_peer()) == NULL));
    EXPECT_TRUE(rt != NULL);
    //Label is retained as evpn still present
    EXPECT_TRUE(rt->GetActivePath()->label() == 0);
    EXPECT_TRUE(rt->FindPath(agent_->local_peer())->label() == evpn_mpls_label);
    EXPECT_TRUE(FindMplsLabel(evpn_mpls_label));
    MplsLabel *mpls_entry = GetActiveLabel(evpn_mpls_label);
    EXPECT_TRUE(mpls_entry->nexthop() ==
                rt->GetActiveNextHop());

    //Delete EVPN
    TunnelOlist del_olist;
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 del_olist,
                                 0,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    //route should get deleted
    EXPECT_TRUE(rt->FindPath(bgp_peer_ptr) == NULL);

    //Label should have been released
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(agent_->mpls_table()->FindActiveEntry(new MplsLabelKey(evpn_mpls_label)) == NULL);

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(1000, 1000, (rt->FindPath(agent_->local_vm_peer()) != NULL));
    //New label taken, should re-use old one
    EXPECT_TRUE(rt->GetActivePath()->label() == evpn_mpls_label);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

//Let VRF be unsubscribed from Bgp Peer and then add
//route in same. This route should not be sent to BGP
//peer.
TEST_F(RouteTest, DISABLED_send_route_add_in_not_subscribed_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000,
             (L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_) != NULL));
    EvpnRouteEntry *rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    EXPECT_TRUE(route_state->exported_ == true);

    VrfEntry *vrf = VrfGet(vrf_name_.c_str());
    RouteExport *rt_export_tmp[Agent::ROUTE_TABLE_MAX];
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>
        (bgp_peer_ptr->GetVrfExportState(vrf->get_table_partition(), vrf));
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        rt_export_tmp[table_type] = vrf_state->rt_export_[table_type];
    }

    //Stop BGP peer
    vrf->ClearState(vrf->get_table_partition()->parent(),
                    bgp_peer_ptr->GetVrfExportListenerId());
    //Mark Old route as not exported.
    route_state->exported_ = false;
    rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    rt->get_table_partition()->Notify(rt);
    client->WaitForIdle();
    EXPECT_TRUE(route_state->exported_ == false);
    route_state->exported_ = true;

    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        if (rt_export_tmp[table_type])
            (rt_export_tmp[table_type])->Unregister();
    }
    //Delete route
    //delete vrf_state;
    DeleteBridgeRoute(agent_->local_vm_peer(), vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, notify_on_vrf_with_deleted_state_for_peer) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000,
             (L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_) != NULL));
    EvpnRouteEntry *rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    EXPECT_TRUE(route_state->exported_ == true);

    VrfEntry *vrf = VrfGet(vrf_name_.c_str());
    RouteExport *rt_export_tmp[Agent::ROUTE_TABLE_MAX];
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>
        (bgp_peer_ptr->GetVrfExportState(vrf->get_table_partition(), vrf));
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        rt_export_tmp[table_type] = vrf_state->rt_export_[table_type];
        rt->ClearState(rt->get_table(), rt_export_tmp[table_type]->GetListenerId());
    }

    //Stop BGP peer
    bgp_peer_ptr->GetAgentXmppChannel()->DeCommissionBgpPeer();
    //create another bgp peer
    bgp_peer_ptr->GetAgentXmppChannel()->CreateBgpPeer();
    //Mark Old route as not exported.
    rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    rt->get_table_partition()->Notify(rt);
    client->WaitForIdle();
    route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    EXPECT_TRUE(route_state == NULL);
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get()->GetAgentXmppChannel(),
                                                        xmps::NOT_READY);
    client->WaitForIdle();
    bgp_peer_ptr->DelPeerRoutes(
        boost::bind(&VNController::ControllerPeerHeadlessAgentDelDoneEnqueue,
                    agent_->controller(), bgp_peer_ptr));
    client->WaitForIdle();

    //Delete route
    DeleteBridgeRoute(agent_->local_vm_peer(), vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, delete_notify_on_multicast_rt_with_no_state) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    WAIT_FOR(1000, 1000,
             (L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_) != NULL));
    //Delete route local_vm_peer
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    //Delete state
    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    EXPECT_TRUE(route_state != NULL);
    VrfExport::State *vs = static_cast<VrfExport::State *>
        (bgp_peer_ptr->GetVrfExportState(agent_->vrf_table()->GetTablePartition(rt->vrf()),
                           rt->vrf())); 
    rt->ClearState(rt->get_table(),
                   vs->rt_export_[Agent::BRIDGE]->GetListenerId());
    delete route_state;
    //Now delete local peer from route
    BridgeAgentRouteTable::DeleteBroadcastReq(agent_->local_peer(),
                                              "vrf1", 1,
                                              Composite::L2COMP);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    bgp_peer.reset();
    client->WaitForIdle();
}

// Reset agentxmppchannel in active xmpp channel of agent.
// This results in ignoring any notification as channel is not active.
// Now delete VRF, but take a reference to hold it.
// Start a delpeer walk for Bgp peer of channel.
// Verify if state is deleted in VRF.
// Objective:
// Analogous to channel coming down however VRF delete notification coming later
// and delpeer walk running after that.
TEST_F(RouteTest, delpeer_walk_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();

    //Reset agent xmpp channel and back up the original channel.
    AgentXmppChannel *channel1 = agent_->controller_xmpp_channel(1);
    agent_->reset_controller_xmpp_channel(1);
    //Take VRF reference and delete VRF.
    VrfEntryRef vrf_ref = VrfGet("vrf1");
    DelVrf("vrf1");
    client->WaitForIdle();
    //Restore agent xmpp channel
    agent_->set_controller_xmpp_channel(channel1, 1);

    //Now bring channel down
    AgentXmppChannel::HandleAgentXmppClientChannelEvent(bgp_peer.get()->GetAgentXmppChannel(),
                                                        xmps::NOT_READY);
    client->WaitForIdle();
    //Release VRF reference
    vrf_ref.reset();
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
    bgp_peer.reset();
    client->WaitForIdle();
}

TEST_F(RouteTest, notify_walk_on_deleted_vrf_with_no_state_but_listener_id) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and keep a reference of same.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    DBTableBase::ListenerId bgp_peer_id =
        bgp_peer_ptr->GetVrfExportListenerId();
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();

    //Take VRF reference and delete VRF.
    VrfEntryRef vrf_ref = VrfGet("vrf1");
    BridgeRouteEntry *rt = L2RouteGet(vrf_name_, local_vm_mac_, local_vm_ip4_);
    EXPECT_TRUE(rt != NULL);
    DBState *state = new DBState();
    rt->SetState(rt->get_table(), DBEntryBase::ListenerId(100), state);

    bgp_peer_ptr->PeerNotifyRoutes();
    client->WaitForIdle();
    bgp_peer_ptr->SetVrfListenerId(100);
    bgp_peer_ptr->route_walker()->RouteWalkNotify(rt->get_table_partition(),
                                                  rt);
    client->WaitForIdle();
    bgp_peer_ptr->SetVrfListenerId(bgp_peer_id);

    rt->ClearState(rt->get_table(), DBEntryBase::ListenerId(100));
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelVrf("vrf1");
    client->WaitForIdle();
    //Release VRF reference
    vrf_ref.reset();
    bgp_peer.reset();
    client->WaitForIdle();
}

TEST_F(RouteTest, add_route_in_vrf_with_delayed_vn_vrf_link_add) {
    client->Reset();
    AddVrf("vrf1", 1);
    AddVn("vn1", 1, true);
    client->WaitForIdle();
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();

    //Delete VN
    VrfEntry *vrf = VrfGet("vrf1");
    //Add remote evpn route
    BridgeTunnelRouteAdd(bgp_peer_ptr, "vrf1", TunnelType::AllType(),
                         server1_ip_, MplsTable::kStartLabel,
                         remote_vm_mac_, remote_vm_ip4_, 32);
    WAIT_FOR(1000, 100,
             (L2RouteFind("vrf1", remote_vm_mac_, remote_vm_ip4_) == true));
    //Now add remote l3 route for IP.
    Inet4TunnelRouteAdd(bgp_peer_ptr, "vrf1", remote_vm_ip4_, 32, server1_ip_,
                        TunnelType::AllType(), MplsTable::kStartLabel, "vrf1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    InetUnicastRouteEntry *inet_rt = RouteGet("vrf1", remote_vm_ip4_, 32);
    VrfKSyncObject *vrf_obj = agent_->ksync()->vrf_ksync_obj();;
    DBTableBase::ListenerId vrf_listener_id = vrf_obj->vrf_listener_id();
    InetUnicastAgentRouteTable *vrf_uc_table =
        static_cast<InetUnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    VrfKSyncObject::VrfState *l3_state =
        static_cast<VrfKSyncObject::VrfState *>
        (vrf->GetState(vrf_uc_table, vrf_listener_id));
    RouteKSyncObject *vrf_rt_obj = l3_state->inet4_uc_route_table_;
    RouteKSyncEntry key(vrf_rt_obj, inet_rt);
    RouteKSyncEntry *ksync_route =
        static_cast<RouteKSyncEntry *>(vrf_rt_obj->GetReference(&key));
    EXPECT_FALSE(vrf_obj->RouteNeedsMacBinding(inet_rt));
    EXPECT_TRUE(ksync_route->mac() == MacAddress::ZeroMac());

    //Add VN
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(vrf->vn() != NULL);
    WAIT_FOR(1000, 1000, (ksync_route->mac() != MacAddress::ZeroMac()));
    EXPECT_TRUE(vrf_obj->RouteNeedsMacBinding(inet_rt));

    //Delete
    DeleteBridgeRoute(bgp_peer_ptr, vrf_name_, remote_vm_mac_,
                remote_vm_ip4_);
    WAIT_FOR(1000, 100,
             (L2RouteFind("vrf1", remote_vm_mac_, remote_vm_ip4_) == false));
    DeleteRoute("vrf1", remote_vm_ip4_str_, 32, bgp_peer_ptr);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, deleted_peer_walk_on_deleted_vrf) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntry *vrf1 = VrfGet("vrf1");
    //Add a peer
    BgpPeer *bgp_peer = CreateBgpPeer(Ip4Address(1), "BGP Peer1");

    DBTableBase *table = Agent::GetInstance()->vrf_table();
    DBTableBase::ListenerId l_id =
        table->Register(boost::bind(&TestL2RouteVrfNotify, _1, _2));

    TestL2RouteState tmp_state;
    // Hold VRF by Adding a state to it, this makes sure that refcount
    // on this VRF drops to zero but it is still held by state
    // so that VRF Walk Notify can walk through deleted VRF
    vrf1->SetState(table, l_id, &tmp_state);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    bgp_peer->route_walker()->set_type(ControllerRouteWalker::DELPEER);
    bgp_peer->route_walker()->VrfWalkNotify(vrf1->get_table_partition(), vrf1);
    DeleteBgpPeer(bgp_peer);
    vrf1->ClearState(table, l_id);
    table->Unregister(l_id);
    client->WaitForIdle();
}

// Bug# 1508894
TEST_F(RouteTest, add_stale_non_stale_path_in_l2_mcast_and_delete_non_stale) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and keep a reference of same.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    BgpPeer *bgp_peer_ptr_2 = CreateBgpPeer(Ip4Address(2), "BGP Peer2");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    PeerConstPtr bgp_peer_2 =
        bgp_peer_ptr_2->GetAgentXmppChannel()->bgp_peer_id_ref();

    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    //Send explicit evpn olist
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    mc_handler->ModifyEvpnMembers(bgp_peer_ptr,
                                 "vrf1",
                                 olist,
                                 0,
                                 1);
    client->WaitForIdle();
    mc_handler->ModifyEvpnMembers(bgp_peer_ptr_2,
                                 "vrf1",
                                 olist,
                                 0,
                                 1);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt != NULL);

    AgentPath *path = rt->FindPath(bgp_peer_ptr_2);
    path->set_is_stale(true);

    Route::PathList::iterator it = rt->GetPathList().begin();
    while (it != rt->GetPathList().end()) {
        AgentPath *it_path = static_cast<AgentPath *>(it.operator->());
        EXPECT_TRUE(it_path != NULL);
        it++;
    }
    //Delete Evpn path
    mc_handler->ModifyEvpnMembers(bgp_peer_ptr,
                                 "vrf1",
                                 olist,
                                 0,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    //Delete all
    DeleteBgpPeer(bgp_peer.get());
    DeleteBgpPeer(bgp_peer_2.get());
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    bgp_peer.reset();
    bgp_peer_2.reset();
}

TEST_F(RouteTest, multiple_peer_evpn_label_check) {
    client->Reset();
    AddVrf("vrf1");
    AddVn("vn1", 1);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    agent_->mpls_table()->ReserveMulticastLabel(4000, 5000, 0);
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::MplsType()));
    mc_handler->ModifyTorMembers(bgp_peer_ptr,
                                 "vrf1",
                                 olist,
                                 10,
                                 1);
    client->WaitForIdle();

    mc_handler->ModifyFabricMembers(Agent::GetInstance()->
                                    multicast_tree_builder_peer(),
                                    "vrf1",
                                    IpAddress::from_string("255.255.255.255").to_v4(),
                                    IpAddress::from_string("0.0.0.0").to_v4(),
                                    4100, olist, 1);
    client->WaitForIdle();
    //MulticastGroupObject *obj =
    //    mc_handler->FindFloodGroupObject("vrf1");
    //uint32_t evpn_label = obj->evpn_mpls_label();
    //EXPECT_FALSE(FindMplsLabel(evpn_label));

    //Delete remote paths
    mc_handler->ModifyFabricMembers(Agent::GetInstance()->
                                    multicast_tree_builder_peer(),
                                    "vrf1",
                                    IpAddress::from_string("255.255.255.255").to_v4(),
                                    IpAddress::from_string("0.0.0.0").to_v4(),
                                    4100, olist,
                                    ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    TunnelOlist del_olist;
    mc_handler->ModifyTorMembers(bgp_peer_ptr,
                                 "vrf1",
                                 del_olist,
                                 10,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    mc_handler->ModifyTorMembers(bgp_peer_ptr,
                                 "vrf1",
                                 olist,
                                 10,
                                 1);
    client->WaitForIdle();
    mc_handler->ModifyTorMembers(bgp_peer_ptr,
                                 "vrf1",
                                 del_olist,
                                 10,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();

    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
}

// Bug# 1529665 
TEST_F(RouteTest, evpn_mcast_label_check_with_no_vm) {
    client->Reset();
    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };
    AddVrf("vrf1", 1);
    AddVn("vn1", 1, true);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    RouteExport::State *route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    //EXPECT_TRUE(route_state->label_ != 0);

    autogen::EnetItemType item;
    stringstream ss_node;
    AgentXmppChannel *channel1 = agent_->controller_xmpp_channel(1);
    SecurityGroupList sg;
    CommunityList communities;
    channel1->BuildEvpnMulticastMessage(item,
                                        ss_node,
                                        rt,
                                        agent_->router_ip_ptr(),
                                        "vn1",
                                        &sg,
                                        &communities,
                                        route_state->label_,
                                        TunnelType::AllType(),
                                        true,
                                        rt->FindPath(agent_->multicast_peer()),
                                        false);
    autogen::EnetNextHopType nh = item.entry.next_hops.next_hop.back();
    EXPECT_TRUE(nh.label != 0);
    EXPECT_TRUE(item.entry.nlri.ethernet_tag != 0);
    EXPECT_TRUE(item.entry.nlri.ethernet_tag == nh.label);
    //Add VM
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    route_state = static_cast<RouteExport::State *>
        (bgp_peer_ptr->GetRouteExportState(rt->get_table_partition(),
                                        rt));
    EXPECT_TRUE(route_state->label_ != 0);

    //Delete all
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, add_local_peer_and_then_vm) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    client->WaitForIdle();
    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt->FindPath(agent_->multicast_peer()));

    MulticastGroupObject *obj =
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj->vn() != NULL);
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    obj =
        MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
    EXPECT_TRUE(obj != NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
}

TEST_F(RouteTest, l2_flood_vxlan_update) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    VnAddReq(1, "vn1");
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and enqueue path add in multicast route.
    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt->FindPath(agent_->multicast_peer()));
    uint32_t vxlan_id = rt->FindPath(agent_->multicast_peer())->vxlan_id();
    EXPECT_TRUE(vxlan_id != 0);

    VxLanNetworkIdentifierMode(true);
    client->WaitForIdle();

    uint32_t new_vxlan_id = rt->FindPath(agent_->multicast_peer())->vxlan_id();
    EXPECT_TRUE(vxlan_id != new_vxlan_id);

    VxLanNetworkIdentifierMode(false);
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

//Bug# 1562961
TEST_F(RouteTest, StalePathDeleteRouteDelete) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    //Add a peer and keep a reference of same.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();

    BridgeTunnelRouteAdd(bgp_peer_ptr, vrf_name_, TunnelType::MplsType(),
                         server1_ip_, MplsTable::kStartLabel + 10,
                         remote_vm_mac_, remote_vm_ip4_, 32);
    client->WaitForIdle();

    EvpnRouteEntry *rt = EvpnRouteGet(vrf_name_, remote_vm_mac_,
                                        remote_vm_ip4_, 0);
    EXPECT_TRUE(rt != NULL);
    AgentPath *path = rt->FindPath(bgp_peer_ptr);
    path->set_is_stale(true);
    DelVrf(vrf_name_.c_str());
    client->WaitForIdle();
    rt = EvpnRouteGet(vrf_name_, remote_vm_mac_,
                    remote_vm_ip4_, 0);
    EXPECT_TRUE(rt == NULL);

    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    //Release VRF reference
    bgp_peer.reset();
    client->WaitForIdle();
}

class SetupTask : public Task {
    public:
        SetupTask(RouteTest *test, std::string name) :
            Task((TaskScheduler::GetInstance()->
                  GetTaskId("db::DBTable")), 0), test_(test),
            test_name_(name) {
        }

        virtual bool Run() {
            if (test_name_ == "SquashPathTest_1") {
                char local_vm_mac_str_[100];
                MacAddress  local_vm_mac_;
                Ip4Address  local_vm_ip4_;
                char local_vm_ip4_str_[100];
                strcpy(local_vm_ip4_str_, "1.1.1.10");
                local_vm_ip4_ = Ip4Address::from_string(local_vm_ip4_str_);
                strcpy(local_vm_mac_str_, "00:00:01:01:01:10");
                local_vm_mac_ = MacAddress::FromString(local_vm_mac_str_);
                EvpnRouteEntry *rt = EvpnRouteGet("vrf1",
                                                  local_vm_mac_,
                                                  local_vm_ip4_,
                                                  0);
                const VrfEntry *vrf = VrfGet("vrf1");
                vrf->GetEvpnRouteTable()->SquashStalePaths(rt, NULL);
            }
            return true;
        }
    std::string Description() const { return "SetupTask"; }
    private:
        RouteTest *test_;
        std::string test_name_;
};

//Bug# 1571598 
TEST_F(RouteTest, SquashPathTest_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EvpnRouteEntry *rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActivePath() != NULL);
    SetupTask * task = new SetupTask(this, "SquashPathTest_1");
    TaskScheduler::GetInstance()->Enqueue(task);
    client->WaitForIdle();
    rt = EvpnRouteGet("vrf1", local_vm_mac_, local_vm_ip4_, 0);
    EXPECT_TRUE(rt != NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

//Bug# 1580733
TEST_F(RouteTest, label_in_evpn_mcast_path) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    //Add VM
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    BridgeRouteEntry *rt = L2RouteGet("vrf1",
                                      MacAddress::FromString("00:00:00:01:01:01"),
                                      Ip4Address(0));
    EXPECT_TRUE(rt != NULL);
    uint32_t mpls_label = rt->GetActivePath()->label();
    //Add a peer and enqueue path add in multicast route.
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");
    PeerConstPtr bgp_peer =
        bgp_peer_ptr->GetAgentXmppChannel()->bgp_peer_id_ref();
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent_->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    olist.push_back(OlistTunnelEntry(nil_uuid(), 10,
                                     IpAddress::from_string("8.8.8.8").to_v4(),
                                     TunnelType::VxlanType()));
    //Add EVPN olist
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 olist,
                                 mpls_label,
                                 1);
    client->WaitForIdle();
    agent_->oper_db()->multicast()->DeleteBroadcast(agent_->local_vm_peer(), "vrf1",
                    0, Composite::L2INTERFACE);
    BridgeAgentRouteTable::DeleteBroadcastReq(agent_->local_peer(),
                                              "vrf1",
                                              0,
                                              Composite::L2COMP);
    client->WaitForIdle();
    rt = L2RouteGet("vrf1",
                    MacAddress::FromString("ff:ff:ff:ff:ff:ff"),
                    Ip4Address(0));
    EXPECT_TRUE(rt != NULL);

    //Delete VM
    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    //Delete EVPN
    TunnelOlist del_olist;
    mc_handler->ModifyEvpnMembers(bgp_peer.get(),
                                 "vrf1",
                                 del_olist,
                                 mpls_label,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer.get());
    client->WaitForIdle();
    bgp_peer.reset();
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
