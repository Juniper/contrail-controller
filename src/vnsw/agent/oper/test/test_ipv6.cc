/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
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

#include <boost/uuid/string_generator.hpp>

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
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "test/test_init.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h> 
#include <ksync/ksync_sock_user.h> 
#include <boost/assign/list_of.hpp>
#include "oper/path_preference.h"
#include "services/icmpv6_proto.h"

void RouterIdDepInit(Agent *agent) {
}

class Ipv6Test : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        intf_count_ = agent_->interface_table()->Size();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
        WAIT_FOR(100, 1000, (agent_->interface_table()->Size() == intf_count_));
        WAIT_FOR(100, 1000, (agent_->vrf_table()->Size() == 1U));
        WAIT_FOR(100, 1000, (agent_->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent_->vn_table()->Size() == 0U));
    }

    int intf_count_;
    Agent *agent_;
};

/* Create a VM interface with both v4 and v6 IP addresses
 * Verify that interface is both v4 and v6 active.
 */
TEST_F(Ipv6Test, v4v6ip_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == true);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
}

/* Create a VM interface with only v6 IP address
 * Verify that interface is only v6 active.
 */
TEST_F(Ipv6Test, v6ip_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0, NULL, NULL, false);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, false, true);
    client->WaitForIdle();
}

/* Create a VM interface with both v4 and v6 IP addresses
 * Verify v6 unicast route
 */
TEST_F(Ipv6Test, v4v6ip_2) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == true);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);
    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
}

/* Create a VM interface with only v6 IP address
 * Verify v6 unicast route
 */
TEST_F(Ipv6Test, v6ip_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0, NULL, NULL, false);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == false);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    boost::system::error_code ec;
    Ip6Address addr = Ip6Address::from_string(input[0].ip6addr, ec);
    InetUnicastRouteEntry* rt = RouteGetV6("vrf1", addr, 128);
    EXPECT_TRUE(rt != NULL);

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, false, true);
    client->WaitForIdle();
}

/* Create a VM interface with both v4 and v6 IP addresses
 * Create V4 and V6 subnets and it to default-network-ipam
 * Associate the VN with default-network-ipam
 * Verify Subnet and Gateway routes and their nexthop types
 */
TEST_F(Ipv6Test, v6_subnet_gw_route) {
    struct PortInfo input[] = {
        {"vnet1", 1, "0.0.0.0", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"fd11::", 120, "fd11::1", true},
        {"fd12::", 96, "fd12::1", true},
    };
    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();

    //Verify subnet routes are present
    boost::system::error_code ec;
    Ip4Address v4_subnet1 = Ip4Address::from_string(ipam_info[0].ip_prefix, ec);
    Ip6Address v6_subnet1 = Ip6Address::from_string(ipam_info[1].ip_prefix, ec);
    Ip6Address v6_subnet2 = Ip6Address::from_string(ipam_info[2].ip_prefix, ec);

    InetUnicastRouteEntry *rt1 = RouteGet("vrf1", v4_subnet1, 24);
    InetUnicastRouteEntry *rt2 = RouteGetV6("vrf1", v6_subnet1, 120);
    InetUnicastRouteEntry *rt3 = RouteGetV6("vrf1", v6_subnet2, 96);

    EXPECT_TRUE(rt1 != NULL);
    EXPECT_TRUE(rt2 != NULL);
    EXPECT_TRUE(rt3 != NULL);

    //Verify subnet routes point to discard nexthops
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::DISCARD);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::DISCARD);
    EXPECT_TRUE(rt3->GetActiveNextHop()->GetType() == NextHop::DISCARD);

    //Verify gateway routes are present
    Ip4Address v4_gw1 = Ip4Address::from_string(ipam_info[0].gw, ec);
    Ip6Address v6_gw1 = Ip6Address::from_string(ipam_info[1].gw, ec);
    Ip6Address v6_gw2 = Ip6Address::from_string(ipam_info[2].gw, ec);

    InetUnicastRouteEntry *rt4 = RouteGet("vrf1", v4_gw1, 32);
    InetUnicastRouteEntry *rt5 = RouteGetV6("vrf1", v6_gw1, 128);
    InetUnicastRouteEntry *rt6 = RouteGetV6("vrf1", v6_gw2, 128);

    EXPECT_TRUE(rt4 != NULL);
    EXPECT_TRUE(rt5 != NULL);
    EXPECT_TRUE(rt6 != NULL);

    //Verify gateway routes point to interface nexthops (pkt0)
    EXPECT_TRUE(rt4->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(rt5->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    EXPECT_TRUE(rt6->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    //cleanup
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
}

//Add and delete v4 and v6 static routes on a single interface
TEST_F(Ipv6Test, IntfStaticRoute_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == true);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    //Add a static route
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };
    AddInterfaceRouteTable("static_route", 1, static_route, 2);

    //Add a v6 static route
    struct TestIp6Prefix static_route6[] = {
        { Ip6Address::from_string("fd12::2"), 120},
    };
    AddInterfaceRouteTableV6("static_route6", 2, static_route6, 1);

    AddLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    AddLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route6");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                static_route[0].plen_));
    EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                static_route[1].plen_));
    EXPECT_TRUE(RouteFindV6("vrf1", static_route6[0].addr_,
                static_route6[0].plen_));

    //Delete the link between interface and route table
    DelLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    DelLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route6");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                static_route[0].plen_));
    EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                static_route[1].plen_));
    EXPECT_FALSE(RouteFindV6("vrf1", static_route6[0].addr_,
                static_route6[0].plen_));

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

//Add and delete static route. Verify that dependent_route_list maintained by
//path_preference_module per interface has only v4 routes and does not include
//v6 routes
TEST_F(Ipv6Test, IntfStaticRoute_2) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd11::2"},
    };

    CreateV6VmportEnv(input, 1, 0);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VmPortActive(input, 0)) == true);
    WAIT_FOR(100, 1000, (VmPortV6Active(input, 0)) == true);

    //Add a static route
    struct TestIp4Prefix static_route[] = {
        { Ip4Address::from_string("24.1.1.0"), 24},
        { Ip4Address::from_string("16.1.1.0"), 16},
    };
    AddInterfaceRouteTable("static_route", 1, static_route, 2);

    //Add a v6 static route
    struct TestIp6Prefix static_route6[] = {
        { Ip6Address::from_string("fd12::2"), 120},
    };
    AddInterfaceRouteTableV6("static_route6", 2, static_route6, 1);

    AddLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    AddLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route6");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("vrf1", static_route[0].addr_,
                static_route[0].plen_));
    EXPECT_TRUE(RouteFind("vrf1", static_route[1].addr_,
                static_route[1].plen_));
    EXPECT_TRUE(RouteFindV6("vrf1", static_route6[0].addr_,
                static_route6[0].plen_));

    VmInterface *vmi = VmInterfaceGet(1);
    const PathPreferenceIntfState *cintf_state =
        static_cast<const PathPreferenceIntfState *>(
        vmi->GetState(Agent::GetInstance()->interface_table(),
                      Agent::GetInstance()->oper_db()->
                      route_preference_module()->intf_id()));
    EXPECT_EQ(2U, cintf_state->DependentRouteListSize());

    //Delete the link between interface and route table
    DelLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route");
    DelLink("virtual-machine-interface", "vnet1",
            "interface-route-table", "static_route6");
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", static_route[0].addr_,
                static_route[0].plen_));
    EXPECT_FALSE(RouteFind("vrf1", static_route[1].addr_,
                static_route[1].plen_));
    EXPECT_FALSE(RouteFindV6("vrf1", static_route6[0].addr_,
                static_route6[0].plen_));

    DeleteVmportEnv(input, 1, 1, 0, NULL, NULL, true, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
}

TEST_F(Ipv6Test, VnNotifyRoutes_1) {
    client->Reset();
    VrfAddReq("vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));

    client->Reset();
    VnAddReq(1, "vn1", 0, "vrf1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    boost::system::error_code ec;
    Ip6Address addr1 = Ip6Address::from_string(IPV6_ALL_ROUTERS_ADDRESS, ec);
    EXPECT_TRUE(RouteFindV6("vrf1", addr1, 128));
    Ip6Address addr2 = Ip6Address::from_string(PKT0_LINKLOCAL_ADDRESS, ec);
    EXPECT_TRUE(RouteFindV6("vrf1", addr2, 128));

    //cleanup
    client->Reset();
    VnDelReq(1);
    VrfDelReq("vrf1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(RouteFindV6("vrf1", addr1, 128));
    EXPECT_FALSE(RouteFindV6("vrf1", addr2, 128));
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
