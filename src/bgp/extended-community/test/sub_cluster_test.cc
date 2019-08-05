/*gtgtgt
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/sub_cluster.h"

#include "testing/gunit.h"

using namespace std;

class SubClusterTest : public ::testing::Test {
};

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_1) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0, 0, 0xff, 0x84, 0x00, 0x01 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:1", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ByteArrayTypeExperimental4ByteAs_2) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0 ,0 ,0xff, 0x84, 0xFF, 0xFE
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:65534", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental4ByteAs_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:1", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:1", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromStringTypeExperimental4ByteAs_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:65534", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:65534", sub_cluster.ToString());
}

// Does not contain the second colon.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is 0.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:0:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is 0xFFFFFFFF.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// AS number is greater than 0xFFFFFFFF.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_4) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967296:100", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-asn equal to 0.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_5) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:0", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-asn greater than 0xFFFF
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_6) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-asn field is bad.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_7) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}


// Get sub_cluster_asn from subcluster
TEST_F(SubClusterTest, GetSubClusterAsn) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0, 0, 0xff, 0x84, 0x01, 0x02 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:258", sub_cluster.ToString());
    EXPECT_EQ(0x102, sub_cluster.SubClusterAsn());
}

// Set sub_cluster_asn
TEST_F(SubClusterTest, SetSubClusterAsn) {
    as_t asn = 65412345;
    uint16_t sub_cluster_asn = 100;
    SubCluster sub_cluster(asn, sub_cluster_asn);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x11, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412345:100", sub_cluster.ToString());
    EXPECT_EQ(100, sub_cluster.SubClusterAsn());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
