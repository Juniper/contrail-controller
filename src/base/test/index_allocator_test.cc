/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/index_allocator.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

class IndexAllocatorTest : public ::testing::Test {
};

TEST_F(IndexAllocatorTest, IndexAllocator_Test) {
    IndexAllocator idx(4);
    EXPECT_EQ(0, idx.AllocIndex());
    EXPECT_EQ(1, idx.AllocIndex());
    EXPECT_EQ(2, idx.AllocIndex());
    idx.FreeIndex(2);
    EXPECT_EQ(3, idx.AllocIndex());
    idx.FreeIndex(0);
    idx.FreeIndex(1);
    EXPECT_EQ(4, idx.AllocIndex());
    EXPECT_EQ(0, idx.AllocIndex());
    EXPECT_EQ(1, idx.AllocIndex());
}


TEST_F(IndexAllocatorTest, Max_IndexAllocator_Test) {
    IndexAllocator idx(2);
    EXPECT_EQ(0, idx.AllocIndex());
    EXPECT_EQ(1, idx.AllocIndex());
    EXPECT_EQ(2, idx.AllocIndex());
    EXPECT_NE(3, idx.AllocIndex());
    EXPECT_EQ(BitSet::npos, idx.AllocIndex());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
