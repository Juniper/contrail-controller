/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/etree_leaf.h"

#include "testing/gunit.h"

using namespace std;

class ETreeLeafTest : public ::testing::Test {
};

TEST_F(ETreeLeafTest, ByteArray_1) {
    ETreeLeaf::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::ETreeLeaf,
        0x01, 0x00, 0x0, 0x0, 0x0, 0x0
    } };
    ETreeLeaf etree_leaf(data);
    EXPECT_EQ("etree-leaf:true", etree_leaf.ToString());
}

TEST_F(ETreeLeafTest, ByteArray_2) {
    ETreeLeaf::bytes_type data = { {
        BgpExtendedCommunityType::Evpn,
        BgpExtendedCommunityEvpnSubType::ETreeLeaf,
        0x00, 0x00, 0x0, 0x0, 0x0, 0x0
    } };
    ETreeLeaf etree_leaf(data);
    EXPECT_EQ("etree-leaf:false", etree_leaf.ToString());
}

TEST_F(ETreeLeafTest, Init) {
    boost::system::error_code ec;
    ETreeLeaf etree_leaf(true);
    EXPECT_EQ(etree_leaf.ToString(), "etree-leaf:true");
}

TEST_F(ETreeLeafTest, Init_2) {
    boost::system::error_code ec;
    ETreeLeaf etree_leaf(false);
    EXPECT_EQ(etree_leaf.ToString(), "etree-leaf:false");
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
