/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inetmcast/inetmcast_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class InetMcastPrefixTest : public ::testing::Test {
};

TEST_F(InetMcastPrefixTest, Build) {
    boost::system::error_code ec;
    RouteDistinguisher rd(RouteDistinguisher::FromString("10.1.1.1:65535"));
    Ip4Address group(Ip4Address::from_string("224.1.1.1", ec));
    Ip4Address source(Ip4Address::from_string("192.168.1.1", ec));
    InetMcastPrefix prefix(rd, group, source);
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535:224.1.1.1,192.168.1.1");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.group().to_string(), "224.1.1.1");
    EXPECT_EQ(prefix.source().to_string(), "192.168.1.1");
}

TEST_F(InetMcastPrefixTest, Parse) {
    InetMcastPrefix prefix(
        InetMcastPrefix::FromString("10.1.1.1:65535:224.1.1.1,192.168.1.1"));
    EXPECT_EQ(prefix.ToString(), "10.1.1.1:65535:224.1.1.1,192.168.1.1");
    EXPECT_EQ(prefix.route_distinguisher().ToString(), "10.1.1.1:65535");
    EXPECT_EQ(prefix.group().to_string(), "224.1.1.1");
    EXPECT_EQ(prefix.source().to_string(), "192.168.1.1");
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
