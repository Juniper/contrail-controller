/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_route.h"

#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_route.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"

#include "testing/gunit.h"

class BgpPeerMock : public IPeer {
public:
    virtual std::string ToString() const {
        return "test-peer";
    }
    virtual std::string ToUVEKey() const {
        return "test-peer";
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }
    virtual BgpServer *server() {
        return NULL;
    }
    virtual IPeerClose *peer_close() {
        return NULL;
    }
    virtual IPeerDebugStats *peer_stats() {
        return NULL;
    }
    virtual bool IsReady() const {
        return true;
    }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() {
    }
    virtual const std::string GetStateName() const {
        return "UNKNOWN";
    }

    BgpProto::BgpPeerType PeerType() const {
        return BgpProto::IBGP;
    }
    virtual uint32_t bgp_identifier() const {
        return 0;
    }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
private:
};

class BgpRouteTest : public ::testing::Test {
protected:
    BgpRouteTest()
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

TEST_F(BgpRouteTest, Paths) {
    BgpAttrSpec spec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttr *attr = new BgpAttr(db, spec);

    attr->set_origin(BgpAttrOrigin::IGP);
    attr->set_med(5);
    attr->set_local_pref(10);

    AsPathSpec as_path;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(30);
    as_path.path_segments.push_back(ps);

    attr->set_as_path(&as_path);


    BgpPeerMock peer;
    BgpPath *path = new BgpPath(&peer, BgpPath::BGP_XMPP, attr, 0, 0);

    Ip4Prefix prefix;
    InetRoute route(prefix);
    route.InsertPath(path);

    BgpAttr *attr2 = new BgpAttr(*attr);
    attr2->set_med(4);
    BgpPath *path2 = new BgpPath(&peer, BgpPath::BGP_XMPP, attr2, 0, 0);
    route.InsertPath(path2);

    EXPECT_EQ(path2, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    BgpAttr *attr3 = new BgpAttr(*attr);
    attr3->set_local_pref(20);
    BgpPath *path3 = new BgpPath(&peer, BgpPath::BGP_XMPP, attr3, 0, 0);
    route.InsertPath(path3);

    EXPECT_EQ(path3, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    BgpAttr *attr4 = new BgpAttr(*attr);
    attr4->set_origin(BgpAttrOrigin::EGP);
    BgpPath *path4 = new BgpPath(&peer, BgpPath::BGP_XMPP, attr4, 0, 0);
    route.InsertPath(path4);

    EXPECT_EQ(path3, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    // Remove all 4 paths that have been added so far
    route.RemovePath(&peer);
    route.RemovePath(&peer);
    route.RemovePath(&peer);
    route.RemovePath(&peer);
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
