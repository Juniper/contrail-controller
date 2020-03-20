/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/sub_cluster.h"
#include "testing/gunit.h"

using namespace std;

class SubClusterTest : public ::testing::Test {
};

// Check type is Experimental(0x80) and subtype is 0x85
TEST_F(SubClusterTest, ByteArrayType2ByteAsn) {
    SubCluster::bytes_type data =
    { { BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xFF, 0x84, 0x0, 0x0, 0x0, 0x1 } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65412:1", sub_cluster.ToString());
}

// Check type is Experimental4ByteAs(0x82) and subtype is 0x85
TEST_F(SubClusterTest, ByteArrayType4ByteAsn) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xFF ,0xFF ,0xFF, 0xFF, 0xFF, 0xFF
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4294967295:65535", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ToString2ByteAsn) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunitySubType::SubCluster,
        0xFF ,0xFF ,0xFF, 0xFF, 0xFF, 0xFF
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65535:4294967295", sub_cluster.ToString());
}

TEST_F(SubClusterTest, ToString4ByteAsn) {
    SubCluster::bytes_type data = { {
        BgpExtendedCommunityType::Experimental4ByteAs,
        BgpExtendedCommunitySubType::SubCluster,
        0xFF ,0xFF ,0xFF, 0xFF, 0xFF, 0xFF
    } };
    SubCluster sub_cluster(data);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4294967295:65535", sub_cluster.ToString());
}

TEST_F(SubClusterTest, FromString2ByteAsn) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65535:4294967295", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ(65535, sub_cluster.GetAsn());
    EXPECT_EQ(4294967295, sub_cluster.GetId());
}

TEST_F(SubClusterTest, FromString4ByteAsn) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295:65535", &ec);
    EXPECT_EQ(0, ec.value());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ(4294967295, sub_cluster.GetAsn());
    EXPECT_EQ(65535, sub_cluster.GetId());
}

// ASN is 0
TEST_F(SubClusterTest, FromStringInvalidAsn_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:0:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// ASN is negative number
TEST_F(SubClusterTest, FromStringInvalidAsn_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:-4294967295:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// ASN > 0xFFFFFFFF
TEST_F(SubClusterTest, FromStringInvalidAsn_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967296:65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-id is 0
TEST_F(SubClusterTest, FromString2ByteAsnInvalidId_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65535:0", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-id > 0xFFFFFFFF if asn <= 0xFFFF
TEST_F(SubClusterTest, FromString2ByteAsnInvalidId_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65535:4294967296", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// id is negative number
TEST_F(SubClusterTest, FromString2ByteAsnInvalidId_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65535:-4294967295", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-id > 0xFFFF if 0xFFFF < asn <= 0xFFFFFFFF
TEST_F(SubClusterTest, FromString4ByteAsnInvalidId_1) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65536:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// sub-cluster-id > 0xFFFF if asn == 0xFFFFFFFF
TEST_F(SubClusterTest, FromString4ByteAsnInvalidId_2) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295:65536", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// id is negative number
TEST_F(SubClusterTest, FromString4ByteAsnInvalidId_3) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:4294967295:-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// Does not contain the second colon.
TEST_F(SubClusterTest, FromStringInvalidString) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412-65535", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// 0xFFFF < sub-cluster-id <= 0xFFFFFFFF
TEST_F(SubClusterTest, ValidIdRange2ByteAsn) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:65536", &ec);
    EXPECT_EQ(65536, sub_cluster.GetId());
}

// sub-cluster-id field is bad.
TEST_F(SubClusterTest, ErrorExperimental4ByteAs_7) {
    boost::system::error_code ec;
    SubCluster sub_cluster =
        SubCluster::FromString("subcluster:65412:10.1.1.1", &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_TRUE(sub_cluster.IsNull());
}

// For asn <= 0xFFFF, sub-cluster-id should be in 0x1-0xFFFFFFFF
TEST_F(SubClusterTest, ValidSubCluster2ByteAsn) {
    boost::system::error_code ec;
    SubCluster sub_cluster(65535, 4294967295);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x80, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:65535:4294967295", sub_cluster.ToString());
}

// For asn > 0xFFFF, sub-cluster-id should be in 0x1-0xFFFF
TEST_F(SubClusterTest, ValidSubCluster4ByteAsn) {
    boost::system::error_code ec;
    SubCluster sub_cluster(4294967295, 65535);
    EXPECT_FALSE(sub_cluster.IsNull());
    EXPECT_EQ(0x82, sub_cluster.Type());
    EXPECT_EQ(0x85, sub_cluster.Subtype());
    EXPECT_EQ("subcluster:4294967295:65535", sub_cluster.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
