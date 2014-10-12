/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>

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
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h> 
#include <ksync/ksync_sock_user.h> 
#include <boost/assign/list_of.hpp>

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
