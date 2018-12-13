/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/agent_route.h"
#include "oper/inet_unicast_route.h"

#include "testing/gunit.h"
#include "gtest/gtest.h"

/*
 *  Patricia has its own tests in base module. However, under Windows,
 *  a specific bug/undefined behavior has been observed for specific
 *  version of Microsoft C++ compiler (19.00.24210) with optimizations
 *  enabled ('production' build) and for specific arguments of Patricia::Tree
 *  template (InetUnicastAgentRouteTable::InetRouteTree). This undesired
 *  behavior has been changed (and can no longer be observed in version
 *  19.00.24215.1).
 *
 *  This test, compiled with MS C++ 19.00.24210 (with optimizations enabled)
 *  or other compiler that behaves similarly and with
 *  InetUnicastAgentRouteTable implementation older than this test, is going
 *  to fail.
 */

TEST(InetRouteTreeTest, Test1) {
    InetUnicastAgentRouteTable::InetRouteTree tree;
    boost::asio::ip::address address1 = IpAddress::from_string("10.0.0.11");
    boost::asio::ip::address address2 = IpAddress::from_string("10.0.0.13");

    InetUnicastRouteEntry route1(NULL, address1, 32, false);
    InetUnicastRouteEntry route2(NULL, address2, 32, false);

    EXPECT_EQ(tree.Insert(&route1), true);
    EXPECT_EQ(tree.Insert(&route2), true);

    InetUnicastRouteEntry route_to_find(NULL, address2, 32, false);

    EXPECT_EQ(tree.Find(&route_to_find), &route2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
