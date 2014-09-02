/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/esi.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class EthernetSegmentIdTest : public ::testing::Test {
};

TEST_F(EthernetSegmentIdTest, NullEsi) {
    EthernetSegmentId esi = EthernetSegmentId::kZeroEsi;
    EXPECT_TRUE(esi.IsZero());
    EXPECT_EQ(0, esi.Type());
    EXPECT_EQ("zero_esi", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, FromStringNullEsi) {
    string esi_str("zero_esi");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_TRUE(esi.IsZero());
    EXPECT_EQ(0, esi.Type());
    EXPECT_EQ("zero_esi", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, MaxEsi) {
    EthernetSegmentId esi = EthernetSegmentId::kMaxEsi;
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(0xFF, esi.Type());
    EXPECT_EQ("max_esi", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, FromStringMaxEsi) {
    string esi_str("max_esi");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(0xFF, esi.Type());
    EXPECT_EQ("max_esi", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType0) {
    uint8_t data[] = { 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::CONFIGURED, esi.Type());
    EXPECT_EQ("00:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType0) {
    string esi_str("00:00:01:23:45:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::CONFIGURED, esi.Type());
    EXPECT_EQ("00:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType1) {
    uint8_t data[] = { 0x01, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::LACP_BASED, esi.Type());
    EXPECT_EQ("01:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType1) {
    string esi_str("01:00:01:23:45:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::LACP_BASED, esi.Type());
    EXPECT_EQ("01:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType2) {
    uint8_t data[] = { 0x02, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::STP_BASED, esi.Type());
    EXPECT_EQ("02:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType2) {
    string esi_str("02:00:01:23:45:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::STP_BASED, esi.Type());
    EXPECT_EQ("02:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType3) {
    uint8_t data[] = { 0x03, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::MAC_BASED, esi.Type());
    EXPECT_EQ("03:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType3) {
    string esi_str("03:00:01:23:45:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::MAC_BASED, esi.Type());
    EXPECT_EQ("03:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType4_1) {
    uint8_t data[] = { 0x04, 0xc0, 0xa8, 0x01, 0x64, 0x01, 0x02, 0x03, 0x04, 0x00 };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::IP_BASED, esi.Type());
    EXPECT_EQ("192.168.1.100:16909060", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, ByteArrayType4_2) {
    uint8_t data[] = { 0x04, 0xc0, 0xa8, 0x01, 0x64, 0xff, 0xff, 0xff, 0xff, 0x00 };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::IP_BASED, esi.Type());
    EXPECT_EQ("192.168.1.100:4294967295", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType4_1) {
    string esi_str("192.168.1.100:16909060");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::IP_BASED, esi.Type());
    EXPECT_EQ("192.168.1.100:16909060", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, FromStringType4_2) {
    string esi_str("192.168.1.100:4294967295");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::IP_BASED, esi.Type());
    EXPECT_EQ("192.168.1.100:4294967295", esi.ToString());
}

TEST_F(EthernetSegmentIdTest, ByteArrayType5_1) {
    uint8_t data[] = { 0x05, 0x00, 0x00, 0xff, 0x84, 0x01, 0x02, 0x03, 0x04, 0x00 };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::AS_BASED, esi.Type());
    EXPECT_EQ("65412:16909060", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, ByteArrayType5_2) {
    uint8_t data[] = { 0x05, 0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04, 0x00 };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::AS_BASED, esi.Type());
    EXPECT_EQ("4294967295:16909060", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType5_1) {
    string esi_str("65412:16909060");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::AS_BASED, esi.Type());
    EXPECT_EQ("65412:16909060", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringType5_2) {
    string esi_str("4294967295:16909060");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(EthernetSegmentId::AS_BASED, esi.Type());
    EXPECT_EQ("4294967295:16909060", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, ByteArrayTypeX) {
    uint8_t data[] = { 0xff, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi(data);
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(0xFF, esi.Type());
    EXPECT_EQ("ff:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, FromStringTypeX) {
    string esi_str("ff:00:01:23:45:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_FALSE(esi.IsZero());
    EXPECT_EQ(0xFF, esi.Type());
    EXPECT_EQ("ff:00:01:23:45:67:89:ab:cd:ef", esi.ToString());
    EXPECT_EQ(EthernetSegmentId(esi.GetData()), esi);
}

TEST_F(EthernetSegmentIdTest, Compare1) {
    uint8_t data1[] = { 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi1(data1);
    uint8_t data2[] = { 0xff, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi2(data2);
    EXPECT_LT(esi1, esi2);
    EXPECT_GT(esi2, esi1);
}

TEST_F(EthernetSegmentIdTest, Compare2) {
    uint8_t data1[] = { 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    EthernetSegmentId esi1(data1);
    uint8_t data2[] = { 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xff };
    EthernetSegmentId esi2(data2);
    EXPECT_LT(esi1, esi2);
    EXPECT_GT(esi2, esi1);
}

// Wrong number of colons, 8 instead of 9.
TEST_F(EthernetSegmentIdTest, FromString_Error1) {
    string esi_str("00:00:01:23:45:67:89:ab:cd");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Wrong number of colons, 10 instead of 9.
TEST_F(EthernetSegmentIdTest, FromString_Error2) {
    string esi_str("00:00:01:23:45:67:89:ab:cd:ef:00");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Trailing garbage.
TEST_F(EthernetSegmentIdTest, FromString_Error3) {
    string esi_str("00:00:01:23:45:67:89:ab:cd:efx");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Trailing garbage.
TEST_F(EthernetSegmentIdTest, FromString_Error4) {
    string esi_str("00:00:01:23:45:67:89:ab:cd:efX");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Bad digits.
TEST_F(EthernetSegmentIdTest, FromString_Error5) {
    string esi_str("00:00:01:23:xx:67:89:ab:cd:ef");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Wrong number of dots.
TEST_F(EthernetSegmentIdTest, FromString_Error6) {
    string esi_str("10.1.1:65536");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Bad IP address.
TEST_F(EthernetSegmentIdTest, FromString_Error7) {
    string esi_str("10.1.1.256:65536");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Bad discriminator for IP based.
TEST_F(EthernetSegmentIdTest, FromString_Error8) {
    string esi_str("10.1.1.1:65536x");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Bad ASN.
TEST_F(EthernetSegmentIdTest, FromString_Error9) {
    string esi_str("64512x:65536");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

// Bad discriminator for ASN based.
TEST_F(EthernetSegmentIdTest, FromString_Error10) {
    string esi_str("64512:65536x");
    boost::system::error_code ec;
    EthernetSegmentId esi = EthernetSegmentId::FromString(esi_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(EthernetSegmentId::kMaxEsi, esi);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
