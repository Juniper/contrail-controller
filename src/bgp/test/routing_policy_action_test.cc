/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/routing-policy/routing_policy_action.h"
#include "bgp/rtarget/rtarget_address.h"
#include "control-node/control_node.h"

using boost::assign::list_of;
using std::find;
using std::string;
using std::vector;

class UpdateExtCommunityTest : public ::testing::Test {
protected:
    UpdateExtCommunityTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

TEST_F(UpdateExtCommunityTest, Update) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    UpdateExtCommunity action(communities, "add");

    ExtCommunitySpec comm_spec;
    comm_spec.communities.clear();
    BgpAttrSpec spec;
    spec.push_back(&comm_spec);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    action(const_cast<BgpAttr *>(attr.get()));
    const ExtCommunity *comm = attr->ext_community();
    EXPECT_TRUE(comm!= NULL);
    EXPECT_TRUE(comm->communities().size() == 2);
    RouteTarget val0 = RouteTarget(comm->communities()[0]);
    RouteTarget val1 = RouteTarget(comm->communities()[1]);
    EXPECT_TRUE(val0.ToString() == communities[0]);
    EXPECT_TRUE(val1.ToString() == communities[1]);

    communities = list_of("target:33:11")("target:53:11");
    UpdateExtCommunity action2(communities, "set");
    action2(const_cast<BgpAttr *>(attr.get()));
    comm = attr->ext_community();
    EXPECT_TRUE(comm!= NULL);
    EXPECT_TRUE(comm->communities().size() == 2);
    val0 = RouteTarget(comm->communities()[0]);
    val1 = RouteTarget(comm->communities()[1]);
    EXPECT_TRUE(val0.ToString() == communities[0]);
    EXPECT_TRUE(val1.ToString() == communities[1]);

    vector<string> communities2 = list_of("target:53:11");
    UpdateExtCommunity action3(communities2, "remove");
    action3(const_cast<BgpAttr *>(attr.get()));
    comm = attr->ext_community();
    EXPECT_TRUE(comm!= NULL);
    EXPECT_TRUE(comm->communities().size() == 1);
    val0 = RouteTarget(comm->communities()[0]);
    EXPECT_TRUE(val0.ToString() == communities[0]);
}

TEST_F(UpdateExtCommunityTest, ToString) {
    vector<string> communities = list_of("target:23:11")("target:43:11");
    UpdateExtCommunity action(communities, "add");
    EXPECT_EQ("Extcommunity add [ target:23:11,target:43:11 ]",
              action.ToString());
}

TEST_F(UpdateExtCommunityTest, IsEqual1) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:23:11")("target:43:11");
    UpdateExtCommunity action1(communities1, "add");
    UpdateExtCommunity action2(communities2, "add");
    EXPECT_TRUE(action1.IsEqual(action2));
    EXPECT_TRUE(action2.IsEqual(action1));
}

TEST_F(UpdateExtCommunityTest, IsEqual2) {
    vector<string> communities1 = list_of("target:23:11")("target:43:11");
    vector<string> communities2 = list_of("target:23:11")("target:53:11");
    UpdateExtCommunity action1(communities1, "add");
    UpdateExtCommunity action2(communities2, "add");
    EXPECT_FALSE(action1.IsEqual(action2));
    EXPECT_FALSE(action2.IsEqual(action1));
}

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
    vector<uint32_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());
    EXPECT_EQ("as-path expand [ 1000,2000 ]", action.ToString());
}

TEST_F(UpdateAsPathTest, IsEqual1) {
    vector<uint32_t> asn_list1 = list_of(1000)(2000);
    vector<uint32_t> asn_list2 = list_of(1000)(2000);
    UpdateAsPath action1(asn_list1);
    UpdateAsPath action2(asn_list2);
    EXPECT_TRUE(action1.IsEqual(action2));
    EXPECT_TRUE(action2.IsEqual(action1));
}

TEST_F(UpdateAsPathTest, IsEqual2) {
    vector<uint32_t> asn_list1 = list_of(1000)(2000);
    vector<uint32_t> asn_list2 = list_of(1000)(3000);
    UpdateAsPath action1(asn_list1);
    UpdateAsPath action2(asn_list2);
    EXPECT_FALSE(action1.IsEqual(action2));
    EXPECT_FALSE(action2.IsEqual(action1));
}

TEST_F(UpdateAsPathTest, UpdateNull) {
    vector<uint32_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());

    BgpAttr attr(attr_db_);
    action(&attr);
    const AsPath4Byte *as_path = attr.aspath_4byte();
    EXPECT_TRUE(as_path != NULL);
    const AsPath4ByteSpec &as_path_spec = as_path->path();
    EXPECT_EQ(1, as_path_spec.path_segments.size());
    EXPECT_EQ(2, as_path_spec.path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, as_path_spec.path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, as_path_spec.path_segments[0]->path_segment[1]);
}

TEST_F(UpdateAsPathTest, UpdateNonNull) {
    vector<uint32_t> asn_list = list_of(1000)(2000);
    UpdateAsPath action(asn_list);
    EXPECT_EQ(asn_list, action.asn_list());

    BgpAttrSpec spec;
    AsPath4ByteSpec path;
    AsPath4ByteSpec::PathSegment *ps = new AsPath4ByteSpec::PathSegment;
    ps->path_segment_type = AsPath4ByteSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment = list_of(3000)(4000);
    path.path_segments.push_back(ps);
    spec.push_back(&path);

    BgpAttr attr(attr_db_, spec);
    action(&attr);
    const AsPath4Byte *as_path = attr.aspath_4byte();
    EXPECT_TRUE(as_path != NULL);
    const AsPath4ByteSpec &as_path_spec = as_path->path();
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
