/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/path_preference.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

#define NULL_VRF ""
#define ZERO_IP "0.0.0.0"

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
    {"2.2.2.0", 24, "2.2.2.10"},
    {"10.1.1.0", 24, "10.1.1.10"}
};

IpamInfo fabric_ipam_info[] = {
    {"10.1.1.0", 24, "10.1.1.254", true}
};

class FabricVmiTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        client->WaitForIdle();
        peer_ = CreateBgpPeer(Ip4Address(1), "BGP Peer 1");
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 2);
        AddIPAM("vn2", ipam_info, 2);
        client->WaitForIdle();
        AddVn(agent->fabric_vn_name().c_str(), 100);
        AddVrf(agent->fabric_policy_vrf_name().c_str(), 100);
        AddLink("virtual-network", agent->fabric_vn_name().c_str(),
                "routing-instance", agent->fabric_policy_vrf_name().c_str());
        client->WaitForIdle();
        AddIPAM(agent->fabric_vn_name().c_str(), fabric_ipam_info, 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelIPAM("vn1");
        DelIPAM(agent->fabric_vn_name().c_str());
        DelIPAM("vn2");
        DelNode("virtual-network", agent->fabric_vn_name().c_str());
        DelLink("virtual-network", agent->fabric_vn_name().c_str(),
                "routing-instance", agent->fabric_policy_vrf_name().c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == 2U));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
        DeleteBgpPeer(peer_);
    }

    Agent *agent;
    BgpPeer *peer_;
};

TEST_F(FabricVmiTest, Vhost) {
    Ip4Address ip = Ip4Address::from_string("10.1.1.1");

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 32));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_policy_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);

    const VmInterface *vhost =
        static_cast<const VmInterface *>(agent->vhost_interface());
    EXPECT_TRUE(vhost->policy_enabled() == false);
}

TEST_F(FabricVmiTest, basic_1) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE |
                                 1 << TunnelType::NATIVE);
    Inet4TunnelRouteAdd(peer_,
                        agent->fabric_policy_vrf_name().c_str(),
                        ip, 32, server_ip, bmap, 16, "vn1",
                        SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->gw_ip() == server_ip);

    DeleteRoute(agent->fabric_policy_vrf_name().c_str(), "1.1.1.1", 32,
                peer_);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, basic_2) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vm_intf->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name());
    const InterfaceNH *intf_nh =
        dynamic_cast<const InterfaceNH *>(vm_intf->flow_key_nh());
    EXPECT_TRUE(intf_nh->GetVrf()->GetName() == agent->fabric_vrf_name());
    EXPECT_TRUE(vm_intf->proxy_arp_mode() == VmInterface::PROXY_ARP_UNRESTRICTED);

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(rt->GetActivePath()->peer() == vm_intf->peer());
    EXPECT_TRUE(rt->GetActivePath()->native_vrf_id() ==
                (uint32_t)(VrfGet("vrf1")->rd()));
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) != 0);

    agent->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vm_intf->id(),
                           vm_intf->forwarding_vrf()->vrf_id(),
                           vm_intf->vm_mac());
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->path_preference().preference() ==
                PathPreference::HIGH);
    rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->path_preference().preference() ==
                PathPreference::HIGH);

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf() == NULL);

    EXPECT_TRUE(vm_intf->forwarding_vrf()->GetName() == "vrf1");
    EXPECT_TRUE(intf_nh->GetVrf()->GetName() == "vrf1");

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    client->WaitForIdle();
}

//Export route in ip-fabric VRF with no forwarding VRF
//Verify that NH is published with local peer and not
//route export peer
TEST_F(FabricVmiTest, basic_3) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    VnListType vn_list;
    agent->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer_,
            agent->fabric_policy_vrf_name(), ip, 32, MakeUuid(2),
            vn_list, 10, SecurityGroupList(),
            TagList(), CommunityList(), false, PathPreference(),
            Ip4Address(0), EcmpLoadBalance(), false, false, true);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(2));
    EXPECT_TRUE(vm_intf->forwarding_vrf()->GetName() == "vrf1");

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(rt->GetActivePath()->peer() == agent->local_peer());

    //Add a local peer route
    agent->fabric_inet4_unicast_table()->AddLocalVmRouteReq(vm_intf->peer(),
            agent->fabric_policy_vrf_name(), ip, 32, MakeUuid(2),
            vn_list, 10, SecurityGroupList(),
            TagList(), CommunityList(), false, PathPreference(),
            Ip4Address(0), EcmpLoadBalance(), false, false, true);
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActivePath()->peer() == vm_intf->peer());

    //Delete local peer path route should we with local Peer now
    agent->fabric_inet4_unicast_table()->DeleteReq(vm_intf->peer(),
            agent->fabric_policy_vrf_name(), ip, 32, NULL);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActivePath()->peer() == agent->local_peer());

    agent->fabric_inet4_unicast_table()->DeleteReq(peer_,
            agent->fabric_policy_vrf_name(), ip, 32, NULL);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

//Add gateway if tunnel nh and addr mismatch
TEST_F(FabricVmiTest, GatewayRoute) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.254");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE |
                                 1 << TunnelType::NATIVE);
    Inet4TunnelRouteAdd(peer_,
                        agent->fabric_policy_vrf_name().c_str(),
                        ip, 32, ip, bmap, 16, "vn1",
                        SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->gw_ip() == server_ip);

    DeleteRoute(agent->fabric_policy_vrf_name().c_str(), "1.1.1.1", 32,
                peer_);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

//Verify FIP route gets exported with Native encap, if FIP VN
//is using fabric for forwarding
TEST_F(FabricVmiTest, FIP_Native_Encap) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    AddVn("default-project:vn2", 10);
    AddVrf("default-project:vn2:vn2");
    AddLink("virtual-network", "default-project:vn2",
            "routing-instance", "default-project:vn2:vn2");
    client->WaitForIdle();

    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine-interface", "intf2", "floating-ip", "fip1");
    client->WaitForIdle();

    AddLink("virtual-network", "default-project:vn2", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(VrfGet("default-project:vn2:vn2")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());

    Ip4Address ip = Ip4Address::from_string("2.1.1.100");
    //Route should be leaked to fabric VRF
    WAIT_FOR(1000, 1000, RouteFind(agent->fabric_vrf_name(), ip, 32));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet("default-project:vn2:vn2", ip, 32);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) != 0);

    DelLink("virtual-network", "default-project:vn2", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, RouteFind(agent->fabric_vrf_name(), ip, 32) == false);

    DelFloatingIpPool("fip-pool1");
    DelFloatingIp("fip1");
    DelLink("virtual-network", "default-project:vn2",
            "routing-instance", "default-project:vn2:vn2");
    DelLink("virtual-machine-interface", "intf2", "floating-ip", "fip1");
    DelVn("default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, PolicyVrfDelete) {
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet(agent->fabric_policy_vrf_name().c_str())->
                forwarding_vrf()->GetName() == agent->fabric_vrf_name().c_str());
}

//Add a route in default vrf via fabric peer
//and another path via local peer(route leaking scenario)
//Verify that both path are added and deleted appropriatly
TEST_F(FabricVmiTest, FabricAndLocalPeerRoute) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(2));

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");

    AddLocalVmRoute(agent, agent->fabric_policy_vrf_name(), "1.1.1.1",
                    32, "vn1", 2, peer_);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->FindPath(agent->local_peer()) != NULL);
    EXPECT_TRUE(rt->FindPath(vm_intf->peer()) != NULL);

    DeleteRoute(agent->fabric_policy_vrf_name().c_str(), "1.1.1.1", 32,
                peer_);
    client->WaitForIdle();
    EXPECT_TRUE(rt->FindPath(agent->local_peer()) == NULL);
    EXPECT_TRUE(rt->FindPath(vm_intf->peer()) != NULL);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, v6) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
        {"fd11::", 96, "fd11::1"},
    };

    CreateV6VmportEnv(input, 1, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) == 0);

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, DefaultRoute) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("0.0.0.0");
    //Route should be leaked to fabric VRF
    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 0));

    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet("vrf1", ip, 0);
    EXPECT_TRUE(rt->FindPath(agent->local_peer()) != NULL);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                TunnelType::NativeType()) != 0);

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    rt = RouteGet("vrf1", ip, 0);
    EXPECT_TRUE(rt == NULL);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, NonOverlayRoute) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.3");

    PathPreference path_preference(1, PathPreference::LOW, false, false);
    TunnelType::TypeBmap bmap = (1 << TunnelType::MPLS_GRE);
    Inet4TunnelRouteAdd(peer_,
                        agent->fabric_policy_vrf_name().c_str(),
                        ip, 32, server_ip, bmap, 16, "vn1",
                        SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    //Route should be leaked to fabric VRF
    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    bmap = (1 << TunnelType::MPLS_GRE | 1 << TunnelType::NATIVE);
    Inet4TunnelRouteAdd(peer_, agent->fabric_policy_vrf_name().c_str(),
                       ip, 32, server_ip, bmap, 16, "vn1",
                       SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    //Verify that nexthop is ARP nexthop fpr server_ip
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);
    EXPECT_TRUE(rt->GetActivePath()->gw_ip() == server_ip);

    bmap = (1 << TunnelType::MPLS_GRE);
    Inet4TunnelRouteAdd(peer_, agent->fabric_policy_vrf_name().c_str(),
                        ip, 32, server_ip, bmap, 16, "vn1",
                        SecurityGroupList(), TagList(), path_preference);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));

    DeleteRoute(agent->fabric_policy_vrf_name().c_str(), "1.1.1.1", 32,
                peer_);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    DelVrf(agent->fabric_policy_vrf_name().c_str());
    client->WaitForIdle();
}

//Subnet and default route in fabric VRF should not be
//overwritten
TEST_F(FabricVmiTest, IntfStaticRoute) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));

   //Add a static route
   struct TestIp4Prefix static_route[] = {
       { Ip4Address::from_string("0.0.0.0"), 0},
       { Ip4Address::from_string("10.1.1.0"), 24},
   };

   AddInterfaceRouteTable("static_route", 1, static_route, 2);
   AddLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();

   InetUnicastRouteEntry *rt = RouteGet(agent->fabric_vrf_name(),
                                        static_route[0].addr_,
                                        static_route[0].plen_);
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);

   rt = RouteGet(agent->fabric_vrf_name(),
                 static_route[1].addr_,
                 static_route[1].plen_);
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);

   //Delete the link between interface and route table
   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   client->WaitForIdle();

   rt = RouteGet(agent->fabric_vrf_name(),
                 static_route[0].addr_,
                 static_route[0].plen_);
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::ARP);

   rt = RouteGet(agent->fabric_vrf_name(),
           static_route[1].addr_,
           static_route[1].plen_);
   EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE);

   DelLink("virtual-machine-interface", "vnet1",
           "interface-route-table", "static_route");
   DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
   DeleteVmportEnv(input, 1, true);
   client->WaitForIdle();

   EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(),
                         static_route[0].addr_,
                         static_route[0].plen_));
   EXPECT_TRUE(RouteFind(agent->fabric_vrf_name(),
                         static_route[1].addr_,
                         static_route[1].plen_));
   EXPECT_FALSE(VmPortFind(1));
}

TEST_F(FabricVmiTest, GwRoute) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 2);
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip4Address addr = Ip4Address::from_string("1.1.1.10", ec);
    InetUnicastRouteEntry* rt = RouteGet(agent->fabric_vrf_name(), addr, 32);
    EXPECT_TRUE(rt != NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    rt = RouteGet(agent->fabric_vrf_name(), addr, 32);
    EXPECT_TRUE(rt == NULL);

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, VmWithVhostIp) {
    struct PortInfo input[] = {
        {"vnet1", 1, "10.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    boost::system::error_code ec;
    Ip4Address addr = Ip4Address::from_string("10.1.1.1", ec);
    InetUnicastRouteEntry* rt = RouteGet(agent->fabric_vrf_name(), addr, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    rt = RouteGet(agent->fabric_vrf_name(), addr, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::RECEIVE);

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
}

TEST_F(FabricVmiTest, default_route) {
    Ip4Address ip(0);
    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 0));
    InetUnicastRouteEntry *rt = RouteGet(agent->fabric_policy_vrf_name(), ip, 0);
    EXPECT_TRUE(rt->GetActivePath()->tunnel_bmap() & TunnelType::NativeType());

    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();

    EXPECT_TRUE(RouteFind(agent->fabric_policy_vrf_name(), ip, 0));
    EXPECT_TRUE(rt->GetActivePath()->tunnel_bmap() & TunnelType::NativeType());
}

//Add 2 VMI with same IP address verify ECMP peer
//creates a path with both nexthop
TEST_F(FabricVmiTest, Ecmp1) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
        {"intf3", 3, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());
    const VmInterface *vm_intf1 = static_cast<const VmInterface *>(VmPortGet(2));
    const VmInterface *vm_intf2 = static_cast<const VmInterface *>(VmPortGet(3));

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry*>(RouteGet(agent->fabric_vrf_name(),
                                                     ip, 32));
    EXPECT_TRUE(rt->FindPath(vm_intf1->peer()) != NULL);
    EXPECT_TRUE(rt->FindPath(vm_intf2->peer()) != NULL);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) != 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf() == NULL);
    EXPECT_TRUE(RouteGet(agent->fabric_vrf_name(), ip, 32) == NULL);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    client->WaitForIdle();
}

//Add same IP address in 2 different virtual network
//Verify ECMP path gets created for the same
TEST_F(FabricVmiTest, Ecmp2) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
        {"intf3", 3, "1.1.1.1", "00:00:00:01:01:01", 2, 2},
    };
    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());
    const VmInterface *vm_intf1 = static_cast<const VmInterface *>(VmPortGet(2));
    const VmInterface *vm_intf2 = static_cast<const VmInterface *>(VmPortGet(3));

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry*>(RouteGet(agent->fabric_vrf_name(),
                                                     ip, 32));
    EXPECT_TRUE(rt->FindPath(vm_intf1->peer()) != NULL);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) != 0);

    AddLink("virtual-network", "vn2", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(rt->FindPath(vm_intf1->peer()) != NULL);
    EXPECT_TRUE(rt->FindPath(vm_intf2->peer()) != NULL);
    EXPECT_TRUE((rt->GetActivePath()->tunnel_bmap() &
                 TunnelType::NativeType()) != 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);


    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(rt->FindPath(vm_intf1->peer()) == NULL);
    EXPECT_TRUE(rt->FindPath(vm_intf2->peer()) != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    DelLink("virtual-network", "vn2", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet(agent->fabric_vrf_name(), ip, 32) == NULL);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    client->WaitForIdle();
}

//Add a ECMP path
//Add a BGP path
//Verify that BGP path added has local ECMP path also
TEST_F(FabricVmiTest, Ecmp3) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
        {"intf3", 3, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
        InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry*>(RouteGet(agent->fabric_vrf_name(),
                    ip, 32));

    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    Ip4Address remote_address = Ip4Address::from_string("10.10.10.100");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent->fabric_vrf_name(),
                agent->router_id(), remote_address, false, TunnelType::NativeType()));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(peer_, agent->fabric_vrf_name(), ip, 32,
                       comp_nh_list, -1, "vn1",
                       sg_list, TagList(), PathPreference(), true);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    agent->fabric_inet4_unicast_table()->DeleteReq(peer_,
           agent->fabric_vrf_name().c_str(), ip, 32,
           new ControllerVmRoute(peer_));
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf() == NULL);
    EXPECT_TRUE(RouteGet(agent->fabric_vrf_name(), ip, 32) == NULL);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    client->WaitForIdle();
}

//Add one local VM path
//Add a BGP path verify BGP path is ECMP with right no. of nexthop
//Add one more local path, verify BGP path now has 3 path
//Delete one local VM path and verify same gets reflected in
//BGP path also
TEST_F(FabricVmiTest, Ecmp4) {
    struct PortInfo input1[] = {
        {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
        {"intf3", 3, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };
    CreateVmportWithEcmp(input1, 1);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
        InetUnicastRouteEntry *rt =
        static_cast<InetUnicastRouteEntry*>(RouteGet(agent->fabric_vrf_name(),
                    ip, 32));

    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf()->GetName() ==
                agent->fabric_vrf_name().c_str());
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() != NextHop::COMPOSITE);

    Ip4Address remote_address = Ip4Address::from_string("10.10.10.100");
    ComponentNHKeyPtr nh_data1(new ComponentNHKey(30, agent->fabric_vrf_name(),
                agent->router_id(), remote_address, false, TunnelType::NativeType()));
    ComponentNHKeyList comp_nh_list;
    //Insert new NH first and then existing route NH
    comp_nh_list.push_back(nh_data1);

    SecurityGroupList sg_list;
    EcmpTunnelRouteAdd(peer_, agent->fabric_vrf_name(), ip, 32,
                       comp_nh_list, -1, "vn1",
                       sg_list, TagList(), PathPreference(), true);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ActiveComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->GetNH(0) == NULL);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == true);


    Ip4Address aap_ip = Ip4Address::from_string("10.10.10.10");
    MacAddress mac("0a:0b:0c:0d:0e:0f");
    AddAapWithMacAndDisablePolicy("intf3", 3, aap_ip, mac.ToString(), true);
    client->WaitForIdle();

    comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->ActiveComponentNHCount() == 2);
    EXPECT_TRUE(comp_nh->GetNH(0) == NULL);
    EXPECT_TRUE(comp_nh->PolicyEnabled() == false);


    agent->fabric_inet4_unicast_table()->DeleteReq(peer_,
            agent->fabric_vrf_name().c_str(), ip, 32,
            new ControllerVmRoute(peer_));
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "virtual-network",
            agent->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(VrfGet("vrf1")->forwarding_vrf() == NULL);
    EXPECT_TRUE(RouteGet(agent->fabric_vrf_name(), ip, 32) == NULL);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();

    EXPECT_FALSE(RouteFind(agent->fabric_vrf_name(), ip, 32));
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, false, false, false);
    int ret = RUN_ALL_TESTS();
    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;
    return ret;
}
