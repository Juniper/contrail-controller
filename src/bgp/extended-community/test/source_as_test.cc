/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/source_as.h"

#include "testing/gunit.h"

using namespace std;

class SourceAsTest : public ::testing::Test {
};

TEST_F(SourceAsTest, ByteArrayType0_1) {
    SourceAs::bytes_type data =
    { { BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:16909060", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType0_2) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x04, 0x03, 0x02, 0x01
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:67305985", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType0_3) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:0", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType0_4) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::TwoOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:4294967295", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType2_1) {
    SourceAs::bytes_type data =
    { { BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:4286841090:772", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType2_2) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x04, 0x03, 0x02, 0x01
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:4286841859:513", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType2_3) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:4286840832:0", sas.ToString());
}

TEST_F(SourceAsTest, ByteArrayType2_4) {
    SourceAs::bytes_type data = { {
        BgpExtendedCommunityType::FourOctetAS,
        BgpExtendedCommunitySubType::SourceAS,
        0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF
    } };
    SourceAs sas(data);
    EXPECT_FALSE(sas.IsNull());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:4286906367:65535", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType0_1) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:16909060", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:16909060", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType0_2) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:67305985", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:67305985", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType0_3) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:0", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType0_4) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:4294967295", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65412:4294967295", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType2_1) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:6541200:1690", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:6541200:1690", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType2_2) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:6541200:6730", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:6541200:6730", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType2_3) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:6541200:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:6541200:0", sas.ToString());
}

TEST_F(SourceAsTest, FromStringType2_4) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:4294967295:65412", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(2, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:4294967295:65412", sas.ToString());
}

// Does not contain the second colon.
TEST_F(SourceAsTest, Error) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

// AS number is 0.
TEST_F(SourceAsTest, ErrorType0_1) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

// AS number is 65535.
TEST_F(SourceAsTest, ErrorType0_2) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65535:100", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, sas.Type());
    EXPECT_EQ(9, sas.Subtype());
    EXPECT_EQ("source-as:65535:100", sas.ToString());
}

// AS number is greater than 4294967295.
TEST_F(SourceAsTest, ErrorType0_3) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:4294967296:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(SourceAsTest, ErrorType0_4) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SourceAsTest, ErrorType0_5) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SourceAsTest, ErrorType0_6) {
    boost::system::error_code ec;
    SourceAs sas =
        SourceAs::FromString("source-as:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sas.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
