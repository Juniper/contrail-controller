/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/extended-community/etree.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "control-node/control_node.h"
#include "net/community_type.h"


using std::string;

class PeerMock : public IPeer {
public:
    PeerMock()
        : peer_type_(BgpProto::IBGP),
          address_(Ip4Address(0)) {
    }
    PeerMock(BgpProto::BgpPeerType peer_type, Ip4Address address)
        : peer_type_(peer_type),
          address_(address),
          address_str_("Peer_" + address.to_string()) {
    }

    virtual const string &ToString() const { return address_str_; }
    virtual const string &ToUVEKey() const { return address_str_; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual BgpServer *server() const { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return peer_type_ == BgpProto::XMPP; }
    virtual void Close(bool graceful) { }
    virtual const string GetStateName() const { return "Established"; }
    BgpProto::BgpPeerType PeerType() const { return peer_type_; }
    virtual uint32_t bgp_identifier() const { return address_.to_ulong(); }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return false; }
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual void ProcessPathTunnelEncapsulation(const BgpPath *path,
        BgpAttr *attr, ExtCommunityDB *extcomm_db, const BgpTable *table)
        const {
    }
    virtual const std::vector<std::string> GetDefaultTunnelEncap(
        Address::Family family) const {
        return std::vector<std::string>();
    }
    virtual bool IsRegistrationRequired() const { return true; }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    BgpProto::BgpPeerType peer_type_;
    Ip4Address address_;
    std::string address_str_;
};

class BgpRouteTest : public ::testing::Test {
protected:
    BgpRouteTest()
        : server_(&evm_), table_(&db_, "test.inet.0") {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    InetTable table_;
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
    BgpPath *path = new BgpPath(&peer, BgpPath::BGP_XMPP,
                                server_.attr_db()->Locate(attr), 0, 0);

    Ip4Prefix prefix;
    InetRoute route(prefix);
    route.InsertPath(path);

    BgpAttr *attr2 = new BgpAttr(*attr);
    attr2->set_med(4);
    BgpPath *path2 = new BgpPath(&peer, BgpPath::BGP_XMPP,
                                 server_.attr_db()->Locate(attr2), 0, 0);
    route.InsertPath(path2);

    EXPECT_EQ(path2, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    BgpAttr *attr3 = new BgpAttr(*attr);
    attr3->set_local_pref(20);
    BgpPath *path3 = new BgpPath(&peer, BgpPath::BGP_XMPP,
                                 server_.attr_db()->Locate(attr3), 0, 0);
    route.InsertPath(path3);

    EXPECT_EQ(path3, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    BgpAttr *attr4 = new BgpAttr(*attr);
    attr4->set_origin(BgpAttrOrigin::EGP);
    BgpPath *path4 = new BgpPath(&peer, BgpPath::BGP_XMPP,
                                 server_.attr_db()->Locate(attr4), 0, 0);
    route.InsertPath(path4);

    EXPECT_EQ(path3, route.FindPath(BgpPath::BGP_XMPP, &peer, 0));

    // Remove all 4 paths that have been added so far
    route.RemovePath(&peer);
    route.RemovePath(&peer);
    route.RemovePath(&peer);
    route.RemovePath(&peer);
}

//
// Path with shorter AS Path is better - even when building ECMP nexthops.
//
TEST_F(BgpRouteTest, PathCompareAsPathLength1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(64512);
    ps1->path_segment.push_back(64512);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(100);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(-1, path1.PathCompare(path2, true));
    EXPECT_EQ(1, path2.PathCompare(path1, true));
}

//
// AS Path length is ignored for service chain paths - only when building ECMP
// nexthops.
//
TEST_F(BgpRouteTest, PathCompareAsPathLength2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(64512);
    ps1->path_segment.push_back(64512);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    OriginVnPathSpec ovn_spec1;
    OriginVn origin_vn1(64512, 999);
    ovn_spec1.origin_vns.push_back(origin_vn1.GetExtCommunityValue());
    spec1.push_back(&ovn_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(100);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    OriginVnPathSpec ovn_spec2;
    OriginVn origin_vn2(64512, 999);
    ovn_spec2.origin_vns.push_back(origin_vn2.GetExtCommunityValue());
    spec2.push_back(&ovn_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// AS Path length is not ignored when comparing a service chain path and a
// regular path - even when building ECMP nexthops.
//
TEST_F(BgpRouteTest, PathCompareAsPathLength3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(64512);
    ps1->path_segment.push_back(64512);
    aspath_spec1.path_segments.push_back(ps1);
    spec1.push_back(&aspath_spec1);
    OriginVnPathSpec ovn_spec1;
    OriginVn origin_vn1(64512, 999);
    ovn_spec1.origin_vns.push_back(origin_vn1.GetExtCommunityValue());
    spec1.push_back(&ovn_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(100);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    aspath_spec2.path_segments.push_back(ps2);
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(-1, path1.PathCompare(path2, true));
    EXPECT_EQ(1, path2.PathCompare(path1, true));
}

//
// EBGP paths are preferred over IBGP paths.
//
TEST_F(BgpRouteTest, PathCompareEIBGP) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::EBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Lower path-id should be preferred if peer-type is same.
//
TEST_F(BgpRouteTest, PathComparePathID1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec;
    BgpAttrPtr attr = db->Locate(spec);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, 1, BgpPath::BGP_XMPP, attr, 0, 0, 0);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, 2, BgpPath::BGP_XMPP, attr, 0, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// EBGP path shoud be preferred even if path-id is lower.
//
TEST_F(BgpRouteTest, PathComparePathID2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec;
    BgpAttrPtr attr = db->Locate(spec);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, 1, BgpPath::BGP_XMPP, attr, 0, 0, 0);

    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, 2, BgpPath::BGP_XMPP, attr, 0, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// EBGP path shoud be preferred even if path-id is lower.
//
TEST_F(BgpRouteTest, PathComparePathID3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec;
    BgpAttrPtr attr = db->Locate(spec);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, 2, BgpPath::BGP_XMPP, attr, 0, 0, 0);

    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, 1, BgpPath::BGP_XMPP, attr, 0, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
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
// Paths with different MEDs are not considered ECMP.
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
    EXPECT_EQ(-1, path1.PathCompare(path2, true));
    EXPECT_EQ(1, path2.PathCompare(path1, true));
}

//
// Paths with different neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// Both paths have as paths, but the leftmost as is different.
// Paths with different MEDs are considered ECMP as leftmost AS is different.
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
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// Paths with different neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// Both paths have nil as path.
// Paths with different MEDs are considered ECMP as leftmost AS is 0.
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
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// Paths with 0 neighbor as are not compared w.r.t med.
// Path with higher med happens to win since it has lower router id.
// First segment in both paths is an AS_SET.
// Paths with different MEDs are considered ECMP as leftmost AS is 0.
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
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// Paths with empty as path are compared w.r.t med if always compare med knob
// is set.
// Path with lower med is better.
// Both paths have empty as paths.
// Paths with different MEDs are not considered ECMP.
//
TEST_F(BgpRouteTest, PathCompareMed5) {
    boost::system::error_code ec;
    server_.global_config()->set_always_compare_med(true);
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrMultiExitDisc med_spec1(100);
    spec1.push_back(&med_spec1);
    AsPathSpec aspath_spec1;
    spec1.push_back(&aspath_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    BgpAttrMultiExitDisc med_spec2(200);
    spec2.push_back(&med_spec2);
    AsPathSpec aspath_spec2;
    spec2.push_back(&aspath_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(-1, path1.PathCompare(path2, true));
    EXPECT_EQ(1, path2.PathCompare(path1, true));
}

//
// Paths with different neighbor as are compared w.r.t med if always compare
// med knob is set.
// Path with lower med is better.
// Both paths have as paths, but the leftmost as is different.
// Paths with different MEDs are not considered ECMP.
//
TEST_F(BgpRouteTest, PathCompareMed6) {
    boost::system::error_code ec;
    server_.global_config()->set_always_compare_med(true);
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

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(-1, path1.PathCompare(path2, true));
    EXPECT_EQ(1, path2.PathCompare(path1, true));
}

//
// Paths with sticky bit is chosen
// Test with path having same seq no and different sticky bit
//
TEST_F(BgpRouteTest, PathCompareSticky1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    MacMobility mm1(100, true);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(mm1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    MacMobility mm2(100, false);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(mm2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(1, path1.PathCompare(path2, true));
    EXPECT_EQ(-1, path2.PathCompare(path1, true));
}

//
// Paths with sticky bit is chosen.
// Test with path having different seq no and sticky bit
//
TEST_F(BgpRouteTest, PathCompareSticky2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    MacMobility mm1(200, true);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(mm1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    MacMobility mm2(100, false);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(mm2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(1, path1.PathCompare(path2, true));
    EXPECT_EQ(-1, path2.PathCompare(path1, true));
}

//
// Path with latest seq no is chosen
//
TEST_F(BgpRouteTest, PathCompareSeqNo) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    MacMobility mm1(100, true);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(mm1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    MacMobility mm2(200, true);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(mm2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(1, path1.PathCompare(path2, true));
    EXPECT_EQ(-1, path2.PathCompare(path1, true));
}

//
// ECMP Test with seq no.
// With same seq-no and sticky route
//
TEST_F(BgpRouteTest, PathCompareSeqNo1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    MacMobility mm1(100, true);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(mm1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    MacMobility mm2(100, true);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(mm2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// ECMP Test with seq no.
// With same seq-no and non-sticky route
//
TEST_F(BgpRouteTest, PathCompareSeqNo2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    MacMobility mm1(100, false);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(mm1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    MacMobility mm2(100, false);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(mm2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}
//
// ETree root Path is chosen
//
TEST_F(BgpRouteTest, PathCompareETree) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    ETree etree1(true, 100);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(etree1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    ETree etree2(false, 100);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(etree2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(1, path1.PathCompare(path2, true));
    EXPECT_EQ(-1, path2.PathCompare(path1, true));
}

//
// ETree root ECMP test
// Both path being root path
//
TEST_F(BgpRouteTest, PathCompareETree1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    ETree etree1(true, 100);
    ExtCommunitySpec ext_spec1;
    ext_spec1.communities.push_back(get_value(etree1.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec1;
    spec1.push_back(&ext_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    ETree etree2(true, 100);
    ExtCommunitySpec ext_spec2;
    ext_spec2.communities.push_back(get_value(etree2.GetExtCommunity().begin(), 8));
    BgpAttrSpec spec2;
    spec2.push_back(&ext_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

TEST_F(BgpRouteTest, CheckPrimaryPathsCountInTable1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetStale();
    path.SetLlgrStale();
    path.SetPolicyReject(); // Make the path infeasible.

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count + 1, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count + 1, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count + 1, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count + 1, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

TEST_F(BgpRouteTest, CheckPrimaryPathsCountInTable2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetStale();
    path.SetPolicyReject(); // Make the path infeasible.

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count + 1, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count + 1, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count + 1, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

TEST_F(BgpRouteTest, CheckPrimaryPathsCountInTable3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetLlgrStale();
    path.SetPolicyReject(); // Make the path infeasible.

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count + 1, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count + 1, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count + 1, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

TEST_F(BgpRouteTest, CheckSecondaryPathsCountInTable1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpSecondaryPath path(&peer1, 1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetStale();
    path.SetLlgrStale();
    path.SetPolicyReject(); // Make the path infeasible.

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count + 1, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count + 1, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count + 1, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count + 1, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

TEST_F(BgpRouteTest, CheckSecondaryPathsCountInTable2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpSecondaryPath path(&peer1, 1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetStale();
    path.SetLlgrStale();

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count + 1, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count + 1, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count + 1, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

TEST_F(BgpRouteTest, CheckSecondaryPathsCountInTable3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpSecondaryPath path(&peer1, 1, BgpPath::BGP_XMPP, attr1, 0, 0);

    path.SetStale();
    path.SetPolicyReject(); // Make the path infeasible.

    auto llgr_stale_path_count = table_.GetLlgrStalePathCount();
    auto stale_path_count = table_.GetStalePathCount();
    auto infeasible_path_count = table_.GetInfeasiblePathCount();
    auto primary_path_count = table_.GetPrimaryPathCount();
    auto secondary_path_count = table_.GetSecondaryPathCount();

    table_.UpdatePathCount(&path, +1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count + 1, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count + 1, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count + 1, table_.GetSecondaryPathCount());

    table_.UpdatePathCount(&path, -1);
    EXPECT_EQ(llgr_stale_path_count, table_.GetLlgrStalePathCount());
    EXPECT_EQ(stale_path_count, table_.GetStalePathCount());
    EXPECT_EQ(infeasible_path_count, table_.GetInfeasiblePathCount());
    EXPECT_EQ(primary_path_count, table_.GetPrimaryPathCount());
    EXPECT_EQ(secondary_path_count, table_.GetSecondaryPathCount());
}

//
// LlgrStale path is considered worse.
// Path2 is marked as LlgrStale
//
TEST_F(BgpRouteTest, PathCompareLlgrStale1) {
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
    path2.SetLlgrStale();

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// LlgrStale path is considered worse.
// Path2 is tagged with LLGR_STALE community
//
TEST_F(BgpRouteTest, PathCompareLlgrStale2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    CommunitySpec comm_spec1;
    comm_spec1.communities.push_back(CommunityType::LlgrStale);
    spec1.push_back(&comm_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    CommunitySpec comm_spec2;
    comm_spec2.communities.push_back(CommunityType::LlgrStale);
    spec2.push_back(&comm_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// LlgrStale path is considered worse.
// Path2 is both marked LlgrStale and is tagged with LLGR_STALE community
//
TEST_F(BgpRouteTest, PathCompareLlgrStale3) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    CommunitySpec comm_spec2;
    comm_spec2.communities.push_back(CommunityType::LlgrStale);
    spec2.push_back(&comm_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);
    path2.SetLlgrStale();

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
}

//
// Llgr Paths with same router id are considered are equal.
// Both Path1 and Path2 are marked LlgrStale
//
TEST_F(BgpRouteTest, PathCompareLlgrStale4) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);
    path1.SetLlgrStale();

    BgpAttrSpec spec2;
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);
    path2.SetLlgrStale();

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
}

//
// Llgr Paths with same router id are considered are equal.
// Both Path1 and Path2 are tagged with LLGR_STALE community
//
TEST_F(BgpRouteTest, PathCompareLlgrStale5) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    CommunitySpec comm_spec1;
    comm_spec1.communities.push_back(CommunityType::LlgrStale);
    spec1.push_back(&comm_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    CommunitySpec comm_spec2;
    comm_spec2.communities.push_back(CommunityType::LlgrStale);
    spec2.push_back(&comm_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
}

//
// Llgr Paths with same router id are considered are equal.
// Both Path1 and Path2 are marked LlgrStale and
// Both Path1 and Path2 are tagged with LLGR_STALE community
//
TEST_F(BgpRouteTest, PathCompareLlgrStale6) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    CommunitySpec comm_spec1;
    comm_spec1.communities.push_back(CommunityType::LlgrStale);
    spec1.push_back(&comm_spec1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path1(&peer1, BgpPath::BGP_XMPP, attr1, 0, 0);
    path1.SetLlgrStale();

    BgpAttrSpec spec2;
    CommunitySpec comm_spec2;
    comm_spec2.communities.push_back(CommunityType::LlgrStale);
    spec1.push_back(&comm_spec2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    PeerMock peer2(BgpProto::IBGP, Ip4Address::from_string("10.1.1.250", ec));
    BgpPath path2(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);
    path2.SetLlgrStale();

    EXPECT_EQ(0, path1.PathCompare(path2, false));
    EXPECT_EQ(0, path2.PathCompare(path1, false));
}

//
// Non-aliased paths are preferred if all attributes used for ECMP comparison
// are the same.
// The aliased flag is ignored for ECMP comparison.
//
TEST_F(BgpRouteTest, PathCompareAliased1) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec;
    BgpAttrPtr attr = db->Locate(spec);
    PeerMock peer(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    uint32_t flags1 = 0;
    BgpPath path1(&peer, BgpPath::BGP_XMPP, attr, flags1, 0);
    uint32_t flags2 = BgpPath::AliasedPath;
    BgpPath path2(&peer, BgpPath::BGP_XMPP, attr, flags2, 0);

    EXPECT_EQ(-1, path1.PathCompare(path2, false));
    EXPECT_EQ(1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

//
// Path id is compared after we look at the aliased flag i.e. aliased path
// is considered worse even if it has the lower path id.
// The aliased flag is ignored for ECMP comparison.
//
TEST_F(BgpRouteTest, PathCompareAliased2) {
    boost::system::error_code ec;
    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec;
    BgpAttrPtr attr = db->Locate(spec);
    PeerMock peer(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    uint32_t flags1 = BgpPath::AliasedPath;
    BgpPath path1(&peer, 10111, BgpPath::BGP_XMPP, attr, flags1, 0);
    uint32_t flags2 = 0;
    BgpPath path2(&peer, 10112, BgpPath::BGP_XMPP, attr, flags2, 0);

    EXPECT_EQ(1, path1.PathCompare(path2, false));
    EXPECT_EQ(-1, path2.PathCompare(path1, false));
    EXPECT_EQ(0, path1.PathCompare(path2, true));
    EXPECT_EQ(0, path2.PathCompare(path1, true));
}

TEST_F(BgpRouteTest, PathSameNeighborAs) {
    boost::system::error_code ec;

    BgpPath path1(BgpPath::None, 0);
    BgpPath path2(BgpPath::None, 0);
    PeerMock peer1(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BgpPath path3(&peer1, BgpPath:: BGP_XMPP, 0, 0, 0);
    PeerMock peer2(BgpProto::EBGP, Ip4Address::from_string("10.1.1.2", ec));
    BgpPath path4(&peer2, BgpPath:: BGP_XMPP, 0, 0, 0);

    // Null peer and IBGP peer are ignored
    EXPECT_FALSE(path1.PathSameNeighborAs(path2));
    EXPECT_FALSE(path3.PathSameNeighborAs(path2));
    EXPECT_FALSE(path4.PathSameNeighborAs(path2));
    EXPECT_FALSE(path4.PathSameNeighborAs(path3));

    BgpAttrDB *db = server_.attr_db();

    BgpAttrSpec spec1;
    AsPathSpec as_path1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps1->path_segment.push_back(20);
    as_path1.path_segments.push_back(ps1);
    spec1.push_back(&as_path1);
    BgpAttrPtr attr1 = db->Locate(spec1);
    BgpPath path5(&peer2, BgpPath::BGP_XMPP, attr1, 0, 0);

    BgpAttrSpec spec2;
    AsPathSpec as_path2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(100);
    ps2->path_segment.push_back(20);
    as_path2.path_segments.push_back(ps2);
    spec2.push_back(&as_path2);
    BgpAttrPtr attr2 = db->Locate(spec2);
    BgpPath path6(&peer2, BgpPath::BGP_XMPP, attr2, 0, 0);

    BgpAttrSpec spec3;
    spec3.push_back(&as_path1);
    BgpAttrPtr attr3 = db->Locate(spec3);
    BgpPath path7(&peer2, BgpPath::BGP_XMPP, attr3, 0, 0);

    EXPECT_FALSE(path5.PathSameNeighborAs(path6));
    EXPECT_TRUE(path5.PathSameNeighborAs(path7));
}

TEST_F(BgpRouteTest, GetSourceRouteDistinguisher) {
    BgpAttrDB *db = server_.attr_db();
    BgpAttrSpec attr_spec1;
    RouteDistinguisher rd1 = RouteDistinguisher::FromString("10.1.1.1:1");
    BgpAttrSourceRd rd_spec(rd1);
    attr_spec1.push_back(&rd_spec);
    BgpAttrPtr attr1 = db->Locate(attr_spec1);
    BgpPath path1(BgpPath::None, BgpPath::BGP_XMPP, attr1, 0, 0);

    EXPECT_EQ(rd1, path1.GetSourceRouteDistinguisher());

    BgpAttrSpec attr_spec2;
    string source_rd_str = "";
    BgpAttrSourceRd source_rd = BgpAttrSourceRd(
                   RouteDistinguisher::FromString(source_rd_str));
    attr_spec2.push_back(&source_rd);
    BgpAttrPtr attr2 = db->Locate(attr_spec2);
    BgpPath path2(BgpPath::None, BgpPath::BGP_XMPP, attr2, 0, 0);

    EXPECT_EQ(RouteDistinguisher::kZeroRd,
                   path2.GetSourceRouteDistinguisher());

    BgpSecondaryPath path(NULL, 1, BgpPath::BGP_XMPP, attr2, 0, 0);
    InetRoute route1(Ip4Prefix::FromString("10.1.1.2/32"));
    path.SetReplicateInfo(&table_, &route1);

    EXPECT_EQ(RouteDistinguisher::kZeroRd,
                   path.GetSourceRouteDistinguisher());

    string prefix_str("10.1.1.1:2:192.168.1.1/32");
    RouteDistinguisher rd2 = RouteDistinguisher::FromString("10.1.1.1:2");
    InetVpnRoute route2(InetVpnPrefix::FromString(prefix_str));
    path.SetReplicateInfo(&table_, &route2);

    EXPECT_EQ(rd2, path.GetSourceRouteDistinguisher());
}

TEST_F(BgpRouteTest, PathFlagsStringListNone) {
    vector<string> expected = boost::assign::list_of("None");
    EXPECT_EQ(expected, BgpPath(BgpPath::None, 0).GetFlagsStringList());
}

TEST_F(BgpRouteTest, PathFlagsStringListAll) {
    vector<string> expected = boost::assign::list_of
        ("AsPathLooped")
        ("NoNeighborAs")
        ("Stale")
        ("NoTunnelEncap")
        ("OriginatorIdLooped")
        ("ResolveNexthop")
        ("ResolvedPath")
        ("RoutingPolicyReject")
        ("LlgrStale")
        ("ClusterListLooped")
        ("AliasedPath")
        ("CheckGlobalErmVpnRoute")
    ;
    EXPECT_EQ(expected, BgpPath(BgpPath::None, NULL, ~0, 0, 0).
                            GetFlagsStringList());
}

TEST_F(BgpRouteTest, GetSourceString) {
    vector<BgpPath::PathSource> sources = boost::assign::list_of
        (BgpPath::None)
        (BgpPath::BGP_XMPP)
        (BgpPath::ServiceChain)
        (BgpPath::StaticRoute)
        (BgpPath::Aggregate)
        (BgpPath::Local)
    ;

    boost::system::error_code ec;
    PeerMock peer(BgpProto::IBGP, Ip4Address::from_string("10.1.1.1", ec));
    BOOST_FOREACH(BgpPath::PathSource source, sources) {
        switch (source) {
        case BgpPath::None:
            EXPECT_EQ("None", BgpPath(source, NULL, 0, 0, 0).
                                  GetSourceString(true));
            EXPECT_EQ("None", BgpPath(source, NULL, 0, 0, 0).
                                  GetSourceString(false));
            break;
        case BgpPath::BGP_XMPP:
            EXPECT_EQ("BGP_XMPP", BgpPath(source, NULL, 0, 0, 0).
                                      GetSourceString(true));
            EXPECT_EQ("None", BgpPath(source, NULL, 0, 0, 0).
                                  GetSourceString(false));
            EXPECT_EQ("BGP", BgpPath(&peer, 10111, BgpPath::BGP_XMPP, NULL, 0,
                                     0).GetSourceString(false));
            break;
        case BgpPath::ServiceChain:
            EXPECT_EQ("ServiceChain", BgpPath(source, NULL, 0, 0, 0).
                                  GetSourceString(true));
            EXPECT_EQ("ServiceChain", BgpPath(source, NULL, 0, 0, 0).
                                  GetSourceString(false));
            break;
        case BgpPath::StaticRoute:
            EXPECT_EQ("StaticRoute", BgpPath(source, NULL, 0, 0, 0).
                                         GetSourceString(true));
            EXPECT_EQ("StaticRoute", BgpPath(source, NULL, 0, 0, 0).
                                         GetSourceString(false));
            break;
        case BgpPath::Aggregate:
            EXPECT_EQ("Aggregate", BgpPath(source, NULL, 0, 0, 0).
                                       GetSourceString(true));
            EXPECT_EQ("Aggregate", BgpPath(source, NULL, 0, 0, 0).
                                       GetSourceString(false));
            break;
        case BgpPath::Local:
            EXPECT_EQ("Local", BgpPath(source, NULL, 0, 0, 0).
                                   GetSourceString(true));
            EXPECT_EQ("Local", BgpPath(source, NULL, 0, 0, 0).
                                   GetSourceString(false));
            break;
        }
    }
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
