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
    EXPECT_TRUE(MacAddress::BroadcastMac().IsBroadcast());
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

TEST_F(MacAddressTest, DefaultConstructor) {
    MacAddress mac;
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

TEST_F(MacAddressTest, ConstructFromString) {
    boost::system::error_code ec;
    MacAddress mac("A1:2:F3:4:D5:B6");
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("a1:02:f3:04:d5:b6", mac.ToString());
}

//Construction from invalid string
TEST_F(MacAddressTest, ConstructFromInvalidString) {
    boost::system::error_code ec;
    MacAddress mac("Z1:2:F3:4:D5:B6");
    EXPECT_NE(true, mac.IsValid());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

TEST_F(MacAddressTest, ConstructFrom_ether_addr) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    MacAddress mac(a);
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
}

TEST_F(MacAddressTest, ConstructFromPointerTo_ether_addr) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    MacAddress mac(&a);
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
}

/* Check if MacAddress is true copy and changes to it will not affect
   the oriinal structure and reverse */
TEST_F(MacAddressTest, ConstructFromPointerIsACopy) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    struct ether_addr b = a;
    MacAddress mac(&a);
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    mac[0] = 0x00;
    mac[1] = 0x01;
    EXPECT_EQ("00:01:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(0, memcmp(&a, &b, sizeof(a)));
}

TEST_F(MacAddressTest, ConstructFromInts) {
    MacAddress mac(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("aa:bb:cc:dd:ee:ff", mac.ToString());
}

TEST_F(MacAddressTest, ConstructFromArray) {
    u_int8_t a[] =  { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45 };
    MacAddress mac(a);
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("ab:cd:ef:01:23:45", mac.ToString());
}

TEST_F(MacAddressTest, ArraySubscriptOperator) {
    u_int8_t a[] =  { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45 };
    MacAddress mac(a);
    EXPECT_EQ("ab:cd:ef:01:23:45", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    mac[0] = 0x01;
    mac[1] = 0x02;
    mac[2] = 0x03;
    mac[3] = 0x04;
    mac[4] = 0x05;
    mac[5] = 0x06;
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, AssignmentFrom_ether_addr) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    struct ether_addr b = { { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } };
    MacAddress mac(a);
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    mac = b;
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, AssignmentFromFromPointerTo_u_int8_t) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    u_int8_t b[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    MacAddress mac(a);
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    mac = b;
    EXPECT_EQ("01:02:03:04:05:06", mac.ToString());
}

TEST_F(MacAddressTest, CastTo_ether_addr) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    struct ether_addr b = { { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } };
    MacAddress mac(a);
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    b = mac;
    EXPECT_EQ(0, memcmp(&a, &b, sizeof(a)));
}

TEST_F(MacAddressTest, CastTo_sockaddr) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    struct sockaddr b =  { 0x00 } ;
#if defined(__linux__)
    struct sockaddr ref = { 0x00, { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
#elif defined(__FreeBSD__)
    struct sockaddr ref = { 0x00, 0x00, { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
#endif
    MacAddress mac(a);
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    b = mac;
    EXPECT_EQ(0, memcmp(&b, &ref, sizeof(ref)));
}

TEST_F(MacAddressTest, SizeIsETHER_ADDR_LEN) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    MacAddress mac(a);
    EXPECT_EQ(ETHER_ADDR_LEN, mac.size());
}

TEST_F(MacAddressTest, last_octet) {
    struct ether_addr a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } };
    MacAddress mac(a);
    EXPECT_EQ("0a:0b:0c:0d:0e:0f", mac.ToString());
    EXPECT_EQ(true, mac.IsValid());
    mac.last_octet() = 0xff;
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("0a:0b:0c:0d:0e:ff", mac.ToString());
    mac.last_octet() = 0xaa;
    EXPECT_EQ("0a:0b:0c:0d:0e:aa", mac.ToString());
}

TEST_F(MacAddressTest, Zeroing) {
    u_int8_t a[] =  { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45 };
    MacAddress mac(a);
    mac.Zero();
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("00:00:00:00:00:00", mac.ToString());
}

TEST_F(MacAddressTest, Broadcasting) {
    u_int8_t a[] =  { 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45 };
    MacAddress mac(a);
    mac.Broadcast();
    EXPECT_EQ(true, mac.IsValid());
    EXPECT_EQ("ff:ff:ff:ff:ff:ff", mac.ToString());
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
