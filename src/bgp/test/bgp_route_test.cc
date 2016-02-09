/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_route.h"
#include "control-node/control_node.h"


using std::string;

class PeerMock : public IPeer {
public:
    PeerMock()
        : peer_type_(BgpProto::IBGP),
          address_(Ip4Address(0)) {
    }
    PeerMock(BgpProto::BgpPeerType peer_type, Ip4Address address)
        : peer_type_(peer_type),
          address_(address) {
    }

    virtual string ToString() const {
        return string("Peer_") + address_.to_string();
    }
    virtual string ToUVEKey() const {
        return string("Peer_") + address_.to_string();
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return peer_type_ == BgpProto::XMPP; }
    virtual void Close() { }
    virtual const string GetStateName() const { return "Established"; }
    BgpProto::BgpPeerType PeerType() const { return peer_type_; }
    virtual uint32_t bgp_identifier() const { return address_.to_ulong(); }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
    virtual void UpdatePrimaryPathCount(int count) const { }
    virtual int GetPrimaryPathCount() const { return 0; }

private:
    BgpProto::BgpPeerType peer_type_;
    Ip4Address address_;
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


    PeerMock peer;
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

//
// Paths with same router id are considered are equal.
//
TEST_F(BgpRouteTest, PathCompareRouterId1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
}

//
// Path with lower router id is better.
//
TEST_F(BgpRouteTest, PathCompareRouterId2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Paths with same originator id are considered are equal even if router ids
// are different.
//
TEST_F(BgpRouteTest, PathCompareOriginatorId1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.251", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    Ip4Address origid2 = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId origid_spec2(origid2.to_ulong());
    spec2.push_back(&origid_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.252", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
}

//
// Path with lower originator id is better.
// Router ids are the same.
//
TEST_F(BgpRouteTest, PathCompareOriginatorId2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    Ip4Address origid2 = Ip4Address::from_string("10.1.1.2", ec);
    BgpAttrOriginatorId origid_spec2(origid2.to_ulong());
    spec2.push_back(&origid_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Path with lower originator id is better.
// Router ids are different.
//
TEST_F(BgpRouteTest, PathCompareOriginatorId3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.252", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    Ip4Address origid2 = Ip4Address::from_string("10.1.1.2", ec);
    BgpAttrOriginatorId origid_spec2(origid2.to_ulong());
    spec2.push_back(&origid_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.251", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Path with lower originator id than other path's router id is better.
// One path has originator id and other path has no originator id.
//
TEST_F(BgpRouteTest, PathCompareOriginatorId4) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Path with shorter cluster list length is better.
// Both paths have a cluster list.
// Path with longer cluster list is learnt from peer with lower address.
//
TEST_F(BgpRouteTest, PathCompareClusterList1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.100", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    ClusterListSpec clist_spec1;
    clist_spec1.cluster_list.push_back(100);
    clist_spec1.cluster_list.push_back(200);
    spec1.push_back(&clist_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.251", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    Ip4Address origid2 = Ip4Address::from_string("10.1.1.100", ec);
    BgpAttrOriginatorId origid_spec2(origid2.to_ulong());
    spec2.push_back(&origid_spec2);
    ClusterListSpec clist_spec2;
    clist_spec2.cluster_list.push_back(100);
    spec2.push_back(&clist_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.252", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
}

//
// Path with shorter cluster list length is better.
// One path has a cluster list while other one has none.
// Path with longer cluster list is learnt from peer with lower address.
//
TEST_F(BgpRouteTest, PathCompareClusterList2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrLocalPref lpref_spec1(100);
    spec1.push_back(&lpref_spec1);
    Ip4Address origid1 = Ip4Address::from_string("10.1.1.100", ec);
    BgpAttrOriginatorId origid_spec1(origid1.to_ulong());
    spec1.push_back(&origid_spec1);
    ClusterListSpec clist_spec1;
    clist_spec1.cluster_list.push_back(100);
    spec1.push_back(&clist_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrLocalPref lpref_spec2(100);
    spec2.push_back(&lpref_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.100", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
}

//
// Path with lower med is better.
//
TEST_F(BgpRouteTest, PathCompareMed1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(64512);
    ps1->path_segment.push_back(64513);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(200);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64514);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Paths with different neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// Both paths have as paths, but the leftmost as is different.
//
TEST_F(BgpRouteTest, PathCompareMed2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(64514);
    ps1->path_segment.push_back(64513);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(200);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64513);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
}

//
// Paths with different neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// Both paths have nil as path.
//
TEST_F(BgpRouteTest, PathCompareMed3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(200);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
}

//
// Paths with 0 neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// First segment in both paths is an AS_SET.
//
TEST_F(BgpRouteTest, PathCompareMed4) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps1->path_segment.push_back(64512);
    ps1->path_segment.push_back(64513);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(200);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64513);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
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
