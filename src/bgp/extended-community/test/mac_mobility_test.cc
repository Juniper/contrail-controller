/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/mac_mobility.h"

#include "testing/gunit.h"

using namespace std;

class MacMobilityTest : public ::testing::Test {
};

TEST_F(MacMobilityTest, ByteArray_1) {
    MacMobility::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::MacMobility,
        0x01, 0x00, 0x0, 0x0, 0x0, 0x1
    } };
    MacMobility mac_mobility(data);
    EXPECT_EQ("mobility:sticky:1", mac_mobility.ToString());
}

TEST_F(MacMobilityTest, ByteArray_2) {
    MacMobility::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::MacMobility,
        0x01, 0x00, 0x0, 0x1, 0x0, 0x0
    } };
    MacMobility mac_mobility(data);
    EXPECT_EQ("mobility:sticky:65536", mac_mobility.ToString());
}

TEST_F(MacMobilityTest, ByteArray_3) {
    MacMobility::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::MacMobility,
        0x00, 0x00, 0x0, 0x0, 0x0, 0x1
    } };
    MacMobility mac_mobility(data);
    EXPECT_EQ("mobility:non-sticky:1", mac_mobility.ToString());
}


TEST_F(MacMobilityTest, Init) {
    boost::system::error_code ec;
    MacMobility mac_mobility(0x1);
    EXPECT_EQ(mac_mobility.ToString(), "mobility:non-sticky:1");
}

TEST_F(MacMobilityTest, Init_2) {
    boost::system::error_code ec;
    MacMobility mac_mobility(0x10000);
    EXPECT_EQ(mac_mobility.ToString(), "mobility:non-sticky:65536");
}

TEST_F(MacMobilityTest, Init_3) {
    boost::system::error_code ec;
    MacMobility mac_mobility(0x10000, true);
    EXPECT_EQ(mac_mobility.ToString(), "mobility:sticky:65536");
}

TEST_F(MacMobilityTest, Init_4) {
    boost::system::error_code ec;
    MacMobility mac_mobility(0x10000, false);
    EXPECT_EQ(mac_mobility.ToString(), "mobility:non-sticky:65536");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
