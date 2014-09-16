/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6/inet6_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

class Inet6PrefixTest : public ::testing::Test {
    virtual void SetUp() {
        Inet6Masks::Init();
    }
    virtual void TearDown() {
        Inet6Masks::Clear();
    }
};

TEST_F(Inet6PrefixTest, PrefixTest) {
    for (int i = 0; i <= Inet6Prefix::kMaxV6PrefixLen; ++i) {
        Inet6Prefix prefix = Inet6Masks::PrefixlenToMask(i);
        std::cout << "prefix " << i << " is " << prefix.ToString() << std::endl;
    }
}

TEST_F(Inet6PrefixTest, BuildPrefix) {
    boost::system::error_code ec;

    Inet6Prefix prefix(Inet6Prefix::FromString(
        "2001:0db8:85a3:0000:0000:8a2e:0370:7334/64", &ec));
    EXPECT_EQ(prefix.ToString(), "2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // :: format
    prefix = Inet6Prefix::FromString("2001:0db8:85a3::8a2e:0370:7334/64", &ec);
    EXPECT_EQ(prefix.ToString(), "2001:db8:85a3::8a2e:370:7334/64");
    EXPECT_EQ(ec.value(), 0);

    // :: at the end
    Inet6Prefix pnet1(Inet6Prefix::FromString("2001:0db8:85a3::/64", &ec));
    EXPECT_EQ(pnet1.ToString(), "2001:db8:85a3::/64");
    EXPECT_EQ(ec.value(), 0);

    // 2 possible ::'s
    pnet1 = Inet6Prefix::FromString(
        "2001:0db8:85a3:0000:0000:8a2e:0000:0000/96", &ec);
    EXPECT_EQ(pnet1.ToString(), "2001:db8:85a3::8a2e:0:0/96");
    EXPECT_EQ(ec.value(), 0);

    // should get :: at the end
    pnet1 = Inet6Prefix::FromString(
        "2001:0db8:85a3:a:b:8a2e:0000:0000/96", &ec);
    EXPECT_EQ(pnet1.ToString(), "2001:db8:85a3:a:b:8a2e::/96");
    EXPECT_EQ(ec.value(), 0);

    // 3 colon's
    prefix = Inet6Prefix::FromString("2001:0db8:85a3:::0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // non-hexadecimal 'g'
    prefix = Inet6Prefix::FromString("2001:0ggg:85a3:::0370:7334/64", &ec);
    EXPECT_NE(ec.value(), 0);

    // no prefix length
    prefix = Inet6Prefix::FromString("2001:0db8:85a3::0370:7334", &ec);
    EXPECT_NE(ec.value(), 0);

    // bad prefix length
    prefix = Inet6Prefix::FromString("2001:0db8:85a3::0370:7334/130", &ec);
    EXPECT_NE(ec.value(), 0);

    // bad prefix length
    prefix = Inet6Prefix::FromString("2001:0db8:85a3::0370:7334/-12", &ec);
    EXPECT_NE(ec.value(), 0);
}

TEST_F(Inet6PrefixTest, ComparePrefixes) {
    boost::system::error_code ec;
    Inet6Prefix phost1(Inet6Prefix::FromString(
        "2001:0db8:85a3:0000:0000:8a2e:0370:7334/128", &ec));
    EXPECT_EQ(phost1.ToString(), "2001:db8:85a3::8a2e:370:7334/128");

    Inet6Prefix phost2(Inet6Prefix::FromString(
        "2001:0db8:85a3:0000:0000:0000:0000:0001/128", &ec));
    EXPECT_EQ(phost2.ToString(), "2001:db8:85a3::1/128");

    Inet6Prefix pnet1(Inet6Prefix::FromString(
        "2001:0db8:85a3:0000:0000:0000:0000:0000/64", &ec));
    EXPECT_EQ(pnet1.ToString(), "2001:db8:85a3::/64");

    Inet6Prefix pnet2(Inet6Prefix::FromString(
        "2002:aaaa:bbbb:0000:0000:0000:0000:0000/56", &ec));
    EXPECT_EQ(pnet2.ToString(), "2002:aaaa:bbbb::/56");

    EXPECT_EQ(phost1.IsMoreSpecific(pnet1), true);
    EXPECT_EQ(phost2.IsMoreSpecific(pnet1), true);

    EXPECT_EQ(phost1.IsMoreSpecific(phost2), false);

    EXPECT_EQ(pnet1.IsMoreSpecific(pnet1), true);

    // pnet1.plen less than phost2.plen
    EXPECT_EQ(pnet1.IsMoreSpecific(phost2), false);
    EXPECT_EQ(pnet1.IsMoreSpecific(pnet2), false);

    EXPECT_EQ(phost1.IsMoreSpecific(pnet2), false);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
