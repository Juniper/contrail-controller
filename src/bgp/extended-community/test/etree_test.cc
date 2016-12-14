/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/etree.h"

#include "testing/gunit.h"

using namespace std;

class ETreeTest : public ::testing::Test {
};

TEST_F(ETreeTest, ByteArray_1) {
    ETree::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::ETree,
        0x01, 0x00, 0x0, 0x0, 0x0, 0x0
    } };
    ETree etree(data);
    EXPECT_EQ("etree:leaf:0", etree.ToString());
}

TEST_F(ETreeTest, ByteArray_2) {
    ETree::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::ETree,
        0x00, 0x00, 0x0, 0x1, 0x0, 0x0
    } };
    ETree etree(data);
    EXPECT_EQ("etree:root:4096", etree.ToString());
}

TEST_F(ETreeTest, ByteArray_3) {
    ETree::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::ETree,
        0x00, 0x00, 0x0, 0x0, 0x1, 0x0
    } };
    ETree etree(data);
    EXPECT_EQ("etree:root:16", etree.ToString());
}


TEST_F(ETreeTest, Init) {
    boost::system::error_code ec;
    ETree etree(true);
    EXPECT_EQ(etree.ToString(), "etree:leaf:0");
}

TEST_F(ETreeTest, Init_2) {
    boost::system::error_code ec;
    ETree etree(false);
    EXPECT_EQ(etree.ToString(), "etree:root:0");
}

TEST_F(ETreeTest, Init_3) {
    boost::system::error_code ec;
    ETree etree(false, 100);
    EXPECT_EQ(etree.ToString(), "etree:root:100");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
