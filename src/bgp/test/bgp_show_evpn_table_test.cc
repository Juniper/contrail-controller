/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_show_instance_or_table_test.h"

typedef TypeDefinition<
    ShowEvpnTableReq,
    ShowEvpnTableReqIterate,
    ShowEvpnTableResp> RegularReq;

typedef TypeDefinition<
    ShowEvpnTableSummaryReq,
    ShowEvpnTableSummaryReqIterate,
    ShowEvpnTableSummaryResp> SummaryReq;

// Common routine for regular and summary requests.
static void AddEvpnTableName(vector<string> *names, const string &name) {
    string table_name;
    if (name == BgpConfigManager::kMasterInstance) {
        table_name = "bgp.evpn.0";
    } else {
        table_name = name + ".evpn.0";
    }
    names->push_back(table_name);
}

// Specialization of AddInstanceOrTableName for regular request.
template<>
void BgpShowInstanceOrTableTest<RegularReq>::AddInstanceOrTableName(
    vector<string> *names, const string &name) {
    AddEvpnTableName(names, name);
}

// Specialization of AddInstanceOrTableName for summary request.
template<>
void BgpShowInstanceOrTableTest<SummaryReq>::AddInstanceOrTableName(
    vector<string> *names, const string &name) {
    AddEvpnTableName(names, name);
}

// Common routine for regular and summary requests.
template <typename RespT>
static void ValidateEvpnResponse(Sandesh *sandesh, vector<string> &result,
    const string &next_batch) {
    RespT *resp = dynamic_cast<RespT *>(sandesh);
    TASK_UTIL_EXPECT_TRUE(resp != NULL);
    TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());
    TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
    for (size_t i = 0; i < resp->get_tables().size(); ++i) {
        TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].get_name());
        cout << resp->get_tables()[i].log() << endl;
    }
}

// Specialization of ValidateResponse for regular request.
template<>
void BgpShowInstanceOrTableTest<RegularReq>::ValidateResponse(
    Sandesh *sandesh, vector<string> &result, const string &next_batch) {
    ValidateEvpnResponse<RegularReq::RespT>(sandesh, result, next_batch);
    validate_done_ = true;
}

// Specialization of ValidateResponse for summary request.
template<>
void BgpShowInstanceOrTableTest<SummaryReq>::ValidateResponse(
    Sandesh *sandesh, vector<string> &result, const string &next_batch) {
    ValidateEvpnResponse<SummaryReq::RespT>(sandesh, result, next_batch);
    validate_done_ = true;
}

// Instantiate all test patterns for ShowEvpnTableReq.
INSTANTIATE_TYPED_TEST_CASE_P(Regular, BgpShowInstanceOrTableTest, RegularReq);

// Instantiate all test patterns for ShowEvpnTableSummaryReq.
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
