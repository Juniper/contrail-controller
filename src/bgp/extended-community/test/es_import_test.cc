/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/es_import.h"

#include "testing/gunit.h"

class EsImportTest : public ::testing::Test {
};

TEST_F(EsImportTest, ByteArray_1) {
    EsImport::bytes_type data =
        { { BgpExtendedCommunityType::Evpn,
              BgpExtendedCommunityEvpnSubType::EsImport,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } };
    EsImport es_import(data);
    EXPECT_EQ("esimport:01:02:03:04:05:06", es_import.ToString());
}

TEST_F(EsImportTest, ByteArray_2) {
    EsImport::bytes_type data =
        { { BgpExtendedCommunityType::Evpn,
              BgpExtendedCommunityEvpnSubType::EsImport,
            0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff } };
    EsImport es_import(data);
    EXPECT_EQ("esimport:aa:bb:cc:dd:ee:ff", es_import.ToString());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
