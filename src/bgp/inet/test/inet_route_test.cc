/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */


#include "bgp/bgp_log.h"
#include "bgp/inet/inet_table.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using std::string;

class InetRouteTest : public ::testing::Test {
};

TEST_F(InetRouteTest, CompareTo1) {
    string prefix_str1("192.168.1.1/32");
    string prefix_str2("192.168.1.2/32");
    InetRoute route1(Ip4Prefix::FromString(prefix_str1));
    InetRoute route2(Ip4Prefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetRouteTest, CompareTo2) {
    string prefix_str1("192.168.0.0/24");
    string prefix_str2("192.168.0.0/28");
    InetRoute route1(Ip4Prefix::FromString(prefix_str1));
    InetRoute route2(Ip4Prefix::FromString(prefix_str2));
    EXPECT_GT(0, route1.CompareTo(route2));
    EXPECT_LT(0, route2.CompareTo(route1));
}

TEST_F(InetRouteTest, CompareTo3) {
    string prefix_str1("192.168.0.0/24");
    string prefix_str2("192.168.0.0/24");
    InetRoute route1(Ip4Prefix::FromString(prefix_str1));
    InetRoute route2(Ip4Prefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(InetRouteTest, CompareTo4) {
    string prefix_str1("192.168.0/24");
    string prefix_str2("192.168.0.0/24");
    InetRoute route1(Ip4Prefix::FromString(prefix_str1));
    InetRoute route2(Ip4Prefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(InetRouteTest, CompareTo5) {
    string prefix_str1("0/0");
    string prefix_str2("0.0.0.0/0");
    InetRoute route1(Ip4Prefix::FromString(prefix_str1));
    InetRoute route2(Ip4Prefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(InetRouteTest, ToString1) {
    string prefix_str("192.168.1.1/32");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix);
    EXPECT_EQ("192.168.1.1/32", prefix.ToString());
    EXPECT_EQ("192.168.1.1/32", route.ToString());
}

TEST_F(InetRouteTest, ToString2) {
    string prefix_str("192.168.1.0/24");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix);
    EXPECT_EQ("192.168.1.0/24", prefix.ToString());
    EXPECT_EQ("192.168.1.0/24", route.ToString());
}

TEST_F(InetRouteTest, ToString3) {
    string prefix_str("0.0.0.0/0");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix);
    EXPECT_EQ("0.0.0.0/0", prefix.ToString());
    EXPECT_EQ("0.0.0.0/0", route.ToString());
}

TEST_F(InetRouteTest, SetKey1) {
    Ip4Prefix null_prefix;
    InetRoute route(null_prefix);
    string prefix_str("192.168.1.1/32");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    boost::scoped_ptr<InetTable::RequestKey> key(
        new InetTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(InetRouteTest, SetKey2) {
    Ip4Prefix null_prefix;
    InetRoute route(null_prefix);
    string prefix_str("192.168.1.0/24");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    boost::scoped_ptr<InetTable::RequestKey> key(
        new InetTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(InetRouteTest, GetDBRequestKey1) {
    string prefix_str("192.168.1.1/32");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const InetTable::RequestKey *key =
        static_cast<InetTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(InetRouteTest, GetDBRequestKey2) {
    string prefix_str("192.168.1.0/24");
    Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const InetTable::RequestKey *key =
        static_cast<InetTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(InetRouteTest, FromProtoPrefix1) {
    string prefix_str("192.168.1.1/32");
    Ip4Prefix prefix1(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, NULL, 0);
    Ip4Prefix prefix2;
    int result = Ip4Prefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(4 * 8, proto_prefix.prefixlen);
    EXPECT_EQ(4U, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

TEST_F(InetRouteTest, FromProtoPrefix2) {
    string prefix_str("192.168.1.0/24");
    Ip4Prefix prefix1(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, NULL, 0);
    Ip4Prefix prefix2;
    int result = Ip4Prefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(3 * 8, proto_prefix.prefixlen);
    EXPECT_EQ(3U, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

TEST_F(InetRouteTest, FromProtoPrefixError1) {
    string prefix_str("192.168.1.1/32");
    Ip4Prefix prefix1(Ip4Prefix::FromString(prefix_str));
    InetRoute route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, NULL, 0);
    Ip4Prefix prefix2;
    proto_prefix.prefix.resize(5);
    int result = Ip4Prefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_NE(0, result);
}

TEST_F(InetRouteTest, IsMoreSpecific1) {
    string prefix_str = "240.240.240.240/32";
    InetRoute route(Ip4Prefix::FromString(prefix_str));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.240.240/32"));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.240.240/28"));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.240.0/24"));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.240.0/20"));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.0.0/16"));
    EXPECT_TRUE(route.IsMoreSpecific("240.240.0.0/12"));
    EXPECT_TRUE(route.IsMoreSpecific("240.0.0.0/8"));
    EXPECT_TRUE(route.IsMoreSpecific("240.0.0.0/4"));
    EXPECT_TRUE(route.IsMoreSpecific("0.0.0.0/0"));
}

TEST_F(InetRouteTest, IsMoreSpecific2) {
    string prefix_str = "224.224.224.224/32";
    InetRoute route(Ip4Prefix::FromString(prefix_str));
    EXPECT_FALSE(route.IsMoreSpecific("224.224.224.240/32"));
    EXPECT_FALSE(route.IsMoreSpecific("224.224.224.240/28"));
    EXPECT_FALSE(route.IsMoreSpecific("224.224.240.0/24"));
    EXPECT_FALSE(route.IsMoreSpecific("224.224.240.0/20"));
    EXPECT_FALSE(route.IsMoreSpecific("224.240.0.0/16"));
    EXPECT_FALSE(route.IsMoreSpecific("224.240.0.0/12"));
    EXPECT_FALSE(route.IsMoreSpecific("240.0.0.0/8"));
    EXPECT_FALSE(route.IsMoreSpecific("240.0.0.0/4"));
}

TEST_F(InetRouteTest, IsLessSpecific) {
    string prefix_str = "240.240.0.0/16";
    InetRoute route(Ip4Prefix::FromString(prefix_str));
    EXPECT_TRUE(route.IsLessSpecific("240.240.240.240/32"));
    EXPECT_TRUE(route.IsLessSpecific("240.240.240.240/28"));
    EXPECT_TRUE(route.IsLessSpecific("240.240.240.0/24"));
    EXPECT_TRUE(route.IsLessSpecific("240.240.240.0/20"));
    EXPECT_TRUE(route.IsLessSpecific("240.240.0.0/16"));
    EXPECT_FALSE(route.IsLessSpecific("240.240.0.0/12"));
    EXPECT_FALSE(route.IsLessSpecific("240.0.0.0/8"));
    EXPECT_FALSE(route.IsLessSpecific("240.0.0.0/4"));
    EXPECT_FALSE(route.IsLessSpecific("0.0.0.0/0"));
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
