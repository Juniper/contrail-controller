/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "bgp/inet6/inet6_route.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

class Inet6VpnPrefixTest : public ::testing::Test {
    virtual void SetUp() {
        Inet6Masks::Init();
    }
    virtual void TearDown() {
        Inet6Masks::Clear();
    }
};

TEST_F(Inet6VpnPrefixTest, BuildPrefix) {
    boost::system::error_code ec;

    // type 0 - 2B asn, 4B local
    Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(
        "65412:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec));
    EXPECT_EQ(prefix.ToString(),
              "65412:4294967295:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // type 0 - asn 0 is invalid
    prefix = Inet6VpnPrefix::FromString(
        "0:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // type 0 - asn 65535 is invalid
    prefix = Inet6VpnPrefix::FromString(
        "65535:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // type 0 - asn 88888 is invalid
    prefix = Inet6VpnPrefix::FromString(
        "88888:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // type 0 - assigned local 4294967296 is invalid
    prefix = Inet6VpnPrefix::FromString(
        "100:4294967296:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // type 1 - 4B ip address, 2B local
    prefix = Inet6VpnPrefix::FromString(
        "10.1.1.1:4567:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_EQ(prefix.ToString(),
              "10.1.1.1:4567:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // type 1 with assigned as 0
    prefix = Inet6VpnPrefix::FromString(
        "10.1.1.1:0:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_EQ(prefix.ToString(),
              "10.1.1.1:0:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // type 1 with assigned as 65535
    prefix = Inet6VpnPrefix::FromString(
        "10.1.1.1:65535:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_EQ(prefix.ToString(),
              "10.1.1.1:65535:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // type 1 - assinged local 65536 is invalid
    prefix = Inet6VpnPrefix::FromString(
        "10.1.1.1:65536:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // Incomplete prefix
    prefix = Inet6VpnPrefix::FromString("10.1.1.1/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // Incomplete prefix
    prefix = Inet6VpnPrefix::FromString("10.1.1.1:65/64", &ec);
    EXPECT_NE(ec.value(), 0);

    /* type2 not supported?
    // type 2 - 4B asn, 2B local
    prefix = Inet6VpnPrefix::FromString(
        "4294967295:65412:2001:0db8:85a3::8a2e:0370:7334/64", &ec);
    EXPECT_EQ(prefix.ToString(),
              "4294967295:65412:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);
    */
}

TEST_F(Inet6VpnPrefixTest, IsMoreSpecific) {
    boost::system::error_code ec;

    Inet6VpnPrefix phost1(Inet6VpnPrefix::FromString(
        "65412:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/80", &ec));
    EXPECT_EQ(phost1.ToString(),
              "65412:4294967295:2001:db8:85a3::8a2e:370:7334/80");
    EXPECT_EQ(ec.value(), 0);

    Inet6VpnPrefix pnet1_1(Inet6VpnPrefix::FromString(
        "65412:4294967295:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec));
    EXPECT_EQ(pnet1_1.ToString(),
              "65412:4294967295:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    EXPECT_EQ(phost1.IsMoreSpecific(pnet1_1), true);

    // Smaller RD value should not matter since only ip's are compared
    Inet6VpnPrefix pnet1_2(Inet6VpnPrefix::FromString(
        "1:1:2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec));
    EXPECT_EQ(pnet1_2.ToString(),
              "1:1:2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    EXPECT_EQ(phost1.IsMoreSpecific(pnet1_2), true);

    // pnet1_1 vs itself
    EXPECT_EQ(pnet1_1.IsMoreSpecific(pnet1_1), true);
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnPrefixTest, Type0RdDifferentRdAsn) {
    boost::system::error_code ec;

    // Different ASN in the RD
    std::string prefix_str1("100:65535:2001:db8:85a3::8a2e:370:aaaa/128");
    std::string prefix_str2("200:65535:2001:db8:85a3::8a2e:370:aaaa/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnPrefixTest, Type0RdDifferentRdLocal) {
    boost::system::error_code ec;

    // Different local in the RD
    std::string prefix_str1("100:100:2001:db8:85a3::8a2e:370:aaaa/128");
    std::string prefix_str2("100:65535:2001:db8:85a3::8a2e:370:aaaa/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnPrefixTest, Type0RdDifferentAddress) {
    boost::system::error_code ec;

    // Different ipv6 address
    std::string prefix_str1("100:100:2001:db8:85a3::8a2e:370:aaaa/128");
    std::string prefix_str2("100:100:2001:db8:85a3::8a2e:370:bbbb/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnPrefixTest, Type0RdDifferentPrefixLength) {
    boost::system::error_code ec;

    // Different prefix length
    std::string prefix_str1("100:4294967295:2001:db8:85a3::d:e:aaaa/100");
    std::string prefix_str2("100:4294967295:2001:db8:85a3::d:e:aaaa/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 0 - 2B asn, 4B local
TEST_F(Inet6VpnPrefixTest, Type0RdSamePrefix) {
    boost::system::error_code ec;

    // Same prefixes
    std::string prefix_str1("100:4294967295:2001:db8:85a3::d:e:aaaa/100");
    std::string prefix_str2("100:4294967295:2001:db8:85a3::d:e:aaaa/100");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(0, prefix1.CompareTo(prefix2));
    EXPECT_EQ(0, prefix2.CompareTo(prefix1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnPrefixTest, Type1RdDifferentRdIp) {
    boost::system::error_code ec;

    // Different ip address in the RD
    std::string prefix_str1("10.100.100.100:4567:2001:db8:85a3::a:b:c/128");
    std::string prefix_str2("10.100.100.101:4567:2001:db8:85a3::a:b:c/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnPrefixTest, Type1RdDifferentRdLocal) {
    boost::system::error_code ec;

    // Different local part in the RD
    std::string prefix_str1("10.100.100.100:4567:2001:db8:85a3::a:b:c/128");
    std::string prefix_str2("10.100.100.100:6789:2001:db8:85a3::a:b:c/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnPrefixTest, Type1RdDifferentAddress) {
    boost::system::error_code ec;

    // Different ipv6 address
    std::string prefix_str1("1.1.1.1:2345:2001:db8:85a3::a:b:cccc/128");
    std::string prefix_str2("1.1.1.1:2345:2001:db8:85a3::a:b:dddd/128");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnPrefixTest, Type1RdDifferentPrefixLength) {
    boost::system::error_code ec;

    // Different prefix length
    std::string prefix_str1("1.1.1.1:2345:2001:db8:85a3::a:b:cccc/80");
    std::string prefix_str2("1.1.1.1:2345:2001:db8:85a3::a:b:cccc/100");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    // prefix1 < prefix2 i.e. CompareTo() will return negative number
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    // prefix2 > prefix1 i.e. CompareTo() will return positive number
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

// type 1 - 4B ip address, 2B local
TEST_F(Inet6VpnPrefixTest, Type1RdSamePrefix) {
    boost::system::error_code ec;

    // Same prefixes
    std::string prefix_str1("1.1.1.1:2345:2001:db8:85a3::a:b:cccc/80");
    std::string prefix_str2("1.1.1.1:2345:2001:db8:85a3::a:b:cccc/80");
    Inet6VpnPrefix prefix1(Inet6VpnPrefix::FromString(prefix_str1, &ec));
    EXPECT_EQ(ec.value(), 0);
    Inet6VpnPrefix prefix2(Inet6VpnPrefix::FromString(prefix_str2, &ec));
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(0, prefix1.CompareTo(prefix2));
    EXPECT_EQ(0, prefix2.CompareTo(prefix1));
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
