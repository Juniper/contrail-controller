/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/test/test_flow_util.h"
#include "uve/test/test_uve_util.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};

VmInterface *flow0;
VmInterface *flow1;

class TestTsnFlow : public ::testing::Test {
public:
    TestTsnFlow() : agent_(NULL), flow_proto_(NULL), util_() {
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        client->WaitForIdle();
    }

    void FlowSetUp() {
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 0)));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 1)));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);

        util_.FlowStatsTimerStartStop(true);
        client->WaitForIdle(10);
    }

    void FlowTearDown() {
        client->Reset();
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        WAIT_FOR(1000, 1000, (VmPortFind(input, 0) == false));
        WAIT_FOR(1000, 1000, (VmPortFind(input, 1) == false));
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        agent_->stats()->Reset();
    }

    virtual void TearDown() {
    }

protected:
    Agent *agent_;
    FlowProto *flow_proto_;
    TestUveUtil util_;
};


//Verify that flows are created and aged out by TSN
TEST_F(TestTsnFlow, FlowCreateDel_1) {
    EXPECT_EQ(0U, agent_->stats()->flow_created());
    EXPECT_EQ(0U, agent_->stats()->flow_aged());
    FlowSetUp();
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        }
    };

    CreateFlow(flow, 1);
    //Verify created flows
    EXPECT_EQ(2U, agent_->stats()->flow_created());

    FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(f1 != NULL);
    EXPECT_TRUE(f1->is_flags_set(FlowEntry::ShortFlow));
    EXPECT_TRUE(f1->short_flow_reason() == FlowEntry::SHORT_FLOW_ON_TSN);

    FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(rev != NULL);
    EXPECT_TRUE(rev->is_flags_set(FlowEntry::ShortFlow));
    EXPECT_TRUE(rev->short_flow_reason() == FlowEntry::SHORT_FLOW_ON_TSN);

    //Invoke FlowStatsCollector to trigger aging
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    util_.FlowStatsTimerStartStop(true);
    client->WaitForIdle(10);

    //Verify aged flows
    EXPECT_EQ(2U, agent_->stats()->flow_aged());

    //cleanup
    FlowTearDown();
    EXPECT_EQ(0U, flow_proto_->FlowCount());
}


int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    strcpy(init_file, DEFAULT_VNSW_TSN_CONFIG_FILE);
    //client = TestInit(init_file, ksync_init);
    client = TestInit(init_file, ksync_init, true, true, true,
                      AgentParam::kAgentStatsInterval,
                      (AgentParam::kFlowStatsInterval * 1000 * 1000));
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
