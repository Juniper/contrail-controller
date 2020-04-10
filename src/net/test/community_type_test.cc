/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "net/community_type.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class CommunityTypeTest : public ::testing::Test {
};

TEST_F(CommunityTypeTest, FromString_0) {
    std::string str = "no-export";
    uint32_t community = CommunityType::CommunityFromString(str);
    std::string result = CommunityType::CommunityToString(community);
    EXPECT_EQ(str, result);
}

TEST_F(CommunityTypeTest, FromString_1) {
    std::string str = "1234:1234";
    uint32_t community = CommunityType::CommunityFromString(str);
    std::string result = CommunityType::CommunityToString(community);
    EXPECT_EQ(str, result);
}

TEST_F(CommunityTypeTest, FromString_2) {
    std::string str = "65535:1";
    boost::system::error_code ec;
    uint32_t community = CommunityType::CommunityFromString(str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(community, 0x0U);
}

TEST_F(CommunityTypeTest, FromString_3) {
    std::string str = "accept-own";
    boost::system::error_code ec;
    uint32_t community = CommunityType::CommunityFromString(str, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(community, 0xFFFF0001U);
}

TEST_F(CommunityTypeTest, ToString_0) {
    uint32_t comm = 0x00020001;
    boost::system::error_code ec;
    std::string community = CommunityType::CommunityToString(comm);
    uint32_t result = CommunityType::CommunityFromString(community, &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(result, comm);
}

TEST_F(CommunityTypeTest, ToString_1) {
    uint32_t comm = 0xFFFF0001;
    std::string community = CommunityType::CommunityToString(comm);
    EXPECT_EQ(community, "accept-own");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
