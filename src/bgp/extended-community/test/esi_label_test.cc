/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "bgp/extended-community/esi_label.h"
#include "testing/gunit.h"

class EsiLabelTest : public ::testing::Test {
};

TEST_F(EsiLabelTest, ByteArray_1) {
    EsiLabel::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_EVPN,
            BGP_EXTENDED_COMMUNITY_EVPN_ESI_MPLS_LABEL,
            0x01, 0x00, 0x00, 0x00, 0x3e, 0x81 } };
    EsiLabel esi_label(data);
    EXPECT_EQ("esilabel:sa:1000", esi_label.ToString());
}

TEST_F(EsiLabelTest, ByteArray_2) {
    EsiLabel::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_EVPN,
            BGP_EXTENDED_COMMUNITY_EVPN_ESI_MPLS_LABEL,
            0x00, 0x00, 0x00, 0x00, 0x3e, 0x81 } };
    EsiLabel esi_label(data);
    EXPECT_EQ("esilabel:aa:1000", esi_label.ToString());
}

TEST_F(EsiLabelTest, ByteArray_3) {
    EsiLabel::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_EVPN,
            BGP_EXTENDED_COMMUNITY_EVPN_ESI_MPLS_LABEL,
            0x01, 0x00, 0x00, 0xff, 0xff, 0xf1 } };
    EsiLabel esi_label(data);
    EXPECT_EQ("esilabel:sa:1048575", esi_label.ToString());
}

TEST_F(EsiLabelTest, ByteArray_4) {
    EsiLabel::bytes_type data =
        { { BGP_EXTENDED_COMMUNITY_TYPE_EVPN,
            BGP_EXTENDED_COMMUNITY_EVPN_ESI_MPLS_LABEL,
            0x00, 0x00, 0x00, 0xff, 0xff, 0xf1 } };
    EsiLabel esi_label(data);
    EXPECT_EQ("esilabel:aa:1048575", esi_label.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
