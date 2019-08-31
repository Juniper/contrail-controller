/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/site_of_origin.h"

#include "testing/gunit.h"

using namespace std;

class SiteOfOriginTest : public ::testing::Test {
};

TEST_F(SiteOfOriginTest, ByteArrayType0_1) {
    SiteOfOrigin::bytes_type data =
    { { BgpExtendedCommunityType::TwoOctetAS,
          BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:16909060", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType0_2) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x04, 0x03, 0x02, 0x01
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:67305985", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType0_3) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType0_4) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:4294967295", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType1_1) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::RouteOrigin,
        0x0a, 0x01, 0x01, 0x01, 0x12, 0x34
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:4660", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType1_2) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::RouteOrigin,
        0x0a, 0x01, 0x01, 0x01, 0x43, 0x21
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:17185", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType1_3) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::RouteOrigin,
        0x0a, 0x01, 0x01, 0x01, 0x00, 0x00
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType1_4) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::IPv4Address,
        BgpExtendedCommunitySubType::RouteOrigin,
        0x0a, 0x01, 0x01, 0x01, 0xFF, 0xFF
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:65535", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType2_1) {
    SiteOfOrigin::bytes_type data =
    { { BgpExtendedCommunityType::FourOctetAS,
          BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:4286841090:772", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType2_2) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x04, 0x03, 0x02, 0x01
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:4286841859:513", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType2_3) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:4286840832:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, ByteArrayType2_4) {
    SiteOfOrigin::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::RouteOrigin,
        0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF
    } };
    SiteOfOrigin soo(data);
    EXPECT_FALSE(soo.IsNull());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:4286906367:65535", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType0_1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:16909060", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:16909060", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType0_2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:67305985", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:67305985", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType0_3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType0_4) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:4294967295", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:65412:4294967295", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType1_1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:4660", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:4660", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType1_2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:17185", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:17185", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType1_3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType1_4) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:10.1.1.1:65535", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType2_1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:6541200:200", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:6541200:200", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType2_2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:6541200:6730", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:6541200:6730", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType2_3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:6541200:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:6541200:0", soo.ToString());
}

TEST_F(SiteOfOriginTest, FromStringType2_4) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:4294967295:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, soo.Type());
    EXPECT_EQ(3, soo.Subtype());
    EXPECT_EQ("soo:4294967295:65535", soo.ToString());
}

// Does not contain a colon.
TEST_F(SiteOfOriginTest, Error1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo-10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// Does not contain keyword soo.
TEST_F(SiteOfOriginTest, Error2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("so:10.1.1.1:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// Does not contain the second colon.
TEST_F(SiteOfOriginTest, Error3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number is 0.
TEST_F(SiteOfOriginTest, ErrorType0_1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number is 65535.
TEST_F(SiteOfOriginTest, ErrorType0_2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("65535:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number is greater than 65535.
TEST_F(SiteOfOriginTest, ErrorType0_3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("65536:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(SiteOfOriginTest, ErrorType0_4) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SiteOfOriginTest, ErrorType0_5) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SiteOfOriginTest, ErrorType0_6) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// IP address is bad.
TEST_F(SiteOfOriginTest, ErrorType1_1) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.256:4660", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// IP address assigned number subfield is too big.
TEST_F(SiteOfOriginTest, ErrorType1_2) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(SiteOfOriginTest, ErrorType1_3) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(SiteOfOriginTest, ErrorType1_4) {
    boost::system::error_code ec;
    SiteOfOrigin soo =
        SiteOfOrigin::FromString("soo:10.1.1.1:1.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(soo.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
