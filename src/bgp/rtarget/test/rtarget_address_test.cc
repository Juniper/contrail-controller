/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_address.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class RouteTargetTest : public ::testing::Test {
};

TEST_F(RouteTargetTest, ByteArrayType0_1) {
    RouteTarget::bytes_type data =
	    { { 0x00, 0x02, 0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:16909060", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType0_2) {
    RouteTarget::bytes_type data =
	    { { 0x00, 0x02, 0xff, 0x84, 0x04, 0x03, 0x02, 0x01 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:67305985", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType0_3) {
    RouteTarget::bytes_type data =
	    { { 0x00, 0x02, 0xff, 0x84, 0x00, 0x00, 0x00, 0x00 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:0", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType0_4) {
    RouteTarget::bytes_type data =
	    { { 0x00, 0x02, 0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:4294967295", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType1_1) {
    RouteTarget::bytes_type data =
	    { { 0x01, 0x02, 0x0a, 0x01, 0x01, 0x01, 0x12, 0x34 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:4660", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType1_2) {
    RouteTarget::bytes_type data =
	    { { 0x01, 0x02, 0x0a, 0x01, 0x01, 0x01, 0x43, 0x21 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:17185", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType1_3) {
    RouteTarget::bytes_type data =
	    { { 0x01, 0x02, 0x0a, 0x01, 0x01, 0x01, 0x00, 0x00 } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:0", rtarget.ToString());
}

TEST_F(RouteTargetTest, ByteArrayType1_4) {
    RouteTarget::bytes_type data =
	    { { 0x01, 0x02, 0x0a, 0x01, 0x01, 0x01, 0xFF, 0xFF } };
    RouteTarget rtarget(data);
    EXPECT_FALSE(rtarget.IsNull());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:65535", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType0_1) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:16909060", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:16909060", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType0_2) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:67305985", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:67305985", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType0_3) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:0", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType0_4) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:4294967295", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:65412:4294967295", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType1_1) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:4660", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:4660", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType1_2) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:17185", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:17185", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType1_3) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:0", rtarget.ToString());
}

TEST_F(RouteTargetTest, FromStringType1_4) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(1, rtarget.Type());
    EXPECT_EQ(2, rtarget.Subtype());
    EXPECT_EQ("target:10.1.1.1:65535", rtarget.ToString());
}

// Does not contain a colon.
TEST_F(RouteTargetTest, Error1) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target-10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// Does not contain keyword target.
TEST_F(RouteTargetTest, Error2) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("rtarget:10.1.1.1:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// Does not contain the second colon.
TEST_F(RouteTargetTest, Error3) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number is 0.
TEST_F(RouteTargetTest, ErrorType0_1) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number is 65535.
TEST_F(RouteTargetTest, ErrorType0_2) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("65535:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number is greater than 65535.
TEST_F(RouteTargetTest, ErrorType0_3) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("65536:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(RouteTargetTest, ErrorType0_4) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(RouteTargetTest, ErrorType0_5) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(RouteTargetTest, ErrorType0_6) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// IP address is bad.
TEST_F(RouteTargetTest, ErrorType1_1) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.256:4660", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// IP address assigned number subfield is too big.
TEST_F(RouteTargetTest, ErrorType1_2) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(RouteTargetTest, ErrorType1_3) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(RouteTargetTest, ErrorType1_4) {
    boost::system::error_code ec;
    RouteTarget rtarget =
        RouteTarget::FromString("target:10.1.1.1:1.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rtarget.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
