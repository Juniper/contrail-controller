/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/mac_mobility.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class MacMobilityTest : public ::testing::Test {
};

TEST_F(MacMobilityTest, ByteArray_1) {
    MacMobility::bytes_type data =
	    { { 0x06, 0x00, 0x01, 0x00, 0x0, 0x0, 0x0, 0x1 } };
    MacMobility mac_mobility(data);
    EXPECT_EQ("seq-no:1", mac_mobility.ToString());
}

TEST_F(MacMobilityTest, Init) {
    boost::system::error_code ec;
    MacMobility mac_mobility(0x1);
    EXPECT_EQ(mac_mobility.ToString(), "seq-no:1");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
