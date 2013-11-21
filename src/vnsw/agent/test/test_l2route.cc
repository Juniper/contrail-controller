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
#include "oper/interface.h"
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
#include "test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h> 

std::string eth_itf;

void RouterIdDepInit() {
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

        if (Agent::GetInstance()->GetRouterIdConfigured()) {
            vhost_ip_ = Agent::GetInstance()->GetRouterId();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        server1_ip_ = Ip4Address::from_string("10.1.1.11");
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        local_vm_mac_ = ether_aton("00:00:01:01:01:10");
        remote_vm_mac_ = ether_aton("00:00:01:01:01:11");
    }

    virtual void SetUp() {
        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        EthInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                                eth_name_, 
                                Agent::GetInstance()->GetDefaultVrf());
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

    void AddRemoteVmRoute(struct ether_addr *remote_vm_mac, 
                          const Ip4Address &server_ip, 
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any toher peer than localvmpeer

        Layer2AgentRouteTable::AddRemoteVmRoute(
            Agent::GetInstance()->GetLocalPeer(), vrf_name_,
            bmap, server_ip, label, *remote_vm_mac, local_vm_ip_, 32);
        client->WaitForIdle();
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        Agent::GetInstance()->
            GetDefaultInet4UnicastRouteTable()->AddResolveRoute(
                Agent::GetInstance()->GetDefaultVrf(), server_ip, plen);
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name, 
                     struct ether_addr *remote_vm_mac) {
        Layer2AgentRouteTable::DeleteReq(peer, vrf_name_,
            *remote_vm_mac);
        client->WaitForIdle();
        while (L2RouteFind(vrf_name, *remote_vm_mac) == true) {
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
    struct ether_addr *local_vm_mac_;
    struct ether_addr *remote_vm_mac_;
    static TunnelType::Type type_;
};
TunnelType::Type RouteTest::type_;

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

TEST_F(RouteTest, LocalVmRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    EnableEvpn();
    client->WaitForIdle();
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, *local_vm_mac_));
    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, *local_vm_mac_);
    EXPECT_TRUE(rt->GetActiveNextHop() != NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(rt->GetDestVnName() == "vn1");
    uint32_t label = rt->GetMplsLabel();
    MplsLabelKey key(MplsLabel::MCAST_NH, label);
    MplsLabel *mpls = 
        static_cast<MplsLabel *>(Agent::GetInstance()->
                                 GetMplsTable()->Find(&key, true));

    EXPECT_TRUE(mpls->GetNextHop() == nh);
    const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
    EXPECT_TRUE(intf_nh->GetFlags() == InterfaceNHFlags::LAYER2);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(L2RouteFind(vrf_name_, *local_vm_mac_) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
    DisableEvpn();
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_1) {
    client->Reset();
    EnableEvpn();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();

    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::MPLS_GRE;
    AddRemoteVmRoute(remote_vm_mac_, server1_ip_, MplsTable::kStartLabel, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, *remote_vm_mac_));

    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, *remote_vm_mac_);
    EXPECT_TRUE(rt->GetMplsLabel() == MplsTable::kStartLabel);

    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh); 
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteRoute(Agent::GetInstance()->GetLocalPeer(), 
                vrf_name_, remote_vm_mac_);
    client->WaitForIdle();
    DisableEvpn();
    DelEncapList();
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_VxLan_1) {
    struct PortInfo input[] = {
        {"vnet2", 2, "2.2.2.10", "00:00:02:02:02:10", 2, 2},
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    RouteTest::SetTunnelType(TunnelType::VXLAN);
    AddEncapList("VXLAN", "MPLSoGRE", "MPLSoUDP");
    EnableEvpn();
    client->WaitForIdle();
    CreateVmportEnv(input, 2);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(L2RouteFind(vrf_name_, *local_vm_mac_));

    struct ether_addr *vxlan_vm_mac = ether_aton("00:00:01:01:01:10");
    Layer2RouteEntry *vnet1_rt = L2RouteGet(vrf_name_, *vxlan_vm_mac);
    const NextHop *vnet1_nh = vnet1_rt->GetActiveNextHop();
    EXPECT_TRUE(vnet1_nh->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetLabel() == 1);
    EXPECT_TRUE(vnet1_rt->GetActivePath()->GetTunnelType() == 
                TunnelType::VXLAN);

    vxlan_vm_mac = ether_aton("00:00:02:02:02:22");
    TunnelType::TypeBmap bmap;
    bmap = 1 << TunnelType::VXLAN;
    AddRemoteVmRoute(vxlan_vm_mac, server1_ip_, 1, bmap);
    EXPECT_TRUE(L2RouteFind(vrf_name_, *vxlan_vm_mac));

    Layer2RouteEntry *rt = L2RouteGet(vrf_name_, *vxlan_vm_mac);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() == NextHop::TUNNEL);
    const TunnelNH *tnh = ((const TunnelNH *)nh); 
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::VXLAN);
    EXPECT_TRUE(rt->GetActivePath()->GetLabel() == 1);
    EXPECT_TRUE(rt->GetActivePath()->GetTunnelType() == 
                TunnelType::VXLAN);
    EXPECT_TRUE(tnh->GetDip()->to_string() == server1_ip_.to_string());

    DeleteRoute(Agent::GetInstance()->GetLocalPeer(), vrf_name_, vxlan_vm_mac);
    client->WaitForIdle();

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    DisableEvpn();
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    if (vm.count("config")) {
        eth_itf = Agent::GetInstance()->GetIpFabricItfName();
    } else {
        eth_itf = "eth0";
    }

    RouteTest::SetTunnelType(TunnelType::MPLS_GRE);
    int ret = RUN_ALL_TESTS();
    RouteTest::SetTunnelType(TunnelType::MPLS_UDP);
    return ret + RUN_ALL_TESTS();
}
