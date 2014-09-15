/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/mac_address.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class MacAddressTest : public ::testing::Test {
};

TEST_F(MacAddressTest, Broadcast) {
    EXPECT_TRUE(MacAddress::kBroadcastAddress.IsBroadcast());
    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    MacAddress mac(data);
    EXPECT_TRUE(mac.IsBroadcast());
}

TEST_F(MacAddressTest, ByteArray) {
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    MacAddress mac(data);
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, NumbersOnly1) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:06", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, NumbersOnly2) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("1:2:3:4:5:6", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, NumbersAndLetters1) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("a1:02:f3:04:d5:b6", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("a1:02:f3:04:d5:b6", mac.ToString());
}

TEST_F(MacAddressTest, NumbersAndLetters2) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("a1:2:f3:4:d5:b6", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("a1:02:f3:04:d5:b6", mac.ToString());
}

TEST_F(MacAddressTest, NumbersAndLetters3) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("A1:02:F3:04:D5:B6", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("a1:02:f3:04:d5:b6", mac.ToString());
}

TEST_F(MacAddressTest, NumbersAndLetters4) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("A1:2:F3:4:D5:B6", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(17, mac.ToString().size());
    EXPECT_EQ("a1:02:f3:04:d5:b6", mac.ToString());
}

// Contains non hex digits.
TEST_F(MacAddressTest, Error1) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:0h:04:0g:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains too few colons.
TEST_F(MacAddressTest, Error2) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra colon at the end.
TEST_F(MacAddressTest, Error3) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:06:", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra colon and other digits.
TEST_F(MacAddressTest, Error4) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:06:07", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains dots instead of colons.
TEST_F(MacAddressTest, Error5) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01.02.03.04.05.06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains non hex digit at the end.
TEST_F(MacAddressTest, Error6) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:0x", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra digit at the end.
TEST_F(MacAddressTest, Error7) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:060", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra non hex digit at the end.
TEST_F(MacAddressTest, Error8) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:03:04:05:06x", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra digit at the beginning.
TEST_F(MacAddressTest, Error9) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("001:02:03:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra non hex digit at the beginning.
TEST_F(MacAddressTest, Error10) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("x01:02:03:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra digit in the middle.
TEST_F(MacAddressTest, Error11) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:003:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra non hex digit in the middle.
TEST_F(MacAddressTest, Error12) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("x01:02:x03:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra non hex digit in the middle.
TEST_F(MacAddressTest, Error13) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:0x:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

// Contains extra non hex digit in the middle.
TEST_F(MacAddressTest, Error14) {
    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString("01:02:0X:04:05:06", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
