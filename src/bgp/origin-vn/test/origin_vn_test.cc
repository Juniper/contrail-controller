/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/origin-vn/origin_vn.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class OriginVnTest : public ::testing::Test {
};

TEST_F(OriginVnTest, ByteArray_1) {
    OriginVn::bytes_type data = {
        { 0x80, 0x71, 0xff, 0x84, 0x01, 0x02, 0x03, 0x04 }
    };
    OriginVn origin_vn(data);
    EXPECT_FALSE(origin_vn.IsNull());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(16909060, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:16909060", origin_vn.ToString());
}

TEST_F(OriginVnTest, ByteArray_2) {
    OriginVn::bytes_type data = {
        { 0x80, 0x71, 0xff, 0x84, 0x04, 0x03, 0x02, 0x01 }
    };
    OriginVn origin_vn(data);
    EXPECT_FALSE(origin_vn.IsNull());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(67305985, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:67305985", origin_vn.ToString());
}

TEST_F(OriginVnTest, ByteArray_3) {
    OriginVn::bytes_type data = {
        { 0x80, 0x71, 0xff, 0x84, 0x00, 0x00, 0x00, 0x00 }
    };
    OriginVn origin_vn(data);
    EXPECT_FALSE(origin_vn.IsNull());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(0, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:0", origin_vn.ToString());
}

TEST_F(OriginVnTest, ByteArray_4) {
    OriginVn::bytes_type data = {
        { 0x80, 0x71, 0xff, 0x84, 0x7F, 0xFF, 0xFF, 0xFF }
    };
    OriginVn origin_vn(data);
    EXPECT_FALSE(origin_vn.IsNull());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(2147483647, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:2147483647", origin_vn.ToString());
}

TEST_F(OriginVnTest, FromString_1) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:16909060", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(16909060, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:16909060", origin_vn.ToString());
}

TEST_F(OriginVnTest, FromString_2) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:67305985", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(67305985, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:67305985", origin_vn.ToString());
}

TEST_F(OriginVnTest, FromString_3) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(0, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:0", origin_vn.ToString());
}

TEST_F(OriginVnTest, FromString_4) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:2147483647", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(65412, origin_vn.as_number());
    EXPECT_EQ(2147483647, origin_vn.vn_index());
    EXPECT_EQ("originvn:65412:2147483647", origin_vn.ToString());
}

// Does not contain a colon.
TEST_F(OriginVnTest, Error_1) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn-10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// Does not contain keyword originvn.
TEST_F(OriginVnTest, Error_2) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("origin:10.1.1.1:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// Does not contain the second colon.
TEST_F(OriginVnTest, Error_3) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number is 0.
TEST_F(OriginVnTest, Error_4) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number is 65535.
TEST_F(OriginVnTest, Error_5) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("65535:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number is greater than 65535.
TEST_F(OriginVnTest, Error_6) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("65536:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(OriginVnTest, Error_7) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(OriginVnTest, Error_8) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(OriginVnTest, Error_9) {
    boost::system::error_code ec;
    OriginVn origin_vn = OriginVn::FromString("originvn:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(origin_vn.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
