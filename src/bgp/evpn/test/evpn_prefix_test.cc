/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class EvpnPrefixTest : public ::testing::Test {
};

TEST_F(EvpnPrefixTest, Build1) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Prefix ip_prefix(Ip4Prefix::FromString("192.1.1.1/32", &ec));
    EvpnPrefix prefix(rd, mac_addr, ip_prefix);
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535-11:12:13:14:15:16,192.1.1.1/32");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.1.1.1/32");
}

TEST_F(EvpnPrefixTest, Build2) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Prefix ip_prefix(Ip4Prefix::FromString("192.1.1.0/24", &ec));
    EvpnPrefix prefix(rd, mac_addr, ip_prefix);
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535-11:12:13:14:15:16,192.1.1.0/24");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.1.1.0/24");
}

TEST_F(EvpnPrefixTest, Parse1) {
    boost::system::error_code ec;
    string prefix_str("10.1.1.1:65535-11:12:13:14:15:16,192.1.1.1/32");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535-11:12:13:14:15:16,192.1.1.1/32");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.1.1.1/32");
}

TEST_F(EvpnPrefixTest, Parse2) {
    boost::system::error_code ec;
    string prefix_str("10.1.1.1:65535-11:12:13:14:15:16,192.1.1.0/24");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str, &ec));
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535-11:12:13:14:15:16,192.1.1.0/24");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.1.1.0/24");
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
