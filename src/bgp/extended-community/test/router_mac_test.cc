/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/router_mac.h"

#include "bgp/community.h"
#include "testing/gunit.h"

using namespace std;

class RouterMacTest : public ::testing::Test {
};

TEST_F(RouterMacTest, ByteArray_1) {
    RouterMac::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::RouterMac,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06
    } };
    RouterMac router_mac(data);
    EXPECT_TRUE(ExtCommunity::is_router_mac(router_mac.GetExtCommunity()));
    EXPECT_EQ("rtrmac:01:02:03:04:05:06", router_mac.ToString());
}

TEST_F(RouterMacTest, ByteArray_2) {
    RouterMac::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::RouterMac,
        0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    } };
    RouterMac router_mac(data);
    EXPECT_TRUE(ExtCommunity::is_router_mac(router_mac.GetExtCommunity()));
    EXPECT_EQ("rtrmac:0a:0b:0c:0d:0e:0f", router_mac.ToString());
}

TEST_F(RouterMacTest, MacAddress_1) {
    boost::system::error_code ec;
    MacAddress mac_addr = MacAddress::FromString("01:02:03:04:05:06", &ec);
    EXPECT_EQ(0, ec.value());
    RouterMac router_mac(mac_addr);
    EXPECT_TRUE(ExtCommunity::is_router_mac(router_mac.GetExtCommunity()));
    EXPECT_EQ("rtrmac:01:02:03:04:05:06", router_mac.ToString());
    EXPECT_EQ(mac_addr, router_mac.mac_address());
    RouterMac router_mac2(router_mac.GetExtCommunity());
    EXPECT_EQ(router_mac.GetExtCommunityValue(),
        router_mac2.GetExtCommunityValue());
}

TEST_F(RouterMacTest, MacAddress_2) {
    boost::system::error_code ec;
    MacAddress mac_addr = MacAddress::FromString("0a:0b:0c:0d:0e:0f", &ec);
    EXPECT_EQ(0, ec.value());
    RouterMac router_mac(mac_addr);
    EXPECT_TRUE(ExtCommunity::is_router_mac(router_mac.GetExtCommunity()));
    EXPECT_EQ("rtrmac:0a:0b:0c:0d:0e:0f", router_mac.ToString());
    EXPECT_EQ(mac_addr, router_mac.mac_address());
    RouterMac router_mac2(router_mac.GetExtCommunity());
    EXPECT_EQ(router_mac.GetExtCommunityValue(),
        router_mac2.GetExtCommunityValue());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
