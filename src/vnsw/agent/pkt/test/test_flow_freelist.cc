/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

class FlowTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        flow_table_ = flow_proto_->GetTable(0);
        free_list_ = flow_table_->free_list();

        total_alloc_ = free_list_->total_alloc();
        total_free_ = free_list_->total_free();

        EXPECT_GE(free_list_->max_count(), FlowEntryFreeList::kTestInitCount);

        ksync_free_list_ = flow_table_->ksync_object()->free_list();
        ksync_total_alloc_ = ksync_free_list_->total_alloc();
        ksync_total_free_ = ksync_free_list_->total_free();
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    FlowTable *flow_table_;
    FlowEntryFreeList *free_list_;
    KSyncFlowEntryFreeList *ksync_free_list_;

    uint64_t total_alloc_;
    uint64_t total_free_;

    uint64_t ksync_total_alloc_;
    uint64_t ksync_total_free_;
};

TEST_F(FlowTest, Alloc_Free_1) {
    FlowKey key;
    FlowEntry *flow = free_list_->Allocate(key);

    EXPECT_TRUE(flow != NULL);
    EXPECT_EQ((total_alloc_ + 1), free_list_->total_alloc());
    EXPECT_EQ(total_free_, free_list_->total_free());

    free_list_->Free(flow);
    EXPECT_EQ((total_alloc_ + 1), free_list_->total_alloc());
    EXPECT_EQ((total_free_ + 1), free_list_->total_free());
}

TEST_F(FlowTest, Alloc_No_Grow_1) {
    uint32_t count = (free_list_->max_count()
                      - FlowEntryFreeList::kMinThreshold - 1);
    std::list<FlowEntry *> flow_list;
    for (uint32_t i = 0; i < count; i++) {
        FlowEntry *flow = free_list_->Allocate(FlowKey());
        flow_list.push_back(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_), free_list_->total_free());

    while (flow_list.size()) {
        FlowEntry *flow = flow_list.back();
        flow_list.pop_back();
        free_list_->Free(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_ + count), free_list_->total_free());
}

TEST_F(FlowTest, Alloc_Grow_1) {
    uint32_t max_count = free_list_->max_count();
    uint32_t count = (free_list_->max_count()
                      - FlowEntryFreeList::kMinThreshold + 1);
    std::list<FlowEntry *> flow_list;
    for (uint32_t i = 0; i < count; i++) {
        FlowEntry *flow = free_list_->Allocate(FlowKey());
        flow_list.push_back(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_), free_list_->total_free());
    EXPECT_EQ((max_count + FlowEntryFreeList::kGrowSize),
              free_list_->max_count());

    while (flow_list.size()) {
        FlowEntry *flow = flow_list.back();
        flow_list.pop_back();
        free_list_->Free(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_ + count), free_list_->total_free());
    EXPECT_EQ((max_count + FlowEntryFreeList::kGrowSize),
              free_list_->max_count());
}

TEST_F(FlowTest, Alloc_Grow_2) {
    uint32_t max_count = free_list_->max_count();
    uint32_t count = free_list_->max_count() + 1;
    flow_proto_->DisableFlowEventQueue(0, true);
    std::list<FlowEntry *> flow_list;
    for (uint32_t i = 0; i < count; i++) {
        FlowEntry *flow = free_list_->Allocate(FlowKey());
        flow_list.push_back(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_), free_list_->total_free());
    EXPECT_EQ((max_count + 1), free_list_->max_count());

    flow_proto_->DisableFlowEventQueue(0, false);
    client->WaitForIdle();
    EXPECT_EQ((max_count + 1 + FlowEntryFreeList::kGrowSize),
              free_list_->max_count());

    while (flow_list.size()) {
        FlowEntry *flow = flow_list.back();
        flow_list.pop_back();
        free_list_->Free(flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((total_alloc_ + count), free_list_->total_alloc());
    EXPECT_EQ((total_free_ + count), free_list_->total_free());
    EXPECT_EQ((max_count + 1 + FlowEntryFreeList::kGrowSize),
              free_list_->max_count());
}

TEST_F(FlowTest, KSync_Alloc_Grow_1) {
    uint32_t max_count = ksync_free_list_->max_count();
    uint32_t count = ksync_free_list_->max_count() + 1;
    flow_proto_->DisableFlowEventQueue(0, true);
    std::list<FlowTableKSyncEntry *> flow_list;

    FlowEntryPtr flow(free_list_->Allocate(FlowKey()));
    FlowTableKSyncEntry tmp(flow_table_->ksync_object(), flow.get(), 0);
    for (uint32_t i = 0; i < count; i++) {
        FlowTableKSyncEntry *ksync_flow = ksync_free_list_->Allocate(&tmp);
        flow_list.push_back(ksync_flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((ksync_total_alloc_ + count), ksync_free_list_->total_alloc());
    EXPECT_EQ((ksync_total_free_), ksync_free_list_->total_free());
    EXPECT_EQ((max_count + 1), ksync_free_list_->max_count());

    flow_proto_->DisableFlowEventQueue(0, false);
    client->WaitForIdle();
    EXPECT_EQ((max_count + 1 + FlowEntryFreeList::kGrowSize),
              ksync_free_list_->max_count());

    while (flow_list.size()) {
        FlowTableKSyncEntry *ksync_flow = flow_list.back();
        flow_list.pop_back();
        ksync_free_list_->Free(ksync_flow);
    }
    client->WaitForIdle();
    EXPECT_EQ((ksync_total_alloc_ + count), ksync_free_list_->total_alloc());
    EXPECT_EQ((ksync_total_free_ + count), ksync_free_list_->total_free());
    EXPECT_EQ((max_count + 1 + FlowEntryFreeList::kGrowSize),
              ksync_free_list_->max_count());
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
