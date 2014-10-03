/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/tunnel_encap/tunnel_encap.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class TunnelEncapTest : public ::testing::Test {
};

TEST_F(TunnelEncapTest, String_1) {
    TunnelEncap tunnel_encap("gre");
    EXPECT_EQ(TunnelEncapType::MPLS_O_GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, String_2) {
    TunnelEncap tunnel_encap("udp");
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, String_3) {
    TunnelEncap tunnel_encap("vxlan");
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, String_4) {
    TunnelEncap tunnel_encap("other");
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, EncapType_1) {
    TunnelEncap tunnel_encap(TunnelEncapType::MPLS_O_GRE);
    EXPECT_EQ(TunnelEncapType::MPLS_O_GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, EncapType_2) {
    TunnelEncap tunnel_encap(TunnelEncapType::MPLS_O_UDP);
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, EncapType_3) {
    TunnelEncap tunnel_encap(TunnelEncapType::VXLAN);
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, EncapType_4) {
    TunnelEncap tunnel_encap(TunnelEncapType::UNSPEC);
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, ByteArray_1) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::MPLS_O_GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, ByteArray_2) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x90, 0x89 } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, ByteArray_3) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x90, 0x8a } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, ByteArray_4) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xde } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
