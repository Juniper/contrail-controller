/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_route.h"

#include "base/logging.h"
#include "base/task.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class RTargetPrefixTest : public ::testing::Test {
};

TEST_F(RTargetPrefixTest, Build) {
    boost::system::error_code ec;
    RouteTarget rtarget(RouteTarget::FromString("target:1:2"));
    RTargetPrefix prefix(100, rtarget);
    EXPECT_EQ(prefix.ToString(), "100:target:1:2");
    EXPECT_EQ(prefix.rtarget().ToString(), "target:1:2");
    EXPECT_EQ(prefix.as(), 100);
}

TEST_F(RTargetPrefixTest, Parse) {
    RTargetPrefix prefix(
        RTargetPrefix::FromString("64512:target:64512:1"));
    EXPECT_EQ(prefix.ToString(), "64512:target:64512:1");
    EXPECT_EQ(prefix.rtarget().ToString(), "target:64512:1");
    EXPECT_EQ(prefix.as(), 64512);
}

TEST_F(RTargetPrefixTest, ProtoPrefix) {
    RTargetPrefix prefix(
        RTargetPrefix::FromString("64512:target:64512:1"));

    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    RTargetPrefix prefix_1(proto_prefix);

    EXPECT_TRUE((prefix.CompareTo(prefix_1) == 0));
}


int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
