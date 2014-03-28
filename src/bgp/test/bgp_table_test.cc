/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_ribout.h"
#include "bgp/scheduling_group.h"
#include "db/db.h"
#include "testing/gunit.h"

using namespace std;

class BgpTableTest : public ::testing::Test {
protected:
    BgpTableTest()
        : rt_table_(static_cast<InetTable *>(db_.CreateTable("inet.0"))) {
    }

    DB db_;
    InetTable *rt_table_;
    SchedulingGroupManager mgr_;
};

// Basic tests for RibOut find/locate/delete.
TEST_F(BgpTableTest, RiboutBasic) {
    RibOut *ribout1 = NULL, *ribout2 = NULL, *ribout3 = NULL, *temp = NULL;
    RibExportPolicy policy1(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);
    RibExportPolicy policy2(BgpProto::IBGP, RibExportPolicy::BGP, 2, 0);
    RibExportPolicy policy3(BgpProto::IBGP, RibExportPolicy::BGP, 3, 0);

    // Create 3 ribouts.
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete ribout2 and make sure it's gone.
    rt_table_->RibOutDelete(policy2);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 2);

    // Check if we can find the others.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Add ribout2 again and make sure we can find it.
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Call locate again and make sure we didn't add a new one.
    temp = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_EQ(temp, ribout2);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can still find the others.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);

    // Create them again but in a different order.
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Call locate again and make sure we didn't add a new one.
    temp = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_EQ(temp, ribout3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);
}

// Different encodings result in creation of different RibOuts.
TEST_F(BgpTableTest, RiboutEncoding1) {
    RibOut *ribout1 = NULL, *ribout2 = NULL, *ribout3 = NULL, *temp = NULL;
    RibExportPolicy policy1(BgpProto::IBGP, RibExportPolicy::BGP, -1, 0);
    RibExportPolicy policy2(BgpProto::EBGP, RibExportPolicy::BGP, -1, 0);
    RibExportPolicy policy3(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0);

    // Create 3 ribouts.
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);
}

// Different encodings result in creation of different RibOuts.
TEST_F(BgpTableTest, RiboutEncoding2) {
    RibOut *ribout1 = NULL, *ribout2 = NULL, *ribout3 = NULL, *temp = NULL;
    RibExportPolicy policy1(BgpProto::IBGP, RibExportPolicy::BGP, -1, 0);
    RibExportPolicy policy2(BgpProto::EBGP, RibExportPolicy::BGP, -1, 0);
    RibExportPolicy policy3(BgpProto::XMPP, RibExportPolicy::XMPP, -1, 0);

    // Create 3 ribouts.
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);
}

// Different cluster ids result in creation of different RibOuts.
TEST_F(BgpTableTest, RiboutClusterId) {
    RibOut *ribout1 = NULL, *ribout2 = NULL, *ribout3 = NULL, *temp = NULL;
    RibExportPolicy policy1(BgpProto::EBGP, RibExportPolicy::BGP, -1, 1);
    RibExportPolicy policy2(BgpProto::EBGP, RibExportPolicy::BGP, -1, 2);
    RibExportPolicy policy3(BgpProto::EBGP, RibExportPolicy::BGP, -1, 3);

    // Create 3 ribouts.
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);
}

// Different AS numbers result in creation of different RibOuts.
TEST_F(BgpTableTest, RiboutAS) {
    RibOut *ribout1 = NULL, *ribout2 = NULL, *ribout3 = NULL, *temp = NULL;
    RibExportPolicy policy1(BgpProto::EBGP, RibExportPolicy::BGP, 101, -1, 0);
    RibExportPolicy policy2(BgpProto::EBGP, RibExportPolicy::BGP, 102, -1, 0);
    RibExportPolicy policy3(BgpProto::EBGP, RibExportPolicy::BGP, 103, -1, 0);

    // Create 3 ribouts.
    ribout1 = rt_table_->RibOutLocate(&mgr_, policy1);
    ASSERT_TRUE(ribout1 != NULL);
    ribout2 = rt_table_->RibOutLocate(&mgr_, policy2);
    ASSERT_TRUE(ribout2 != NULL);
    ribout3 = rt_table_->RibOutLocate(&mgr_, policy3);
    ASSERT_TRUE(ribout3 != NULL);
    ASSERT_EQ(rt_table_->ribout_map().size(), 3);

    // Check if we can find them.
    temp = rt_table_->RibOutFind(policy1);
    ASSERT_EQ(temp, ribout1);
    temp = rt_table_->RibOutFind(policy2);
    ASSERT_EQ(temp, ribout2);
    temp = rt_table_->RibOutFind(policy3);
    ASSERT_EQ(temp, ribout3);

    // Delete all of them.
    rt_table_->RibOutDelete(policy1);
    rt_table_->RibOutDelete(policy2);
    rt_table_->RibOutDelete(policy3);
    ASSERT_EQ(rt_table_->ribout_map().size(), 0);

    // Make sure they are all gone.
    ribout1 = rt_table_->RibOutFind(policy1);
    ASSERT_TRUE(ribout1 == NULL);
    ribout2 = rt_table_->RibOutFind(policy2);
    ASSERT_TRUE(ribout2 == NULL);
    ribout3 = rt_table_->RibOutFind(policy3);
    ASSERT_TRUE(ribout3 == NULL);
}

static void SetUp() {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
