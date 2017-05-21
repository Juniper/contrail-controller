/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/extended-community/tag.h"
#include "bgp/extended-community/types.h"

#include "testing/gunit.h"

using namespace std;

class TagTest : public ::testing::Test {
};

TEST_F(TagTest, ByteArray_1) {
    Tag::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunityExperimentalSubType::Tag,
        0x00, 0x00, 0x0, 0x0, 0x0, 0x0
    } };
    Tag tag(data);
    EXPECT_EQ("tag:0:0", tag.ToString());
}

TEST_F(TagTest, ByteArray_2) {
    Tag::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunityExperimentalSubType::Tag,
        0x00, 0x00, 0x0, 0x1, 0x0, 0x0
    } };
    Tag tag(data);
    EXPECT_EQ("tag:0:65536", tag.ToString());
}

TEST_F(TagTest, ByteArray_3) {
    Tag::bytes_type data = { {
        BgpExtendedCommunityType::Experimental,
        BgpExtendedCommunityExperimentalSubType::Tag,
        0x00, 0xff, 0x80, 0x0, 0x0, 0x1
    } };
    Tag tag(data);
    EXPECT_EQ("tag:255:2147483649", tag.ToString());
}


TEST_F(TagTest, Init) {
    Tag tag(100, 100);
    EXPECT_EQ(tag.ToString(), "tag:100:100");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
