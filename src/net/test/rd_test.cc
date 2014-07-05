/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/rd.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class RouteDistinguisherTest : public ::testing::Test {
};

TEST_F(RouteDistinguisherTest, ByteArrayType0_0) {
    uint8_t data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    RouteDistinguisher rd(data);
    EXPECT_TRUE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("0:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType0_1) {
    uint8_t data[] = { 0x00, 0x00, 0xff, 0x84, 0x01, 0x02, 0x03, 0x04 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:16909060", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType0_2) {
    uint8_t data[] = { 0x00, 0x00, 0xff, 0x84, 0x04, 0x03, 0x02, 0x01 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:67305985", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType0_3) {
    uint8_t data[] = { 0x00, 0x00, 0xff, 0x84, 0x00, 0x00, 0x00, 0x00 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType0_4) {
    uint8_t data[] = { 0x00, 0x00, 0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:4294967295", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType1_1) {
    uint8_t data[] = { 0x00, 0x01, 0x0a, 0x01, 0x01, 0x01, 0x12, 0x34 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:4660", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType1_2) {
    uint8_t data[] = { 0x00, 0x01, 0x0a, 0x01, 0x01, 0x01, 0x43, 0x21 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:17185", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType1_3) {
    uint8_t data[] = { 0x00, 0x01, 0x0a, 0x01, 0x01, 0x01, 0x00, 0x00 };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, ByteArrayType1_4) {
    uint8_t data[] = { 0x00, 0x01, 0x0a, 0x01, 0x01, 0x01, 0xFF, 0xFF };
    RouteDistinguisher rd(data);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:65535", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType0_0) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("0:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("0:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType0_1) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("65412:16909060", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:16909060", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType0_2) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("65412:67305985", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:67305985", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType0_3) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("65412:0", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType0_4) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("65412:4294967295", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(0, rd.Type());
    EXPECT_EQ("65412:4294967295", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType1_1) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("10.1.1.1:4660", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:4660", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType1_2) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("10.1.1.1:17185", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:17185", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType1_3) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("10.1.1.1:0", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:0", rd.ToString());
}

TEST_F(RouteDistinguisherTest, FromStringType1_4) {
    boost::system::error_code ec;
    RouteDistinguisher rd =
        RouteDistinguisher::FromString("10.1.1.1:65535", &ec);
    EXPECT_FALSE(rd.IsNull());
    EXPECT_EQ(1, rd.Type());
    EXPECT_EQ("10.1.1.1:65535", rd.ToString());
}

// Does not contain a colon.
TEST_F(RouteDistinguisherTest, Error) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("10.1.1.1-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number is 0.
TEST_F(RouteDistinguisherTest, ErrorType0_1) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("0:16909060", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number is 65535.
TEST_F(RouteDistinguisherTest, ErrorType0_2) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("65535:16909060", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number is greater than 65535.
TEST_F(RouteDistinguisherTest, ErrorType0_3) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("65536:16909060", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(RouteDistinguisherTest, ErrorType0_4) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(RouteDistinguisherTest, ErrorType0_5) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(RouteDistinguisherTest, ErrorType0_6) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// IP address is bad.
TEST_F(RouteDistinguisherTest, ErrorType1_1) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("10.1.1.256:4660", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// IP address assigned number subfield is too big.
TEST_F(RouteDistinguisherTest, ErrorType1_2) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("10.1.1.1:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(RouteDistinguisherTest, ErrorType1_3) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("10.1.1.1:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

// IP address assigned number subfield is bad.
TEST_F(RouteDistinguisherTest, ErrorType1_4) {
    boost::system::error_code ec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("10.1.1.1:1.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(rd.IsNull());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
