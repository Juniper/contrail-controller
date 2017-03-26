/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/tunnel_encap/tunnel_encap.h"

#include "testing/gunit.h"

using namespace std;

class TunnelEncapTest : public ::testing::Test {
};

TEST_F(TunnelEncapTest, String_1) {
    TunnelEncap tunnel_encap("gre");
    EXPECT_EQ(TunnelEncapType::GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
    EXPECT_EQ("gre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_2) {
    TunnelEncap tunnel_encap("udp");
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
    EXPECT_EQ("udp", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_3) {
    TunnelEncap tunnel_encap("vxlan");
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
    EXPECT_EQ("vxlan", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_4) {
    TunnelEncap tunnel_encap("other");
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
    EXPECT_EQ("unspecified", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_5) {
    TunnelEncap tunnel_encap("nvgre");
    EXPECT_EQ(TunnelEncapType::NVGRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:nvgre", tunnel_encap.ToString());
    EXPECT_EQ("nvgre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_6) {
    TunnelEncap tunnel_encap("mpls");
    EXPECT_EQ(TunnelEncapType::MPLS, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:mpls", tunnel_encap.ToString());
    EXPECT_EQ("mpls", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, String_7) {
    TunnelEncap tunnel_encap("vxlan-gpe");
    EXPECT_EQ(TunnelEncapType::VXLAN_GPE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan-gpe", tunnel_encap.ToString());
    EXPECT_EQ("vxlan-gpe", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_1a) {
    TunnelEncap tunnel_encap(TunnelEncapType::GRE);
    EXPECT_EQ(TunnelEncapType::GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
    EXPECT_EQ("gre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_1b) {
    TunnelEncap tunnel_encap(TunnelEncapType::MPLS_O_GRE);
    EXPECT_EQ(TunnelEncapType::MPLS_O_GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:mpls-o-gre", tunnel_encap.ToString());
    EXPECT_EQ("gre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_2) {
    TunnelEncap tunnel_encap(TunnelEncapType::MPLS_O_UDP);
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
    EXPECT_EQ("udp", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_3) {
    TunnelEncap tunnel_encap(TunnelEncapType::VXLAN);
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
    EXPECT_EQ("vxlan", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_4) {
    TunnelEncap tunnel_encap(TunnelEncapType::UNSPEC);
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
}

TEST_F(TunnelEncapTest, EncapType_5) {
    TunnelEncap tunnel_encap(TunnelEncapType::NVGRE);
    EXPECT_EQ(TunnelEncapType::NVGRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:nvgre", tunnel_encap.ToString());
    EXPECT_EQ("nvgre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_6) {
    TunnelEncap tunnel_encap(TunnelEncapType::MPLS);
    EXPECT_EQ(TunnelEncapType::MPLS, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:mpls", tunnel_encap.ToString());
    EXPECT_EQ("mpls", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, EncapType_7) {
    TunnelEncap tunnel_encap(TunnelEncapType::VXLAN_GPE);
    EXPECT_EQ(TunnelEncapType::VXLAN_GPE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan-gpe", tunnel_encap.ToString());
    EXPECT_EQ("vxlan-gpe", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_1a) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:gre", tunnel_encap.ToString());
    EXPECT_EQ("gre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_1b) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::MPLS_O_GRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:mpls-o-gre", tunnel_encap.ToString());
    EXPECT_EQ("gre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_2) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::MPLS_O_UDP, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:udp", tunnel_encap.ToString());
    EXPECT_EQ("udp", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_3) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::VXLAN, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan", tunnel_encap.ToString());
    EXPECT_EQ("vxlan", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_4) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xde } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::UNSPEC, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:unspecified", tunnel_encap.ToString());
    EXPECT_EQ("unspecified", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_5) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::NVGRE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:nvgre", tunnel_encap.ToString());
    EXPECT_EQ("nvgre", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_6) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::MPLS, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:mpls", tunnel_encap.ToString());
    EXPECT_EQ("mpls", tunnel_encap.ToXmppString());
}

TEST_F(TunnelEncapTest, ByteArray_7) {
    TunnelEncap::bytes_type data =
        { { 0x03, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c } };
    TunnelEncap tunnel_encap(data);
    EXPECT_EQ(TunnelEncapType::VXLAN_GPE, tunnel_encap.tunnel_encap());
    EXPECT_EQ("encapsulation:vxlan-gpe", tunnel_encap.ToString());
    EXPECT_EQ("vxlan-gpe", tunnel_encap.ToXmppString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
