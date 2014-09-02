/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "testing/gunit.h"

using namespace std;

class EvpnRouteTest : public ::testing::Test {
protected:
    EvpnRouteTest() : server_(&evm_) {
        attr_db_ = server_.attr_db();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

class EvpnAutoDiscoveryRouteTest : public EvpnRouteTest {
};

TEST_F(EvpnAutoDiscoveryRouteTest, ToString) {
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp + integerToString(tag);
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
        EXPECT_EQ(prefix_str, route.ToString());
    }
}

TEST_F(EvpnAutoDiscoveryRouteTest, SetKey) {
    EvpnPrefix null_prefix;
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnRoute route(null_prefix);
        string prefix_str = temp + integerToString(tag);
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        boost::scoped_ptr<EvpnTable::RequestKey> key(
            new EvpnTable::RequestKey(prefix, NULL));
        route.SetKey(key.get());
        EXPECT_EQ(prefix, key->prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
    }
}

TEST_F(EvpnAutoDiscoveryRouteTest, GetDBRequestKey) {
    string temp("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp + integerToString(tag);
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
        const EvpnTable::RequestKey *key =
            static_cast<EvpnTable::RequestKey *>(keyptr.get());
        EXPECT_EQ(prefix, key->prefix);
    }
}

class EvpnMacAdvertisementRouteTest : public EvpnRouteTest {
};

TEST_F(EvpnMacAdvertisementRouteTest, ToString) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
        EXPECT_EQ(prefix_str, route.ToString());
    }
}

TEST_F(EvpnMacAdvertisementRouteTest, SetKey) {
    EvpnPrefix null_prefix;
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnRoute route(null_prefix);
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        boost::scoped_ptr<EvpnTable::RequestKey> key(
            new EvpnTable::RequestKey(prefix, NULL));
        route.SetKey(key.get());
        EXPECT_EQ(prefix, key->prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
    }
}

TEST_F(EvpnMacAdvertisementRouteTest, GetDBRequestKey) {
    string temp1("2-10.1.1.1:65535-");
    string temp2("-11:12:13:14:15:16,192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
        const EvpnTable::RequestKey *key =
            static_cast<EvpnTable::RequestKey *>(keyptr.get());
        EXPECT_EQ(prefix, key->prefix);
    }
}

class EvpnInclusiveMulticastRouteTest : public EvpnRouteTest {
};

TEST_F(EvpnInclusiveMulticastRouteTest, ToString) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
        EXPECT_EQ(prefix_str, route.ToString());
    }
}

TEST_F(EvpnInclusiveMulticastRouteTest, SetKey) {
    EvpnPrefix null_prefix;
    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        EvpnRoute route(null_prefix);
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        boost::scoped_ptr<EvpnTable::RequestKey> key(
            new EvpnTable::RequestKey(prefix, NULL));
        route.SetKey(key.get());
        EXPECT_EQ(prefix, key->prefix);
        EXPECT_EQ(prefix, route.GetPrefix());
    }
}

TEST_F(EvpnInclusiveMulticastRouteTest, GetDBRequestKey) {
    string temp1("3-10.1.1.1:65535-");
    string temp2("-192.1.1.1");
    uint32_t tag_list[] = { 0, 100, 128, 4094, 65536, 4294967295 };
    BOOST_FOREACH(uint32_t tag, tag_list) {
        string prefix_str = temp1 + integerToString(tag) + temp2;
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnRoute route(prefix);
        DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
        const EvpnTable::RequestKey *key =
            static_cast<EvpnTable::RequestKey *>(keyptr.get());
        EXPECT_EQ(prefix, key->prefix);
    }
}

class EvpnSegmentRouteTest : public EvpnRouteTest {
};

TEST_F(EvpnSegmentRouteTest, ToString) {
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
    EvpnRoute route(prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
    EXPECT_EQ(prefix_str, route.ToString());
}

TEST_F(EvpnSegmentRouteTest, SetKey) {
    EvpnPrefix null_prefix;
    EvpnRoute route(null_prefix);
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
    boost::scoped_ptr<EvpnTable::RequestKey> key(
        new EvpnTable::RequestKey(prefix, NULL));
    route.SetKey(key.get());
    EXPECT_EQ(prefix, key->prefix);
    EXPECT_EQ(prefix, route.GetPrefix());
}

TEST_F(EvpnSegmentRouteTest, GetDBRequestKey) {
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
    EvpnRoute route(prefix);
    DBEntryBase::KeyPtr keyptr = route.GetDBRequestKey();
    const EvpnTable::RequestKey *key =
        static_cast<EvpnTable::RequestKey *>(keyptr.get());
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
