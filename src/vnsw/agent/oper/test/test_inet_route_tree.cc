/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/agent_route.h"
#include "oper/inet_unicast_route.h"

#include "testing/gunit.h"
#include "gtest/gtest.h"

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
