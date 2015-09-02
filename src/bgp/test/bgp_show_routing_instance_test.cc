/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_show_instance_or_table_test.h"

typedef TypeDefinition<
    ShowRoutingInstanceReq,
    ShowRoutingInstanceReqIterate,
    ShowRoutingInstanceResp> RegularReq;

typedef TypeDefinition<
    ShowRoutingInstanceSummaryReq,
    ShowRoutingInstanceSummaryReqIterate,
    ShowRoutingInstanceSummaryResp> SummaryReq;

// Specialization of RequestIsDetail for regular request.
template<>
bool BgpShowInstanceOrTableTest<RegularReq>::RequestIsDetail() const {
    return true;
}

// Instantiate all test patterns for ShowRoutingInstanceReq.
INSTANTIATE_TYPED_TEST_CASE_P(Regular, BgpShowInstanceOrTableTest, RegularReq);

// Instantiate all test patterns for ShowRoutingInstanceSummaryReq.
INSTANTIATE_TYPED_TEST_CASE_P(Summary, BgpShowInstanceOrTableTest, SummaryReq);

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
