/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */


#include "bgp/bgp_log.h"
#include "bgp/inet6/inet6_table.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

class Inet6RouteTest : public ::testing::Test {
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

TEST_F(Inet6RouteTest, IsMoreSpecific) {
    std::string prefix_str = "2001:db8:85a3:aaaa::b:c:d/128";
    Inet6Route route1(Inet6Prefix::FromString(prefix_str));
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Compare with prefixlen 64
    prefix_str = "2001:db8:85a3:aaaa::b:c:d/64";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "2001:db8:85a3:aaaa::/64";
    Inet6Route route2(Inet6Prefix::FromString(prefix_str));
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Compare with prefixlen 48
    prefix_str = "2001:db8:85a3::/48";
    EXPECT_EQ(route2.IsMoreSpecific(prefix_str), true);

    prefix_str = "2001:db8:85a3:aaaa::/48";
    EXPECT_EQ(route2.IsMoreSpecific(prefix_str), true);
}

TEST_F(Inet6RouteTest, IsLessSpecific) {
    std::string prefix_str1 = "2001:db8:85a3:aaaa::b:c:d/128";
    Inet6Route route1(Inet6Prefix::FromString(prefix_str1));
    EXPECT_EQ(route1.IsLessSpecific(prefix_str1), true);

    // Compare against route with prefixlen 64
    std::string prefix_str2 = "2001:db8:85a3:aaaa::/64";
    Inet6Route route2(Inet6Prefix::FromString(prefix_str2));
    EXPECT_EQ(route2.IsLessSpecific(prefix_str1), true);

    // Compare against route with prefixlen 48
    std::string prefix_str3 = "2001:db8:85a3::/48";
    Inet6Route route3(Inet6Prefix::FromString(prefix_str3));
    EXPECT_EQ(route3.IsLessSpecific(prefix_str1), true);
}

TEST_F(Inet6RouteTest, SetKey) {
    Inet6Prefix null_prefix;
    Inet6Route route(null_prefix);

    std::string prefix_str = "2001:db8:85a3::d:e:a/128";
    Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
    boost::scoped_ptr<Inet6Table::RequestKey> key(
        new Inet6Table::RequestKey(prefix, NULL));

    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(Inet6RouteTest, GetDBRequestKey) {
    std::string prefix_str = "2001:db8:85a3::d:e:a/128";
    Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
    Inet6Route route(prefix);

    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const Inet6Table::RequestKey *key =
        static_cast<Inet6Table::RequestKey *>(keyptr.get());

    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(Inet6RouteTest, CompareTo) {
    std::string prefix_str = "2001:db8:85a3::d:e:a/128";
    Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));

    // Prefixlen 64
    std::string prefix_str1 = "2001:db8:85a3::d:e:a/64";
    Inet6Prefix prefix1(Inet6Prefix::FromString(prefix_str1));
    // prefix.CompareTo(prefix1) should return 1
    EXPECT_LT(0, prefix.CompareTo(prefix1));
    // prefix1.CompareTo(prefix) should return -1
    EXPECT_GT(0, prefix1.CompareTo(prefix));

    // Last byte of address is 0xb
    std::string prefix_str2 = "2001:db8:85a3::d:e:b/128";
    Inet6Prefix prefix2(Inet6Prefix::FromString(prefix_str2));
    // prefix.CompareTo(prefix2) should return -1
    EXPECT_GT(0, prefix.CompareTo(prefix2));
    // prefix2.CompareTo(prefix) should return 1
    EXPECT_LT(0, prefix2.CompareTo(prefix));
}

TEST_F(Inet6RouteTest, FromProtoPrefix1) {
    std::string prefix_str = "2001:db8:85a3::d:e:a/128";
    Inet6Prefix prefix1(Inet6Prefix::FromString(prefix_str));
    Inet6Route route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, NULL, 0);
    Inet6Prefix prefix2;
    int result = Inet6Prefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(16 * 8, proto_prefix.prefixlen);
    EXPECT_EQ(16U, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

TEST_F(Inet6RouteTest, FromProtoPrefix2) {
    std::string prefix_str = "2001:db8:85a3:d36f::/64";
    Inet6Prefix prefix1(Inet6Prefix::FromString(prefix_str));
    Inet6Route route(prefix1);
    BgpProtoPrefix proto_prefix;
    route.BuildProtoPrefix(&proto_prefix, NULL, 0);
    Inet6Prefix prefix2;
    int result = Inet6Prefix::FromProtoPrefix(proto_prefix, &prefix2);
    EXPECT_EQ(0, result);
    EXPECT_EQ(8 * 8, proto_prefix.prefixlen);
    EXPECT_EQ(8U, proto_prefix.prefix.size());
    EXPECT_EQ(prefix1, prefix2);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
