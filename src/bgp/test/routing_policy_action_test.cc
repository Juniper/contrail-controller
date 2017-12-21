/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-policy/routing_policy_action.h"
#include "control-node/control_node.h"

using boost::assign::list_of;
using std::find;
using std::string;
using std::vector;

class UpdateAsPathTest : public ::testing::Test {
protected:
    UpdateAsPathTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(UpdateAsPathTest, ToString) {
    vector<uint16_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());
    EXPECT_EQ("as-path expand [ 1000,2000 ]", action.ToString());
}

TEST_F(UpdateAsPathTest, IsEqual1) {
    vector<uint16_t> asn_list1 = list_of(1000)(2000);
    vector<uint16_t> asn_list2 = list_of(1000)(2000);
    UpdateAsPath action1(asn_list1);
    UpdateAsPath action2(asn_list2);
    EXPECT_TRUE(action1.IsEqual(action2));
    EXPECT_TRUE(action2.IsEqual(action1));
}

TEST_F(UpdateAsPathTest, IsEqual2) {
    vector<uint16_t> asn_list1 = list_of(1000)(2000);
    vector<uint16_t> asn_list2 = list_of(1000)(3000);
    UpdateAsPath action1(asn_list1);
    UpdateAsPath action2(asn_list2);
    EXPECT_FALSE(action1.IsEqual(action2));
    EXPECT_FALSE(action2.IsEqual(action1));
}

TEST_F(UpdateAsPathTest, UpdateNull) {
    vector<uint16_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());

    BgpAttr attr(attr_db_);
    action(&attr);
    const AsPath *as_path = attr.as_path();
    EXPECT_TRUE(as_path != NULL);
    const AsPathSpec &as_path_spec = as_path->path();
    EXPECT_EQ(1, as_path_spec.path_segments.size());
    EXPECT_EQ(2, as_path_spec.path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, as_path_spec.path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, as_path_spec.path_segments[0]->path_segment[1]);
}

TEST_F(UpdateAsPathTest, UpdateNonNull) {
    vector<uint16_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());

    BgpAttrSpec spec;
    AsPathSpec path;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment = list_of(3000)(4000);
    path.path_segments.push_back(ps);
    spec.push_back(&path);

    BgpAttr attr(attr_db_, spec);
    action(&attr);
    const AsPath *as_path = attr.as_path();
    EXPECT_TRUE(as_path != NULL);
    const AsPathSpec &as_path_spec = as_path->path();
    EXPECT_EQ(1, as_path_spec.path_segments.size());
    EXPECT_EQ(4, as_path_spec.path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, as_path_spec.path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, as_path_spec.path_segments[0]->path_segment[1]);
    EXPECT_EQ(3000, as_path_spec.path_segments[0]->path_segment[2]);
    EXPECT_EQ(4000, as_path_spec.path_segments[0]->path_segment[3]);
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
