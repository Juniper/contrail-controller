/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/default_gateway.h"
#include "testing/gunit.h"

class DefaultGatewayTest : public ::testing::Test {
};

TEST_F(DefaultGatewayTest, ByteArray_1) {
    DefaultGateway::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::DefaultGateway,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    DefaultGateway dgw(data);
    EXPECT_EQ("defaultgw:0", dgw.ToString());
}

TEST_F(DefaultGatewayTest, ByteArray_2) {
    DefaultGateway::bytes_type data =
        { { BgpExtendedCommunityType::Opaque,
              BgpExtendedCommunityOpaqueSubType::DefaultGateway,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } };
    DefaultGateway dgw(data);
    EXPECT_EQ("defaultgw:0", dgw.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
