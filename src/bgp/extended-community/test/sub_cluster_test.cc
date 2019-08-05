/*gtgtgt
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/sub_cluster.h"

#include "testing/gunit.h"

using namespace std;

class SubClusterTest : public ::testing::Test {
};

TEST_F(SubClusterTest, ByteArrayTypeExperimental_1) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0, 0, 0x03, 0x04 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:772", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental_2) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0, 0x0, 0x02, 0x01
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:513", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental_3) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:0", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental_4) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0, 0, 0xFF, 0xFF
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:65535", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_1) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0x01, 0x02, 0x03, 0x04 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4286841090L:772", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_2) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0x04, 0x03, 0x02, 0x01
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4286841859L:513", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_3) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0x00, 0x00, 0x00, 0x00
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4286840832L:0", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_4) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0xFF, 0xFF, 0xFF, 0xFF
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4286906367L:65535", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:772", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:772", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:513", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:513", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:0", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental_4) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:65535", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental4ByteAs_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:654123456L:772", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:654123456L:772", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental4ByteAs_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:654123456L:0", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:654123456L:0", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental4ByteAs_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:654123456L:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:654123456L:65535", sub_cluster.ToString());
}

// Does not contain the second colon.
TEST_F(SubClusterTest, Error) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is 0.
TEST_F(SubClusterTest, ErrorExperimental_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is 65535.
TEST_F(SubClusterTest, ErrorExperimental_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65535:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is greater than 65535.
TEST_F(SubClusterTest, ErrorExperimental_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65536:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number assigned number subfield too big.
TEST_F(SubClusterTest, ErrorExperimental_4) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:4294967299", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SubClusterTest, ErrorExperimental_5) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:0xffff", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number assigned number subfield is bad.
TEST_F(SubClusterTest, ErrorExperimental_6) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS4 number is 0xFFFFFFFF.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295L:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS4 number is greater than 0xFFFFFFFF.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295L:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS4 sub_cluster_id is greater than 65535.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412L:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// Get sub_cluster_id from subcluster
TEST_F(SubClusterTest, GetSubClusterId1) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0, 0, 0x01, 0x02 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:258", sub_cluster.ToString());
    EXPECT_EQ(0x102, sub_cluster.GetSubClusterId());
}

// Get incorrect sub_cluster_id from subcluster.
TEST_F(SubClusterTest, GetSubClusterId2) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xff, 0x84, 0, 0, 0x01, 0x02 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:258", sub_cluster.ToString());
    EXPECT_NE(0x103, sub_cluster.GetSubClusterId());
}

TEST_F(SubClusterTest, SubClusterExperimental) {
    as_t asn = 65412;
    uint16_t sub_cluster_id = 100;
    SubCluster sub_cluster(asn, BgpExtendedCommunityType::Experimental,
            sub_cluster_id);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:100", sub_cluster.ToString());
}

TEST_F(SubClusterTest, SubClusterExperimental4ByteAs) {
    as_t asn = 65412345;
    uint16_t sub_cluster_id = 100;
    SubCluster sub_cluster(asn, BgpExtendedCommunityType::Experimental4ByteAs,
            sub_cluster_id);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412345L:100", sub_cluster.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
