/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/vrf_route_import.h"

#include "testing/gunit.h"

using namespace std;

class VrfRouteImportTest : public ::testing::Test {
};

TEST_F(VrfRouteImportTest, ByteArrayType1_1) {
    VrfRouteImport::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::VrfRouteImport,
        0x0a, 0x01, 0x01, 0x01, 0x12, 0x34
    } };
    VrfRouteImport rt_import(data);
    EXPECT_FALSE(rt_import.IsNull());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:4660", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, ByteArrayType1_2) {
    VrfRouteImport::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::VrfRouteImport,
        0x0a, 0x01, 0x01, 0x01, 0x43, 0x21
    } };
    VrfRouteImport rt_import(data);
    EXPECT_FALSE(rt_import.IsNull());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:17185", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, ByteArrayType1_3) {
    VrfRouteImport::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::VrfRouteImport,
        0x0a, 0x01, 0x01, 0x01, 0x00, 0x00
    } };
    VrfRouteImport rt_import(data);
    EXPECT_FALSE(rt_import.IsNull());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:0", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, ByteArrayType1_4) {
    VrfRouteImport::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::VrfRouteImport,
        0x0a, 0x01, 0x01, 0x01, 0xFF, 0xFF
    } };
    VrfRouteImport rt_import(data);
    EXPECT_FALSE(rt_import.IsNull());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:65535", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, FromStringType1_1) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:4660", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:4660", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, FromStringType1_2) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:17185", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:17185", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, FromStringType1_3) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:0", rt_import.ToString());
}

TEST_F(VrfRouteImportTest, FromStringType1_4) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rt_import.Type());
    EXPECT_EQ(11, rt_import.Subtype());
    EXPECT_EQ("rt-import:10.1.1.1:65535", rt_import.ToString());
}

// Does not contain a colon.
TEST_F(VrfRouteImportTest, Error1) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt_import-10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// Does not contain keyword rt_import.
TEST_F(VrfRouteImportTest, Error2) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rti:10.1.1.1:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// Does not contain the second colon.
TEST_F(VrfRouteImportTest, Error3) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// IP address is bad.
TEST_F(VrfRouteImportTest, ErrorType1_1) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.256:4660", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// assigned number subfield is too big.
TEST_F(VrfRouteImportTest, ErrorType1_2) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// assigned number subfield is bad.
TEST_F(VrfRouteImportTest, ErrorType1_3) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

// assigned number subfield is bad.
TEST_F(VrfRouteImportTest, ErrorType1_4) {
    boost::system::error_code ec;
    VrfRouteImport rt_import =
        VrfRouteImport::FromString("rt-import:10.1.1.1:1.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rt_import.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
