/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

class TestFlowTable : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        client->WaitForIdle();
        for (int i = 0; i < 4; i++) {
            flow_count_[i] = 0;
        }
    }

    virtual void TearDown() {
    }

    void FlowHash(const char *sip, const char *dip, uint8_t proto,
                  uint16_t sport, uint16_t dport, uint32_t flow_handle) {
        uint32_t idx = flow_proto_->FlowTableIndex
            (Ip4Address::from_string(sip), Ip4Address::from_string(dip), proto,
             sport, dport, flow_handle);
        flow_count_[idx]++;
    }

    void FlowHash(uint32_t sip, uint32_t dip, uint8_t proto, uint16_t sport,
                  uint16_t dport, uint32_t flow_handle) {
        uint32_t idx = flow_proto_->FlowTableIndex
            (Ip4Address(sip), Ip4Address(dip), proto, sport, dport,
             flow_handle);
        flow_count_[idx]++;
    }

protected:
    Agent *agent_;
    FlowProto *flow_proto_;
    uint32_t flow_count_[4];
};

TEST_F(TestFlowTable, TestParam_1) {
    EXPECT_EQ(flow_proto_->flow_table_count(), 4);
}

TEST_F(TestFlowTable, flow_hash_sport_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash("1.1.1.1", "1.1.1.2", 6, i, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_dport_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash("1.1.1.1", "1.1.1.2", 6, 1, i, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_sip_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101+i, 0x1010102, 6, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_dip_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101, 0x1010101+i,  6, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

TEST_F(TestFlowTable, flow_hash_proto_1) {
    for (int i= 0; i < 128; i++) {
        FlowHash(0x1010101, 0x1010101,  i, 1, 1, i);
    }
    EXPECT_TRUE(flow_count_[0] > 0);
    EXPECT_TRUE(flow_count_[1] > 0);
    EXPECT_TRUE(flow_count_[2] > 0);
    EXPECT_TRUE(flow_count_[3] > 0);
}

int main(int argc, char *argv[]) {
    int ret = 0;
    GETUSERARGS();
    strcpy(init_file, "controller/src/vnsw/agent/pkt/test/flow-table.ini");
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
