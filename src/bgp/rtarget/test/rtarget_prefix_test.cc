/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include "bgp/bgp_log.h"
#include "bgp/rtarget/rtarget_table.h"
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
        RTargetPrefix::FromString("80000:target:80000:1"));
    EXPECT_EQ(prefix.ToString(), "80000:target:80000:1");
    EXPECT_EQ(prefix.rtarget().ToString(), "target:80000:1");
    EXPECT_EQ(prefix.as(), 80000U);
}

TEST_F(RTargetPrefixTest, Default) {
    RTargetPrefix prefix;
    EXPECT_EQ(prefix.as(), 0U);
    EXPECT_EQ(prefix.rtarget(), RouteTarget::null_rtarget);
    EXPECT_EQ(prefix.ToString(), RTargetPrefix::kDefaultPrefixString);
    EXPECT_EQ(prefix,
        RTargetPrefix::FromString(RTargetPrefix::kDefaultPrefixString));
}

// No ":" to delineate the AS number.
TEST_F(RTargetPrefixTest, Error1) {
    boost::system::error_code ec;
    string prefix_str("80000-target-80000-2");
    RTargetPrefix prefix = RTargetPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(0, prefix.as());
    EXPECT_EQ(RouteTarget::null_rtarget, prefix.rtarget());
}

// Invalid route target.
TEST_F(RTargetPrefixTest, Error2) {
    boost::system::error_code ec;
    string prefix_str("80000:target:65536:4294967295");
    RTargetPrefix prefix = RTargetPrefix::FromString(prefix_str, &ec);
    EXPECT_NE(0, ec.value());
    EXPECT_EQ(0, prefix.as());
    EXPECT_EQ(RouteTarget::null_rtarget, prefix.rtarget());
}

TEST_F(RTargetPrefixTest, FromProtoPrefix1) {
    RTargetPrefix prefix(RTargetPrefix::FromString("80000:target:80000:1"));
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    EXPECT_EQ(96, proto_prefix.prefixlen);
    RTargetPrefix prefix_1;
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_EQ(0, result);
    EXPECT_TRUE((prefix.CompareTo(prefix_1) == 0));
}

TEST_F(RTargetPrefixTest, FromProtoPrefix2) {
    RTargetPrefix prefix;
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    EXPECT_EQ(0, proto_prefix.prefixlen);
    RTargetPrefix prefix_1;
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_EQ(0, result);
    EXPECT_TRUE((prefix.CompareTo(prefix_1) == 0));
}

TEST_F(RTargetPrefixTest, FromProtoPrefixError1) {
    RTargetPrefix prefix(RTargetPrefix::FromString("80000:target:80000:1"));
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    RTargetPrefix prefix_1;
    proto_prefix.prefix.resize(4);
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_NE(0, result);
}

TEST_F(RTargetPrefixTest, FromProtoPrefixError2) {
    RTargetPrefix prefix(RTargetPrefix::FromString("80000:target:80000:1"));
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    RTargetPrefix prefix_1;
    proto_prefix.prefix.resize(8);
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_NE(0, result);
}

TEST_F(RTargetPrefixTest, FromProtoPrefixError3) {
    RTargetPrefix prefix(RTargetPrefix::FromString("80000:target:80000:1"));
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    RTargetPrefix prefix_1;
    proto_prefix.prefix.resize(10);
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_NE(0, result);
}

TEST_F(RTargetPrefixTest, FromProtoPrefixError4) {
    RTargetPrefix prefix(RTargetPrefix::FromString("80000:target:80000:1"));
    BgpProtoPrefix proto_prefix;
    prefix.BuildProtoPrefix(&proto_prefix);
    RTargetPrefix prefix_1;
    proto_prefix.prefix.resize(13);
    int result = RTargetPrefix::FromProtoPrefix(proto_prefix, &prefix_1);
    EXPECT_NE(0, result);
}

TEST_F(RTargetPrefixTest, CompareTo1) {
    string prefix_str1("2:target:4294967295:8000");
    string prefix_str2("256:target:4294967295:8000");
    RTargetPrefix prefix1 = RTargetPrefix::FromString(prefix_str1);
    RTargetPrefix prefix2 = RTargetPrefix::FromString(prefix_str2);
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

TEST_F(RTargetPrefixTest, CompareTo2) {
    string prefix_str1("80000:target:4294967295:2");
    string prefix_str2("80000:target:4294967295:256");
    RTargetPrefix prefix1 = RTargetPrefix::FromString(prefix_str1);
    RTargetPrefix prefix2 = RTargetPrefix::FromString(prefix_str2);
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

TEST_F(RTargetPrefixTest, CompareTo3) {
    string prefix_str1("80000:target:80000:2");
    string prefix_str2("80000:target:80000:65526");
    RTargetPrefix prefix1 = RTargetPrefix::FromString(prefix_str1);
    RTargetPrefix prefix2 = RTargetPrefix::FromString(prefix_str2);
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

TEST_F(RTargetPrefixTest, CompareTo4) {
    string prefix_str1("80000:target:10.1.1.1:65535");
    string prefix_str2("80000:target:10.1.1.2:65535");
    RTargetPrefix prefix1 = RTargetPrefix::FromString(prefix_str1);
    RTargetPrefix prefix2 = RTargetPrefix::FromString(prefix_str2);
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

TEST_F(RTargetPrefixTest, CompareTo5) {
    string prefix_str1("80000:target:10.1.1.1:2");
    string prefix_str2("80000:target:10.1.1.1:256");
    RTargetPrefix prefix1 = RTargetPrefix::FromString(prefix_str1);
    RTargetPrefix prefix2 = RTargetPrefix::FromString(prefix_str2);
    EXPECT_GT(0, prefix1.CompareTo(prefix2));
    EXPECT_LT(0, prefix2.CompareTo(prefix1));
}

TEST_F(RTargetPrefixTest, SetKey1) {
    RTargetPrefix null_prefix;
    RTargetRoute route(null_prefix);
    string prefix_str("80000:target:80000:4294967295");
    RTargetPrefix prefix(RTargetPrefix::FromString(prefix_str));
    boost::scoped_ptr<RTargetTable::RequestKey> key(
        new RTargetTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(RTargetPrefixTest, SetKey2) {
    RTargetPrefix null_prefix;
    RTargetRoute route(null_prefix);
    string prefix_str("80000:target:10.1.1.1:65535");
    RTargetPrefix prefix(RTargetPrefix::FromString(prefix_str));
    boost::scoped_ptr<RTargetTable::RequestKey> key(
        new RTargetTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(RTargetPrefixTest, GetDBRequestKey1) {
    string prefix_str("80000:target:80000:4294967295");
    RTargetPrefix prefix(RTargetPrefix::FromString(prefix_str));
    RTargetRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const RTargetTable::RequestKey *key =
        static_cast<RTargetTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}

TEST_F(RTargetPrefixTest, GetDBRequestKey2) {
    string prefix_str("80000:target:10.1.1.1:65535");
    RTargetPrefix prefix(RTargetPrefix::FromString(prefix_str));
    RTargetRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const RTargetTable::RequestKey *key =
        static_cast<RTargetTable::RequestKey *>(keyptr.get());
    EXPECT_EQ(prefix, key->prefix);
}


int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
