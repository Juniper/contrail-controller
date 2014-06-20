/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"

#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_route.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"

#include "testing/gunit.h"

class BgpPeerMock : public IPeer {
public:
    virtual std::string ToString() const { return "test-peer"; }
    virtual std::string ToUVEKey() const { return "test-peer"; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() { }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const std::string GetStateName() const { return "UNKNOWN"; }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
private:
};

class RibOutAttributesTest : public ::testing::Test {
protected:
    RibOutAttributesTest()
        : server_(&evm_) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
};

namespace {

TEST_F(RibOutAttributesTest, Paths) {
    BgpPeerMock peer1;
    BgpPeerMock peer2;
    BgpPeerMock peer3;

    BgpAttr *attr1;
    BgpAttr *attr2;
    BgpAttr *attr3;

    BgpPath *path1;
    BgpPath *path2;
    BgpPath *path3;

    IpAddress nexthop1(Ip4Address(0x01010101));
    IpAddress nexthop2(Ip4Address(0x01010102));
    IpAddress nexthop3(Ip4Address(0x01010103));

    Ip4Prefix prefix;
    AsPathSpec as_path;
    BgpAttrSpec spec;

    InetRoute route(prefix);

    BgpAttrDB *db = server_.attr_db();
    attr1 = new BgpAttr(db, spec);

    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(30);
    as_path.path_segments.push_back(ps);
    attr1->set_as_path(&as_path);
    attr1->set_origin(BgpAttrOrigin::IGP);
    attr1->set_med(5);

    attr1->set_local_pref(10);
    attr1->set_nexthop(nexthop1);
    path1 = new BgpPath(&peer1, BgpPath::BGP_XMPP, attr1, 0, 1);
    route.InsertPath(path1);

    //
    // Route has only one path
    //
    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(1, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    }

    //
    // Add another ECMP eligible path (same local-pref and as count)
    //
    attr2 = new BgpAttr(*attr1);
    attr2->set_nexthop(nexthop2);
    path2 = new BgpPath(&peer2, BgpPath::BGP_XMPP, attr2, 0, 2);
    route.InsertPath(path2);

    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr2->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Add another path which is not ECMP eligible
    //
    attr3 = new BgpAttr(*attr1);
    attr3->set_local_pref(5);
    attr3->set_nexthop(nexthop3);
    path3 = new BgpPath(&peer3, BgpPath::BGP_XMPP, attr3, 0, 3);
    route.InsertPath(path3);

    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr2->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Remove ecmp path
    //
    route.RemovePath(&peer2);
    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(1, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    }

    //
    // Add ecmp path again
    //
    attr2 = new BgpAttr(*attr1);
    attr2->set_nexthop(nexthop2);
    path2 = new BgpPath(&peer2, BgpPath::BGP_XMPP, attr2, 0, 2);
    route.InsertPath(path2);

    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr2->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Remove non ecmp path
    //
    route.RemovePath(&peer3);
    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr2->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Add another ECMP path with duplicate next-hop/label combination
    //
    attr3 = new BgpAttr(*attr1);
    attr3->set_nexthop(nexthop2);
    path3 = new BgpPath(&peer3, BgpPath::BGP_XMPP, attr3, 0, 2);
    route.InsertPath(path3);

    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr2->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Remove duplicate ecmp eligible path, att1 and attr3 are left
    //
    route.RemovePath(&peer2);
    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(2, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr1->nexthop(), ribout_attr.nexthop_list().at(0).address());
    EXPECT_EQ(attr3->nexthop(), ribout_attr.nexthop_list().at(1).address());
    }

    //
    // Remove the best path!
    //
    route.RemovePath(&peer1);
    {
    RibOutAttr ribout_attr(&route, route.BestPath()->GetAttr(), true);
    EXPECT_EQ(1, ribout_attr.nexthop_list().size());
    EXPECT_EQ(attr3->nexthop(), ribout_attr.nexthop_list().at(0).address());
    }

    //
    // Remove all 4 paths that have been added so far
    //
    (void) route.RemovePath(&peer1);
    (void) route.RemovePath(&peer2);
    (void) route.RemovePath(&peer3);
}

}  // namespace

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
