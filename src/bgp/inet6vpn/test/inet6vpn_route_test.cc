/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "bgp/inet6/inet6_route.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

class Inet6VpnRouteTest : public ::testing::Test {
    virtual void SetUp() {
        // Needed primarily for IsMoreSpecific
        Inet6Masks::Init();
    }
    virtual void TearDown() {
        Inet6Masks::Clear();
    }
};

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnRouteTest, Type0RdDifferentRdAsn) {
    // Different ASN in the RD
    std::string prefix_str1("100:65535:2001:db8:85a3::8a2e:370:aaaa/128");
    std::string prefix_str2("200:65535:2001:db8:85a3::8a2e:370:aaaa/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnRouteTest, Type0RdDifferentRdLocal) {
    // Different local in the RD
    std::string prefix_str1("100:1:2001:db8:85a3::8a2e:370:aaaa/128");
    std::string prefix_str2("100:4294967295:2001:db8:85a3::8a2e:370:aaaa/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnRouteTest, Type0RdDifferentAddress) {
    // Different ipv6 address
    std::string prefix_str1("100:4294967295:2001:db8:85a3::d:e:aaaa/128");
    std::string prefix_str2("100:4294967295:2001:db8:85a3::d:e:bbbb/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnRouteTest, Type0RdDifferentPrefixLength) {
    // Different prefix length
    std::string prefix_str1("100:4294967295:2001:db8:85a3::d:e:aaaa/100");
    std::string prefix_str2("100:4294967295:2001:db8:85a3::d:e:aaaa/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnRouteTest, Type0RdSamePrefix) {
    // Same prefix
    std::string prefix_str1("65535:4294967295:2001:db8:85a3::d:e:aaaa/128");
    std::string prefix_str2("65535:4294967295:2001:db8:85a3::d:e:aaaa/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnRouteTest, Type1RdDifferentRdIp) {
    // Different ip address in the RD
    std::string prefix_str1("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/128");
    std::string prefix_str2("10.2.2.2:4567:2001:db8:85a3::a:b:cccc/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnRouteTest, Type1RdDifferentRdLocal) {
    // Different local part in the RD
    std::string prefix_str1("10.1.1.1:1:2001:db8:85a3::a:b:cccc/128");
    std::string prefix_str2("10.1.1.1:65535:2001:db8:85a3::a:b:cccc/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnRouteTest, Type1RdDifferentAddress) {
    // Different ipv6 address
    std::string prefix_str1("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/128");
    std::string prefix_str2("10.1.1.1:4567:2001:db8:85a3::a:b:dddd/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnRouteTest, Type1RdDifferentPrefixLength) {
    // Different prefix length
    std::string prefix_str1("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/100");
    std::string prefix_str2("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/120");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    // route1 < route2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, route1.CompareTo(route2));
    // route2 > route1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, route2.CompareTo(route1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnRouteTest, Type1RdSamePrefix) {
    // Same prefix
    std::string prefix_str1("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/128");
    std::string prefix_str2("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/128");
    Inet6VpnRoute route1(Inet6VpnPrefix::FromString(prefix_str1));
    Inet6VpnRoute route2(Inet6VpnPrefix::FromString(prefix_str2));
    EXPECT_EQ(0, route1.CompareTo(route2));
    EXPECT_EQ(0, route2.CompareTo(route1));
}

TEST_F(Inet6VpnRouteTest, ToString) {
    std::string prefix_str("10.1.1.1:4567:2001:db8:85a3::a:b:cccc/128");
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    Inet6VpnRoute route1(prefix);
    EXPECT_EQ(prefix_str, route1.ToString());

    prefix_str = "65534:4294967295:2001:db8:85a3::d:e:a/128";
    prefix = Inet6VpnPrefix::FromString(prefix_str);
    Inet6VpnRoute route2(prefix);
    EXPECT_EQ(prefix_str, route2.ToString());

    prefix_str = "65534:4294967295:2001:db8:85a3:0000:0000:000d:000e:000a/128";
    prefix = Inet6VpnPrefix::FromString(prefix_str);
    Inet6VpnRoute route3(prefix);
    EXPECT_EQ("65534:4294967295:2001:db8:85a3::d:e:a/128", route3.ToString());

    prefix_str = "10.1.1.1:8:2001:db8:85a3::a:b:cccc/128";
    prefix = Inet6VpnPrefix::FromString(prefix_str);
    Inet6VpnRoute route4(prefix);
    EXPECT_EQ(prefix_str, route4.ToString());

    prefix_str = "10.1.1.1:65535:2001:db8:85a3::a:b:cccc/128";
    prefix = Inet6VpnPrefix::FromString(prefix_str);
    Inet6VpnRoute route5(prefix);
    EXPECT_EQ(prefix_str, route5.ToString());
}

TEST_F(Inet6VpnRouteTest, SetKey1) {
    Inet6VpnPrefix null_prefix;
    Inet6VpnRoute route(null_prefix);
    std::string prefix_str = "65534:4294967295:2001:db8:85a3::d:e:a/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<Inet6VpnTable::RequestKey> key(
        new Inet6VpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(Inet6VpnRouteTest, SetKey2) {
    Inet6VpnPrefix null_prefix;
    Inet6VpnRoute route(null_prefix);
    std::string prefix_str = "10.1.1.1:65535:2001:db8:85a3::a:b:cccc/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<Inet6VpnTable::RequestKey> key(
        new Inet6VpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(Inet6VpnRouteTest, GetDBRequestKey1) {
    std::string prefix_str = "65534:4294967295:2001:db8:85a3::d:e:a/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    Inet6VpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const Inet6VpnTable::RequestKey *key =
        static_cast<Inet6VpnTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(Inet6VpnRouteTest, GetDBRequestKey2) {
    std::string prefix_str = "10.1.1.1:65535:2001:db8:85a3::a:b:cccc/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    Inet6VpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const Inet6VpnTable::RequestKey *key =
        static_cast<Inet6VpnTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(Inet6VpnRouteTest, IsMoreSpecificType0Rd) {
    std::string prefix_str = "65000:4294967290:2001:db8:85a3::d:e:a/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    Inet6VpnRoute route1(prefix);
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "65000:4294967290:2001:db8:85a3::/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Smaller RD value should not matter since only ip's are compared
    prefix_str = "1:4294967290:2001:db8:85a3::/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "1:1:2001:db8:85a3::/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Larger RD value also should not matter
    prefix_str = "65333:4294967290:2001:db8:85a3::/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "65333:4294967295:2001:db8:85a3::/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);
}

TEST_F(Inet6VpnRouteTest, IsMoreSpecificType1Rd) {
    std::string prefix_str = "10.1.1.1:65530:2001:db8:85a3::a:b:c/128";
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
    Inet6VpnRoute route1(prefix);
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "10.1.1.1:65530:2001:db8:85a3::a:b:c/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Smaller RD value should not matter since only ip's are compared
    prefix_str = "10.1.1.0:65530:2001:db8:85a3::a:b:c/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "10.1.1.0:99:2001:db8:85a3::a:b:c/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    // Larger RD value also should not matter
    prefix_str = "10.2.2.2:99:2001:db8:85a3::a:b:c/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);

    prefix_str = "10.2.2.2:65535:2001:db8:85a3::a:b:c/48";
    EXPECT_EQ(route1.IsMoreSpecific(prefix_str), true);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
