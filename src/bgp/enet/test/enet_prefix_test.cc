/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/enet/enet_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class EnetPrefixTest : public ::testing::Test {
};

TEST_F(EnetPrefixTest, Build1) {
    boost::system::error_code ec;
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Prefix ip_prefix(Ip4Prefix::FromString("192.168.1.1/32", &ec));
    EnetPrefix prefix(mac_addr, ip_prefix);
    EXPECT_EQ(prefix.ToString(), "11:12:13:14:15:16,192.168.1.1/32");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.168.1.1/32");
}

TEST_F(EnetPrefixTest, Build2) {
    boost::system::error_code ec;
    MacAddress mac_addr(MacAddress::FromString("11:12:13:14:15:16", &ec));
    Ip4Prefix ip_prefix(Ip4Prefix::FromString("192.168.1.0/24", &ec));
    EnetPrefix prefix(mac_addr, ip_prefix);
    EXPECT_EQ(prefix.ToString(), "11:12:13:14:15:16,192.168.1.0/24");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.168.1.0/24");
}

TEST_F(EnetPrefixTest, Parse1) {
    boost::system::error_code ec;
    EnetPrefix prefix(
        EnetPrefix::FromString("11:12:13:14:15:16,192.168.1.1/32", &ec));
    EXPECT_EQ(prefix.ToString(), "11:12:13:14:15:16,192.168.1.1/32");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.168.1.1/32");
}

TEST_F(EnetPrefixTest, Parse2) {
    boost::system::error_code ec;
    EnetPrefix prefix(
        EnetPrefix::FromString("11:12:13:14:15:16,192.168.1.0/24", &ec));
    EXPECT_EQ(prefix.ToString(), "11:12:13:14:15:16,192.168.1.0/24");
    EXPECT_EQ(prefix.mac_addr().ToString(), "11:12:13:14:15:16");
    EXPECT_EQ(prefix.ip_prefix().ToString(), "192.168.1.0/24");
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
