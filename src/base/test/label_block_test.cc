/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include "base/label_block.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;

//
// Note that even though none of the tests call RemoveBlock explicitly, we
// do end up exercising that method because of the use of intrusive ptrs.
//
class LabelBlockTest : public ::testing::Test {
public:
    void ConcurrencyRun();

protected:
    virtual void SetUp() {
        manager_ = LabelBlockManagerPtr(new LabelBlockManager);
    }

    virtual void TearDown() {
    }

    size_t BlockCount() { return manager_->size(); }

    LabelBlockManagerPtr manager_;
};

TEST_F(LabelBlockTest, Noop) {
}

// Locate a single block once.
TEST_F(LabelBlockTest, LocateBlockNew1) {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    EXPECT_TRUE(block.get() != NULL);
    EXPECT_EQ(1, BlockCount());
}

// Locate multiple blocks once.
TEST_F(LabelBlockTest, LocateBlockNew2) {
    LabelBlockPtr blocks[10];
    for (int idx = 0; idx < 10; idx++) {
        uint32_t start = (idx + 1) * 1000;
        uint32_t end = start + 500 - 1;
        blocks[idx] = manager_->LocateBlock(start, end);
        EXPECT_TRUE(blocks[idx].get() != NULL);
        EXPECT_EQ(idx + 1, BlockCount());
    }
}

// Locate a single block twice.
TEST_F(LabelBlockTest, LocateBlockExisting1) {
    LabelBlockPtr block1 = manager_->LocateBlock(1000, 1500 - 1);
    LabelBlockPtr block2 = manager_->LocateBlock(1000, 1500 - 1);
    EXPECT_TRUE(block1.get() != NULL);
    EXPECT_TRUE(block2.get() != NULL);
    EXPECT_EQ(block1, block2);
}

// Locate multiple blocks twice each.
TEST_F(LabelBlockTest, LocateBlockExisting2) {
    LabelBlockPtr blocks[10];
    for (int idx = 0; idx < 10; idx++) {
        uint32_t start = (idx + 1) * 1000;
        uint32_t end = start + 500 - 1;
        blocks[idx] = manager_->LocateBlock(start, end);
        EXPECT_TRUE(blocks[idx].get() != NULL);
        EXPECT_EQ(idx + 1, BlockCount());
    }
    for (int idx = 0; idx < 10; idx++) {
        uint32_t start = (idx + 1) * 1000;
        uint32_t end = start + 500 - 1;
        LabelBlockPtr block = manager_->LocateBlock(start, end);
        EXPECT_TRUE(block.get() != NULL);
        EXPECT_EQ(block, blocks[idx]);
        EXPECT_EQ(10, BlockCount());
    }
}

// Locate a single block many times.
TEST_F(LabelBlockTest, LocateBlockExisting3) {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    EXPECT_TRUE(block.get() != NULL);
    EXPECT_EQ(1, BlockCount());

    LabelBlockPtr blocks[10];
    for (int idx = 0; idx < 10; idx++) {
        blocks[idx] = manager_->LocateBlock(1000, 1500 - 1);
        EXPECT_TRUE(blocks[idx].get() != NULL);
        EXPECT_EQ(block, blocks[idx]);
        EXPECT_EQ(1, BlockCount());
    }
}

// First allocate all labels in a given block one at a time.
// Then verify that an attempt to allocate one more returns 0.
// Then release all the labels in the order of allocation.
TEST_F(LabelBlockTest, AllocateReleaseLabel1) {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    for (int idx = 0; idx < 500; idx++) {
        uint32_t label = block->AllocateLabel();
        EXPECT_EQ(1000 + idx, label);
    }
    EXPECT_EQ(0, block->AllocateLabel());
    for (int idx = 0; idx < 500; idx++) {
        block->ReleaseLabel(1000 + idx);
    }
}

// First allocate all labels in a given block one at a time.
// Then verify that an attempt to allocate one more returns 0.
// Then release all the labels in reverse order of allocation.
TEST_F(LabelBlockTest, AllocateReleaseLabel2) {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    for (int idx = 0; idx < 500; idx++) {
        uint32_t label = block->AllocateLabel();
        EXPECT_EQ(1000 + idx, label);
    }
    EXPECT_EQ(0, block->AllocateLabel());
    for (int idx = 500 - 1; idx >= 0; idx--) {
        block->ReleaseLabel(1000 + idx);
    }
}

// Allocate and free all labels in a given block.
// Do this twice to check that we wrap around and start allocating from the
// first label in the block the second time around.
TEST_F(LabelBlockTest, AllocateReleaseLabel3) {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    for (int idx = 0; idx < 500; idx++) {
        uint32_t label = block->AllocateLabel();
        EXPECT_EQ(1000 + idx, label);
        block->ReleaseLabel(1000 + idx);
    }
    for (int idx = 0; idx < 500; idx++) {
        uint32_t label = block->AllocateLabel();
        EXPECT_EQ(1000 + idx, label);
        block->ReleaseLabel(1000 + idx);
    }
}

void LabelBlockTest::ConcurrencyRun() {
    LabelBlockPtr block = manager_->LocateBlock(1000, 1500 - 1);
    EXPECT_EQ(1, BlockCount());
}

static void *ConcurrencyThreadRun(void *objp) {
    LabelBlockTest *test = reinterpret_cast<LabelBlockTest *>(objp);
    test->ConcurrencyRun();
    return NULL;
}

TEST_F(LabelBlockTest, LocateBlockConcurrency) {
    std::vector<pthread_t> thread_ids;
    pthread_t tid;

    int thread_count = 1024;
    char *str = getenv("THREAD_COUNT");
    if (str) thread_count = strtoul(str, NULL, 0);

    for (int i = 0; i < thread_count; i++) {
        pthread_create(&tid, NULL, &ConcurrencyThreadRun, this);
        thread_ids.push_back(tid);
    }

    BOOST_FOREACH(tid, thread_ids) { pthread_join(tid, NULL); }
    EXPECT_EQ(0, BlockCount());
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
