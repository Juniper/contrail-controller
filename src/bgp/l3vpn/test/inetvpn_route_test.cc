/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class InetVpnRouteTest : public ::testing::Test {
};

TEST_F(InetVpnRouteTest, CompareTo1) {
    string prefix_str1("10.1.1.1:2:192.168.1.1/32");
    string prefix_str2("10.1.1.1:256:192.168.1.1/32");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, CompareTo2) {
    string prefix_str1("65412:2:192.168.1.1/32");
    string prefix_str2("65412:65536:192.168.1.1/32");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, CompareTo3) {
    string prefix_str1("10.1.1.1:65535:192.168.1.1/32");
    string prefix_str2("10.1.1.1:65535:192.168.1.2/32");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, CompareTo4) {
    string prefix_str1("10.1.1.1:65535:192.168.0.0/24");
    string prefix_str2("10.1.1.1:65535:192.168.0.0/28");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, CompareTo5) {
    string prefix_str1("10.1.1.1:65535:192.168.0.0/24");
    string prefix_str2("10.1.1.1:65535:192.168.0.0/24");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, CompareTo6) {
    string prefix_str1("10.1.1.1:65535:192.168.0/24");
    string prefix_str2("10.1.1.1:65535:192.168.0.0/24");
    InetVpnRoute route1(InetVpnPrefix::FromString(prefix_str1));
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(InetVpnRouteTest, ToString1) {
    string prefix_str("10.1.1.1:65535:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    EXPECT_EQ("10.1.1.1:65535:192.168.1.1/32", prefix.ToString());
    EXPECT_EQ("10.1.1.1:65535:192.168.1.1/32", route.ToString());
}

TEST_F(InetVpnRouteTest, ToString2) {
    string prefix_str("10.1.1.1:65535:192.168.1.0/24");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    EXPECT_EQ("10.1.1.1:65535:192.168.1.0/24", prefix.ToString());
    EXPECT_EQ("10.1.1.1:65535:192.168.1.0/24", route.ToString());
}

TEST_F(InetVpnRouteTest, ToString3) {
    string prefix_str("65412:4294967295:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    EXPECT_EQ("65412:4294967295:192.168.1.1/32", prefix.ToString());
    EXPECT_EQ("65412:4294967295:192.168.1.1/32", route.ToString());
}

TEST_F(InetVpnRouteTest, ToString4) {
    string prefix_str("65412:4294967295:192.168.1.0/24");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    EXPECT_EQ("65412:4294967295:192.168.1.0/24", prefix.ToString());
    EXPECT_EQ("65412:4294967295:192.168.1.0/24", route.ToString());
}

TEST_F(InetVpnRouteTest, SetKey1) {
    InetVpnPrefix null_prefix;
    InetVpnRoute route(null_prefix);
    string prefix_str("10.1.1.1:65535:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<InetVpnTable::RequestKey> key(
        new InetVpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(InetVpnRouteTest, SetKey2) {
    InetVpnPrefix null_prefix;
    InetVpnRoute route(null_prefix);
    string prefix_str("65412:4294967295:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<InetVpnTable::RequestKey> key(
        new InetVpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(InetVpnRouteTest, GetDBRequestKey1) {
    string prefix_str("10.1.1.1:65535:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const InetVpnTable::RequestKey *key =
        static_cast<InetVpnTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(InetVpnRouteTest, GetDBRequestKey2) {
    string prefix_str("65412:4294967295:192.168.1.1/32");
    InetVpnPrefix prefix(InetVpnPrefix::FromString(prefix_str));
    InetVpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const InetVpnTable::RequestKey *key =
        static_cast<InetVpnTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
