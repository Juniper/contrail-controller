/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>

#include <sstream>

#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/community.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/router_mac.h"
#include "bgp/extended-community/sub_cluster.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "net/community_type.h"
#include "net/mac_address.h"

using boost::assign::list_of;
using boost::system::error_code;
using std::string;
using std::vector;

class BgpAttrTest : public ::testing::Test {
protected:
    BgpAttrTest()
        : server_(&evm_),
          attr_db_(server_.attr_db()),
          aspath_db_(server_.aspath_db()),
          cluster_list_db_(server_.cluster_list_db()),
          comm_db_(server_.comm_db()),
          edge_discovery_db_(server_.edge_discovery_db()),
          edge_forwarding_db_(server_.edge_forwarding_db()),
          extcomm_db_(server_.extcomm_db()),
          olist_db_(server_.olist_db()),
          ovnpath_db_(server_.ovnpath_db()),
          pmsi_tunnel_db_(server_.pmsi_tunnel_db()) {
    }

    void TearDown() {
        EXPECT_EQ(0, attr_db_->Size());
        EXPECT_EQ(0, aspath_db_->Size());
        EXPECT_EQ(0, comm_db_->Size());
        EXPECT_EQ(0, edge_discovery_db_->Size());
        EXPECT_EQ(0, edge_forwarding_db_->Size());
        EXPECT_EQ(0, extcomm_db_->Size());
        EXPECT_EQ(0, olist_db_->Size());
        EXPECT_EQ(0, ovnpath_db_->Size());
        EXPECT_EQ(0, pmsi_tunnel_db_->Size());
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AppendExtendedCommunity(ExtCommunity &comm,
                const ExtCommunity::ExtCommunityList &list) {
        comm.Append(list);
    }

    void PrependOriginVpn(OriginVnPath &path,
                          const OriginVnPath::OriginVnValue &value) {
        path.Prepend(value);
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
    AsPathDB *aspath_db_;
    ClusterListDB *cluster_list_db_;
    CommunityDB *comm_db_;
    EdgeDiscoveryDB *edge_discovery_db_;
    EdgeForwardingDB *edge_forwarding_db_;
    ExtCommunityDB *extcomm_db_;
    BgpOListDB *olist_db_;
    OriginVnPathDB *ovnpath_db_;
    PmsiTunnelDB *pmsi_tunnel_db_;
};

TEST_F(BgpAttrTest, UnknownCode) {
    BgpAttrSpec spec;

    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    spec.push_back(&origin);
    BgpAttribute unknown(BgpAttribute::Reserved, BgpAttribute::Optional);
    spec.push_back(&unknown);
    BgpAttrLocalPref local_pref(1729);
    spec.push_back(&local_pref);

    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_EQ(BgpAttrOrigin::IGP, attr->origin());
    EXPECT_EQ(1729, attr->local_pref());
}

TEST_F(BgpAttrTest, Origin1) {
    BgpAttrSpec spec;
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    spec.push_back(&origin);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_EQ(BgpAttrOrigin::IGP, attr->origin());
    EXPECT_EQ("igp", attr->origin_string());
}

TEST_F(BgpAttrTest, Origin2) {
    BgpAttrSpec spec;
    BgpAttrOrigin origin(BgpAttrOrigin::EGP);
    spec.push_back(&origin);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_EQ(BgpAttrOrigin::EGP, attr->origin());
    EXPECT_EQ("egp", attr->origin_string());
}

TEST_F(BgpAttrTest, Origin3) {
    BgpAttrSpec spec;
    BgpAttrOrigin origin(BgpAttrOrigin::INCOMPLETE);
    spec.push_back(&origin);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_EQ(BgpAttrOrigin::INCOMPLETE, attr->origin());
    EXPECT_EQ("incomplete", attr->origin_string());
}

TEST_F(BgpAttrTest, Origin4) {
    BgpAttrSpec spec;
    BgpAttrOrigin origin(255);
    spec.push_back(&origin);
    BgpAttrPtr attr = attr_db_->Locate(spec);
    EXPECT_EQ(255, attr->origin());
    EXPECT_EQ("unknown", attr->origin_string());
}

TEST_F(BgpAttrTest, MultiExitDiscCompare) {
    BgpAttrMultiExitDisc med1(100);
    BgpAttrMultiExitDisc med2(200);
    EXPECT_EQ(0, med1.CompareTo(med1));
    EXPECT_EQ(-1, med1.CompareTo(med2));
    EXPECT_EQ(1, med2.CompareTo(med1));
}

TEST_F(BgpAttrTest, MultiExitDiscToString) {
    BgpAttrMultiExitDisc med(100);
    EXPECT_EQ("MED <code: 4, flags: 80> : 100", med.ToString());
}

TEST_F(BgpAttrTest, AtomicAggregateToString) {
    BgpAttrAtomicAggregate aa;
    EXPECT_EQ("ATOMIC_AGGR <code: 6, flags: 40>", aa.ToString());
}

TEST_F(BgpAttrTest, AggregatorCompare1) {
    BgpAttrAggregator agg1(100, 0x0101010a);
    BgpAttrAggregator agg2(200, 0x0101010a);
    EXPECT_EQ(-1, agg1.CompareTo(agg2));
    EXPECT_EQ(1, agg2.CompareTo(agg1));
}

TEST_F(BgpAttrTest, AggregatorCompare2) {
    BgpAttrAggregator agg1(100, 0x0101010a);
    BgpAttrAggregator agg2(100, 0x0201010a);
    EXPECT_EQ(-1, agg1.CompareTo(agg2));
    EXPECT_EQ(1, agg2.CompareTo(agg1));
}

TEST_F(BgpAttrTest, AggregatorCompare3) {
    BgpAttrAggregator agg1(100, 0x0101010a);
    BgpAttrAggregator agg2(100, 0x0101010a);
    EXPECT_EQ(0, agg1.CompareTo(agg2));
    EXPECT_EQ(0, agg2.CompareTo(agg1));
}

TEST_F(BgpAttrTest, AggregatorToString) {
    BgpAttrAggregator agg(100, 0x0101010a);
    EXPECT_EQ("Aggregator <code: 7, flags: c0> : 100:0101010a", agg.ToString());
}

TEST_F(BgpAttrTest, AsPathLeftMost1) {
    AsPathSpec spec;
    EXPECT_EQ(0, spec.AsLeftMost());
}

TEST_F(BgpAttrTest, AsPathLeftMost2) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    EXPECT_EQ(0, spec.AsLeftMost());
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    EXPECT_EQ(0, spec.AsLeftMost());
}

TEST_F(BgpAttrTest, AsPathLeftMost3) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps->path_segment.push_back(64512);
    ps->path_segment.push_back(64513);
    ps->path_segment.push_back(64514);
    EXPECT_EQ(0, spec.AsLeftMost());
}

TEST_F(BgpAttrTest, AsPathLeftMost4) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(64512);
    ps->path_segment.push_back(64513);
    ps->path_segment.push_back(64514);
    EXPECT_EQ(64512, spec.AsLeftMost());
}

TEST_F(BgpAttrTest, AsPathLoop1) {
    AsPathSpec spec;
    EXPECT_FALSE(spec.AsPathLoop(64512, 0));
}

TEST_F(BgpAttrTest, AsPathLoop2) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    EXPECT_FALSE(spec.AsPathLoop(64512, 0));
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    EXPECT_FALSE(spec.AsPathLoop(64512, 0));
}

TEST_F(BgpAttrTest, AsPathLoop3) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(64512);
    ps->path_segment.push_back(64513);
    ps->path_segment.push_back(64514);
    EXPECT_TRUE(spec.AsPathLoop(64512, 0));
    EXPECT_TRUE(spec.AsPathLoop(64513, 0));
    EXPECT_TRUE(spec.AsPathLoop(64514, 0));

    EXPECT_FALSE(spec.AsPathLoop(64511, 0));
    EXPECT_FALSE(spec.AsPathLoop(64515, 0));

    EXPECT_FALSE(spec.AsPathLoop(64511, 1));
    EXPECT_FALSE(spec.AsPathLoop(64512, 1));
    EXPECT_FALSE(spec.AsPathLoop(64513, 1));
    EXPECT_FALSE(spec.AsPathLoop(64514, 1));
    EXPECT_FALSE(spec.AsPathLoop(64515, 1));
}

TEST_F(BgpAttrTest, AsPathLoop4) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps);

    vector<AsPathSpec::PathSegment::PathSegmentType> segment_type_list = list_of
        (AsPathSpec::PathSegment::AS_SET)
        (AsPathSpec::PathSegment::AS_SEQUENCE);
    BOOST_FOREACH(AsPathSpec::PathSegment::PathSegmentType segment_type,
        segment_type_list) {
        ps->path_segment_type = segment_type;

        ps->path_segment.clear();
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64513);
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64514);
        ps->path_segment.push_back(64512);
        EXPECT_TRUE(spec.AsPathLoop(64512, 0));
        EXPECT_TRUE(spec.AsPathLoop(64512, 1));
        EXPECT_TRUE(spec.AsPathLoop(64512, 2));
        EXPECT_FALSE(spec.AsPathLoop(64512, 3));

        ps->path_segment.clear();
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64513);
        ps->path_segment.push_back(64513);
        EXPECT_TRUE(spec.AsPathLoop(64512, 0));
        EXPECT_TRUE(spec.AsPathLoop(64512, 1));
        EXPECT_TRUE(spec.AsPathLoop(64512, 2));
        EXPECT_FALSE(spec.AsPathLoop(64512, 3));

        ps->path_segment.clear();
        ps->path_segment.push_back(64513);
        ps->path_segment.push_back(64513);
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64512);
        ps->path_segment.push_back(64512);
        EXPECT_TRUE(spec.AsPathLoop(64512, 0));
        EXPECT_TRUE(spec.AsPathLoop(64512, 1));
        EXPECT_TRUE(spec.AsPathLoop(64512, 2));
        EXPECT_FALSE(spec.AsPathLoop(64512, 3));
    }
}

TEST_F(BgpAttrTest, AsPathLoop5) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps1);
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps1->path_segment.push_back(64511);
    ps1->path_segment.push_back(64513);
    ps1->path_segment.push_back(64515);

    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps2);
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64512);

    EXPECT_TRUE(spec.AsPathLoop(64512, 0));
    EXPECT_TRUE(spec.AsPathLoop(64512, 1));
    EXPECT_TRUE(spec.AsPathLoop(64512, 2));
    EXPECT_FALSE(spec.AsPathLoop(64512, 3));
}

TEST_F(BgpAttrTest, AsPathCompare_Sequence) {
    AsPathSpec spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int idx = 1; idx < 5; idx++)
        ps1->path_segment.push_back(100 * idx);
    spec1.path_segments.push_back(ps1);
    AsPath path1(aspath_db_, spec1);

    AsPathSpec spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int idx = 4; idx >= 1; idx--)
        ps2->path_segment.push_back(100 * idx);
    spec2.path_segments.push_back(ps2);
    AsPath path2(aspath_db_, spec2);

    EXPECT_NE(0, path1.CompareTo(path2));
}

TEST_F(BgpAttrTest, AsPathCompare_Set) {
    AsPathSpec spec1;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    for (int idx = 1; idx < 5; idx++)
        ps1->path_segment.push_back(100 * idx);
    spec1.path_segments.push_back(ps1);
    AsPath path1(aspath_db_, spec1);

    AsPathSpec spec2;
    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    for (int idx = 4; idx >= 1; idx--)
        ps2->path_segment.push_back(100 * idx);
    spec2.path_segments.push_back(ps2);
    AsPath path2(aspath_db_, spec2);

    EXPECT_EQ(0, path1.CompareTo(path2));
}

TEST_F(BgpAttrTest, AsPathAdd) {
    AsPathSpec path;

    // Add to an empty path.
    AsPathSpec *p2 = path.Add(1000);
    EXPECT_EQ(1, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    // Populate the path with some entries.
    for (int i = 0; i < 10; i++) {
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        for (int j = 0; j < 10; j++) {
            ps->path_segment.push_back((i << 4) + j);
        }
        path.path_segments.push_back(ps);
    }

    p2 = path.Add(1000);
    EXPECT_EQ(10, p2->path_segments.size());
    EXPECT_EQ(11, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    // Add to a path whose first segment is of type AS_SET.
    path.path_segments[0]->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    p2 = path.Add(1000);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    // Add to a path whose first segment is of type AS_SEQUENCE but the segment
    // does not have enough room for the entire list.
    path.path_segments[0]->path_segment_type =
        AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int j = 11; j < 256; j++) {
        path.path_segments[0]->path_segment.push_back(j);
    }

    p2 = path.Add(1000);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;
}

TEST_F(BgpAttrTest, AsPathAddList) {
    AsPathSpec path;
    vector<as2_t> asn_list = list_of(1000)(2000);

    // Add to an empty path.
    AsPathSpec *p2 = path.Add(asn_list);
    EXPECT_EQ(1, p2->path_segments.size());
    EXPECT_EQ(2, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, p2->path_segments[0]->path_segment[1]);
    delete p2;

    // Populate the path with some entries.
    for (int i = 0; i < 10; i++) {
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        for (int j = 0; j < 10; j++) {
            ps->path_segment.push_back((i << 4) + j);
        }
        path.path_segments.push_back(ps);
    }

    p2 = path.Add(asn_list);
    EXPECT_EQ(10, p2->path_segments.size());
    EXPECT_EQ(12, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, p2->path_segments[0]->path_segment[1]);
    delete p2;

    // Add to a path whose first segment is of type AS_SET.
    path.path_segments[0]->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    p2 = path.Add(asn_list);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(2, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, p2->path_segments[0]->path_segment[1]);
    delete p2;

    // Add to a path whose first segment is of type AS_SEQUENCE but the segment
    // does not have enough room for the entire list.
    path.path_segments[0]->path_segment_type =
        AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int j = 11; j < 255; j++) {
        path.path_segments[0]->path_segment.push_back(j);
    }

    p2 = path.Add(asn_list);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(2, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    EXPECT_EQ(2000, p2->path_segments[0]->path_segment[1]);
    delete p2;
}

TEST_F(BgpAttrTest, AsPathReplace) {
    AsPathSpec spec;
    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps1);
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps1->path_segment.push_back(64511);
    ps1->path_segment.push_back(64513);
    ps1->path_segment.push_back(64515);

    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    spec.path_segments.push_back(ps2);
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps2->path_segment.push_back(64512);
    ps2->path_segment.push_back(64513);
    ps2->path_segment.push_back(64514);

    boost::scoped_ptr<AsPathSpec> new_spec1(spec.Replace(64513, 65000));
    EXPECT_FALSE(new_spec1->AsPathLoop(64513, 0));
    EXPECT_TRUE(new_spec1->AsPathLoop(65000, 1));
    EXPECT_FALSE(new_spec1->AsPathLoop(65000, 2));

    boost::scoped_ptr<AsPathSpec> new_spec2(new_spec1->Replace(65000, 64513));
    EXPECT_FALSE(new_spec2->AsPathLoop(65000, 0));
    EXPECT_TRUE(new_spec2->AsPathLoop(64513, 1));
    EXPECT_FALSE(new_spec2->AsPathLoop(64513, 2));

    EXPECT_EQ(0, spec.CompareTo(*new_spec2));
}

//
// Leftmost AS is private.
// Peer AS is 0 i.e. not specified.
// Test combinations of (bool all, as_t asn).
//
TEST_F(BgpAttrTest, AsPathRemovePrivate1) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(64513);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(200);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    bool all;
    as_t asn, peer_asn;

    // All private ASes till first public AS are removed since all is false.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(65534);
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are removed since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    all = true; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are replaced since all is true.
    // Last encountered public as is used as as replacement value.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(200);
    all = true; asn = 300; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Leftmost AS is non-private.
// Peer AS is 0 i.e. not specified.
// Test combinations of (bool all, as_t asn).
//
TEST_F(BgpAttrTest, AsPathRemovePrivate2) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(200);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    bool all;
    as_t asn, peer_asn;

    // Nothing is modified since leftmost AS is non-private and all is false.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(64512);
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(65534);
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are removed since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    all = true; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are replaced since all is true.
    // Last encountered public as is used as as replacement value.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(200);
    all = true; asn = 300; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Leftmost AS is private.
// Peer AS is same as leftmost AS.
// Test combinations of (bool all, as_t asn).
//
TEST_F(BgpAttrTest, AsPathRemovePrivate3) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(200);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    bool all;
    as_t asn, peer_asn;

    // Nothing is modified since leftmost AS is peer AS and all is false.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(64512);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(65534);
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 64512;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes except peer AS are removed since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(64512);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    all = true; asn = 0; peer_asn = 64512;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes except peer AS are replaced since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(64512);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(200);
    all = true; asn = 300; peer_asn = 64512;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Leftmost AS is private.
// Peer AS is same as 3rd leftmost private AS.
// Test combinations of (bool all, as_t asn).
//
TEST_F(BgpAttrTest, AsPathRemovePrivate4) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(64513);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(200);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    bool all;
    as_t asn, peer_asn;

    // All private ASes till peer AS are removed since all is false.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(65534);
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 65000;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes except peer AS are removed since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    all = true; asn = 0; peer_asn = 65000;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes except peer AS are replaced since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(200);
    all = true; asn = 300; peer_asn = 65000;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Leftmost AS is private.
// Peer AS is a non-private AS.
// Test combinations of (bool all, as_t asn).
//
TEST_F(BgpAttrTest, AsPathRemovePrivate5) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(64513);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(200);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    bool all;
    as_t asn, peer_asn;

    // All private ASes till peer AS are removed since all is false.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(65000);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(65534);
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 100;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are removed since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    all = true; asn = 0; peer_asn = 100;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));

    // All private ASes are replaced since all is true.
    eps1->path_segment.clear();
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(200);
    eps1->path_segment.push_back(200);
    all = true; asn = 300; peer_asn = 100;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Original spec has multiple segments.
// A segment will only private ASes is removed since all is true and private
// ASes are being removed, not replaced.
//
TEST_F(BgpAttrTest, AsPathRemovePrivate6) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *ops2 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops2);
    ops2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ops2->path_segment.push_back(64513);
    ops2->path_segment.push_back(64514);
    AsPathSpec::PathSegment *ops3 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops3);
    ops3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops3->path_segment.push_back(500);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps1->path_segment.push_back(100);
    AsPathSpec::PathSegment *eps2 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps2);
    eps2->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps2->path_segment.push_back(500);

    bool all;
    as_t asn, peer_asn;
    boost::scoped_ptr<AsPathSpec> result;
    all = true; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Original spec has multiple segments.
// A segment will only private ASes is not removed though all is true since
// private ASes are being replaced, not removed.
//
TEST_F(BgpAttrTest, AsPathRemovePrivate7) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *ops2 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops2);
    ops2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ops2->path_segment.push_back(64513);
    ops2->path_segment.push_back(64514);
    AsPathSpec::PathSegment *ops3 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops3);
    ops3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops3->path_segment.push_back(500);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(100);
    AsPathSpec::PathSegment *eps2 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps2);
    eps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    eps2->path_segment.push_back(100);
    eps2->path_segment.push_back(100);
    AsPathSpec::PathSegment *eps3 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps3);
    eps3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps3->path_segment.push_back(500);

    bool all;
    as_t asn, peer_asn;
    boost::scoped_ptr<AsPathSpec> result;
    all = true; asn = 300; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Original spec has multiple segments.
// ASes are being removed, not replaced.
// After the first segment is modified, remaining segments should be copied
// over unchanged since all is not true.
//
TEST_F(BgpAttrTest, AsPathRemovePrivate8) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(64513);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *ops2 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops2);
    ops2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ops2->path_segment.push_back(64514);
    ops2->path_segment.push_back(64515);
    AsPathSpec::PathSegment *ops3 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops3);
    ops3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops3->path_segment.push_back(500);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *eps2 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps2);
    eps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    eps2->path_segment.push_back(64514);
    eps2->path_segment.push_back(64515);
    AsPathSpec::PathSegment *eps3 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps3);
    eps3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps3->path_segment.push_back(500);

    bool all;
    as_t asn, peer_asn;
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 0; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// Original spec has multiple segments.
// ASes are being replaced, not removed.
// After the first segment is modified, remaining segments should be copied
// over unchanged since all is not true.
//
TEST_F(BgpAttrTest, AsPathRemovePrivate9) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(64513);
    ops1->path_segment.push_back(100);
    ops1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *ops2 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops2);
    ops2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ops2->path_segment.push_back(64514);
    ops2->path_segment.push_back(64515);
    AsPathSpec::PathSegment *ops3 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops3);
    ops3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops3->path_segment.push_back(500);

    AsPathSpec expected;
    AsPathSpec::PathSegment *eps1 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps1);
    eps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(300);
    eps1->path_segment.push_back(100);
    eps1->path_segment.push_back(65000);
    AsPathSpec::PathSegment *eps2 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps2);
    eps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    eps2->path_segment.push_back(64514);
    eps2->path_segment.push_back(64515);
    AsPathSpec::PathSegment *eps3 = new AsPathSpec::PathSegment;
    expected.path_segments.push_back(eps3);
    eps3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    eps3->path_segment.push_back(500);

    bool all;
    as_t asn, peer_asn;
    boost::scoped_ptr<AsPathSpec> result;
    all = false; asn = 300; peer_asn = 0;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

//
// All ASes are private.
// All is true, AS and Peer AS are both 0 i.e. not specified.
// Verify that we get an empty AsPath.
//
TEST_F(BgpAttrTest, AsPathRemovePrivate10) {
    AsPathSpec original;
    AsPathSpec::PathSegment *ops1 = new AsPathSpec::PathSegment;
    original.path_segments.push_back(ops1);
    ops1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ops1->path_segment.push_back(64512);
    ops1->path_segment.push_back(65000);
    ops1->path_segment.push_back(65534);

    AsPathSpec expected;
    bool all;
    as_t asn, peer_asn;
    all = true; asn = 0; peer_asn = 0;
    boost::scoped_ptr<AsPathSpec> result;
    result.reset(original.RemovePrivate(all, asn, peer_asn));
    EXPECT_EQ(0, result->CompareTo(expected));
}

TEST_F(BgpAttrTest, AsPathFormat1) {
    AsPathSpec spec;

    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int idx = 1; idx < 5; idx++)
        ps->path_segment.push_back(100 * idx);
    spec.path_segments.push_back(ps);

    AsPath path(aspath_db_, spec);
    EXPECT_EQ("100 200 300 400", path.path().ToString());
}

TEST_F(BgpAttrTest, AsPathFormat2) {
    AsPathSpec spec;

    AsPathSpec::PathSegment *ps1 = new AsPathSpec::PathSegment;
    ps1->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int idx = 1; idx < 3; idx++)
        ps1->path_segment.push_back(100 * idx);
    spec.path_segments.push_back(ps1);

    AsPathSpec::PathSegment *ps2 = new AsPathSpec::PathSegment;
    ps2->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    for (int idx = 4; idx >= 3; idx--)
        ps2->path_segment.push_back(100 * idx);
    spec.path_segments.push_back(ps2);

    AsPathSpec::PathSegment *ps3 = new AsPathSpec::PathSegment;
    ps3->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int idx = 6; idx >= 5; idx--)
        ps3->path_segment.push_back(100 * idx);
    spec.path_segments.push_back(ps3);

    AsPath path(aspath_db_, spec);
    EXPECT_EQ("100 200 {300 400} 600 500", path.path().ToString());
}

TEST_F(BgpAttrTest, ClusterList1) {
    BgpAttrSpec attr_spec;
    ClusterListSpec spec;
    for (int idx = 1; idx < 5; idx++)
        spec.cluster_list.push_back(100 * idx);
    attr_spec.push_back(&spec);
    BgpAttrPtr attr_ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(4, attr_ptr->cluster_list_length());
    BgpAttr attr(*attr_ptr);
    EXPECT_EQ(attr_ptr->cluster_list(), attr.cluster_list());
}

TEST_F(BgpAttrTest, ClusterList2) {
    ClusterListSpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.cluster_list.push_back(100 * idx);
    ClusterListSpec spec2;
    for (int idx = 2; idx < 5; idx++)
        spec2.cluster_list.push_back(100 * idx);
    ClusterListSpec spec3(100, &spec2);
    EXPECT_EQ(0, spec1.CompareTo(spec3));
    EXPECT_EQ(0, spec3.CompareTo(spec1));
}

TEST_F(BgpAttrTest, ClusterListCompare1) {
    ClusterListSpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.cluster_list.push_back(100 * idx);
    ClusterList clist1(cluster_list_db_, spec1);
    EXPECT_EQ(4, clist1.size());

    ClusterListSpec spec2;
    for (int idx = 4; idx >= 1; idx--)
        spec2.cluster_list.push_back(100 * idx);
    ClusterList clist2(cluster_list_db_, spec2);
    EXPECT_EQ(4, clist2.size());

    EXPECT_EQ(-1, clist1.CompareTo(clist2));
    EXPECT_EQ(1, clist2.CompareTo(clist1));
}

TEST_F(BgpAttrTest, ClusterListCompare2) {
    ClusterListSpec spec1;
    for (int idx = 1; idx < 3; idx++)
        spec1.cluster_list.push_back(100 * idx);
    ClusterList clist1(cluster_list_db_, spec1);
    EXPECT_EQ(2, clist1.size());

    ClusterListSpec spec2;
    for (int idx = 1; idx < 5; idx++)
        spec2.cluster_list.push_back(100 * idx);
    ClusterList clist2(cluster_list_db_, spec2);
    EXPECT_EQ(4, clist2.size());

    EXPECT_EQ(-1, clist1.CompareTo(clist2));
    EXPECT_EQ(1, clist2.CompareTo(clist1));
}

TEST_F(BgpAttrTest, ClusterListLoop) {
    ClusterListSpec spec;
    for (int idx = 1; idx < 5; idx++)
        spec.cluster_list.push_back(100 * idx);
    for (int idx = 1; idx < 5; idx++) {
        EXPECT_FALSE(spec.ClusterListLoop(100 * idx - 1));
        EXPECT_TRUE(spec.ClusterListLoop(100 * idx));
        EXPECT_FALSE(spec.ClusterListLoop(100 * idx + 1));
    }
}

TEST_F(BgpAttrTest, ClusterListToString) {
    ClusterListSpec spec;
    std::ostringstream oss;
    for (int idx = 1; idx < 5; idx++)
        spec.cluster_list.push_back(100 * idx);
    oss << "CLUSTER_LIST <code: 10, flags: 0x80> : ";
    oss << "0.0.0.100 0.0.0.200 0.0.1.44 0.0.1.144";
    EXPECT_EQ(oss.str(), spec.ToString());
}

TEST_F(BgpAttrTest, CommunityCompare1) {
    CommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    Community comm1(comm_db_, spec1);

    CommunitySpec spec2;
    for (int idx = 4; idx >= 1; idx--)
        spec2.communities.push_back(100 * idx);
    Community comm2(comm_db_, spec2);

    EXPECT_EQ(0, comm1.CompareTo(comm2));
}

TEST_F(BgpAttrTest, CommunityCompare2) {
    CommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    for (int idx = 4; idx >= 1; idx--)
        spec1.communities.push_back(100 * idx);
    Community comm1(comm_db_, spec1);

    CommunitySpec spec2;
    for (int idx = 1; idx < 5; idx++)
        spec2.communities.push_back(100 * idx);
    Community comm2(comm_db_, spec2);

    EXPECT_EQ(0, comm1.CompareTo(comm2));
}

TEST_F(BgpAttrTest, CommunityBuildStringList1) {
    CommunitySpec spec;
    spec.communities.push_back(0xFFFF0000);
    spec.communities.push_back(CommunityType::AcceptOwn);
    spec.communities.push_back(CommunityType::NoExport);
    spec.communities.push_back(CommunityType::NoAdvertise);
    spec.communities.push_back(CommunityType::NoExportSubconfed);
    spec.communities.push_back(CommunityType::LlgrStale);
    spec.communities.push_back(CommunityType::NoLlgr);
    spec.communities.push_back(CommunityType::AcceptOwnNexthop);
    Community comm(comm_db_, spec);

    vector<string> expected_list = list_of("65535:0")
        ("accept-own")
        ("llgr-stale")("no-llgr")
        ("accept-own-nexthop")
        ("no-export")("no-advertise")("no-export-subconfed");
    vector<string> result_list;
    comm.BuildStringList(&result_list);
    EXPECT_EQ(expected_list, result_list);
}

TEST_F(BgpAttrTest, CommunityBuildStringList2) {
    CommunitySpec spec;
    spec.communities.push_back(CommunityType::AcceptOwnNexthop);
    spec.communities.push_back(CommunityType::NoExportSubconfed);
    spec.communities.push_back(CommunityType::LlgrStale);
    spec.communities.push_back(CommunityType::NoLlgr);
    spec.communities.push_back(CommunityType::NoAdvertise);
    spec.communities.push_back(CommunityType::NoExport);
    spec.communities.push_back(CommunityType::AcceptOwn);
    spec.communities.push_back(0xFFFF0000);
    Community comm(comm_db_, spec);

    vector<string> expected_list = list_of("65535:0")
        ("accept-own")
        ("llgr-stale")("no-llgr")
        ("accept-own-nexthop")
        ("no-export")("no-advertise")("no-export-subconfed");
    vector<string> result_list;
    comm.BuildStringList(&result_list);
    EXPECT_EQ(expected_list, result_list);
}

TEST_F(BgpAttrTest, ExtCommunityCompare1) {
    ExtCommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    ExtCommunity extcomm1(extcomm_db_, spec1);

    ExtCommunitySpec spec2;
    for (int idx = 4; idx >= 1; idx--)
        spec2.communities.push_back(100 * idx);
    ExtCommunity extcomm2(extcomm_db_, spec2);

    EXPECT_EQ(0, extcomm1.CompareTo(extcomm2));
}

TEST_F(BgpAttrTest, ExtCommunityCompare2) {
    ExtCommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    for (int idx = 4; idx >= 1; idx--)
        spec1.communities.push_back(100 * idx);
    ExtCommunity extcomm1(extcomm_db_, spec1);

    ExtCommunitySpec spec2;
    for (int idx = 1; idx < 5; idx++)
        spec2.communities.push_back(100 * idx);
    ExtCommunity extcomm2(extcomm_db_, spec2);

    EXPECT_EQ(0, extcomm1.CompareTo(extcomm2));
}

TEST_F(BgpAttrTest, ExtCommunityAppend1) {
    ExtCommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    ExtCommunity extcomm1(extcomm_db_, spec1);

    ExtCommunity::ExtCommunityList list;
    for (int idx = 8; idx >= 5; idx--) {
        ExtCommunity::ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), 100 * idx);
        list.push_back(comm);
    }
    AppendExtendedCommunity(extcomm1, list);

    ExtCommunitySpec spec2;
    for (int idx = 1; idx < 9; idx++)
        spec2.communities.push_back(100 * idx);
    ExtCommunity extcomm2(extcomm_db_, spec2);

    EXPECT_EQ(0, extcomm1.CompareTo(extcomm2));
}

TEST_F(BgpAttrTest, ExtCommunityAppend2) {
    ExtCommunitySpec spec1;
    for (int idx = 1; idx < 5; idx++)
        spec1.communities.push_back(100 * idx);
    ExtCommunity extcomm1(extcomm_db_, spec1);

    ExtCommunity::ExtCommunityList list;
    for (int idx = 1; idx < 5; idx++) {
        ExtCommunity::ExtCommunityValue comm;
        put_value(comm.data(), comm.size(), 100 * idx);
        list.push_back(comm);
    }
    AppendExtendedCommunity(extcomm1, list);

    ExtCommunitySpec spec2;
    for (int idx = 1; idx < 5; idx++)
        spec2.communities.push_back(100 * idx);
    ExtCommunity extcomm2(extcomm_db_, spec2);

    EXPECT_EQ(0, extcomm1.CompareTo(extcomm2));
}

TEST_F(BgpAttrTest, SequenceNumber1) {
    BgpAttrSpec attr_spec;
    ExtCommunitySpec spec;
    for (int idx = 1; idx < 5; idx++)
        spec.communities.push_back(100 * idx);
    attr_spec.push_back(&spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(0, attr->sequence_number());
}

TEST_F(BgpAttrTest, SequenceNumber2) {
    BgpAttrSpec attr_spec;
    ExtCommunitySpec spec;
    MacMobility mm(13);
    spec.communities.push_back(mm.GetExtCommunityValue());
    attr_spec.push_back(&spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(13, attr->sequence_number());
}

TEST_F(BgpAttrTest, ExtCommunitySubCluster) {
    BgpAttrSpec attr_spec;
    ExtCommunitySpec spec;
    SubCluster sc1(64512, 100);
    spec.communities.push_back(sc1.GetExtCommunityValue());
    attr_spec.push_back(&spec);
    BgpAttrPtr attr_ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ("subcluster:64512:100", sc1.ToString());
    EXPECT_EQ(100, attr_ptr->ext_community()->GetSubClusterId());
    const BgpAttr *attr = attr_ptr.get();
    ExtCommunityPtr ext_community = attr->ext_community();
    SubCluster sc2(64512, 200);
    ext_community = extcomm_db_->
        ReplaceSubClusterAndLocate(ext_community.get(), sc2.GetExtCommunity());
    attr_ptr = attr_db_->ReplaceExtCommunityAndLocate(attr, ext_community);
    attr = attr_ptr.get();
    EXPECT_EQ(200, attr_ptr->ext_community()->GetSubClusterId());
    EXPECT_EQ("subcluster:64512:200",
            ext_community.get()->ToString(sc2.GetExtCommunity()));
}

TEST_F(BgpAttrTest, RouterMac1) {
    BgpAttrSpec attr_spec;
    ExtCommunitySpec spec;
    for (int idx = 1; idx < 5; idx++)
        spec.communities.push_back(100 * idx);
    attr_spec.push_back(&spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_TRUE(attr->mac_address().IsZero());
}

TEST_F(BgpAttrTest, RouterMac2) {
    BgpAttrSpec attr_spec;
    boost::system::error_code ec;
    MacAddress mac_addr = MacAddress::FromString("01:02:03:04:05:06", &ec);
    EXPECT_EQ(0, ec.value());
    RouterMac router_mac(mac_addr);
    ExtCommunitySpec spec;
    spec.communities.push_back(router_mac.GetExtCommunityValue());
    attr_spec.push_back(&spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_FALSE(attr->mac_address().IsZero());
    EXPECT_EQ(mac_addr, attr->mac_address());
}

TEST_F(BgpAttrTest, OriginVnPathToString) {
    OriginVnPathSpec spec;
    for (int idx = 1; idx < 5; idx++)
        spec.origin_vns.push_back(100 * idx);
    EXPECT_EQ("OriginVnPath <code: 243, flags: c0> : 4", spec.ToString());
}

TEST_F(BgpAttrTest, OriginVnPathCompare1) {
    OriginVnPathSpec spec1;
    for (int idx = 1; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec1.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath1(ovnpath_db_, spec1);

    OriginVnPathSpec spec2;
    for (int idx = 4; idx >= 1; idx--) {
        OriginVn origin_vn(64512, 100 * idx);
        spec2.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath2(ovnpath_db_, spec2);

    EXPECT_NE(0, ovnpath1.CompareTo(ovnpath2));
    EXPECT_NE(0, ovnpath2.CompareTo(ovnpath1));
}

TEST_F(BgpAttrTest, OriginVnPathCompare2) {
    OriginVnPathSpec spec1;
    for (int idx = 1; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec1.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath1(ovnpath_db_, spec1);

    OriginVnPathSpec spec2;
    for (int idx = 1; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec2.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath2(ovnpath_db_, spec2);

    EXPECT_EQ(0, ovnpath1.CompareTo(ovnpath2));
    EXPECT_EQ(0, ovnpath2.CompareTo(ovnpath1));
}

TEST_F(BgpAttrTest, OriginVnPathPrepend) {
    OriginVnPathSpec spec1;
    for (int idx = 5; idx < 9; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec1.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath1(ovnpath_db_, spec1);

    for (int idx = 4; idx >= 1; idx--) {
        OriginVn origin_vn(64512, 100 * idx);
        PrependOriginVpn(ovnpath1, origin_vn.GetExtCommunity());
    }

    OriginVnPathSpec spec2;
    for (int idx = 1; idx < 9; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec2.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath2(ovnpath_db_, spec2);

    EXPECT_EQ(0, ovnpath1.CompareTo(ovnpath2));
    EXPECT_EQ(0, ovnpath2.CompareTo(ovnpath1));
}

//
// Both AS and VN index are compared for OriginVns with index from non-global
// range.
//
TEST_F(BgpAttrTest, OriginVnPathContains1) {
    OriginVnPathSpec spec;
    for (int idx = 9; idx >= 1; idx -= 2) {
        OriginVn origin_vn(64512, 100 * idx);
        spec.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPath ovnpath(ovnpath_db_, spec);

    for (int idx = 1; idx <= 9; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        if (idx % 2 == 1) {
            EXPECT_TRUE(ovnpath.Contains(origin_vn.GetExtCommunity()));
        } else {
            EXPECT_FALSE(ovnpath.Contains(origin_vn.GetExtCommunity()));
        }
    }

    for (int idx = 8; idx > 0; idx -= 2) {
        OriginVn origin_vn(64512, 100 * idx);
        PrependOriginVpn(ovnpath, origin_vn.GetExtCommunity());
    }

    for (int idx = 1; idx <= 9; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        EXPECT_TRUE(ovnpath.Contains(origin_vn.GetExtCommunity()));
    }
}

//
// OriginVns with matching VN index from global range are treated as equal
// even if the AS numbers are different.
// OriginVns with matching VN index from non-global range are treated as not
// equal because AS numbers are different.
//
TEST_F(BgpAttrTest, OriginVnPathContains2) {
    OriginVnPathSpec spec;
    for (int idx = 9; idx >= 1; idx--) {
        if (idx % 2 == 1) {
            OriginVn origin_vn(64512, OriginVn::kMinGlobalId + 100 * idx);
            spec.origin_vns.push_back(origin_vn.GetExtCommunityValue());
        } else {
            OriginVn origin_vn(64512, 100 * idx);
            spec.origin_vns.push_back(origin_vn.GetExtCommunityValue());
        }
    }
    OriginVnPath ovnpath(ovnpath_db_, spec);

    for (int idx = 1; idx <= 9; idx++) {
        if (idx % 2 == 1) {
            OriginVn origin_vn(64513, OriginVn::kMinGlobalId + 100 * idx);
            EXPECT_TRUE(ovnpath.Contains(origin_vn.GetExtCommunity()));
        } else {
            OriginVn origin_vn(64513, OriginVn::kMinGlobalId + 100 * idx);
            EXPECT_FALSE(ovnpath.Contains(origin_vn.GetExtCommunity()));
        }
    }
}

TEST_F(BgpAttrTest, OriginVnPathLocate) {
    OriginVnPathSpec spec1;
    for (int idx = 1; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec1.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPathPtr ovnpath1 = ovnpath_db_->Locate(spec1);
    EXPECT_EQ(1, ovnpath_db_->Size());

    OriginVnPathSpec spec2;
    for (int idx = 4; idx >= 1; idx--) {
        OriginVn origin_vn(64512, 100 * idx);
        spec2.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPathPtr ovnpath2 = ovnpath_db_->Locate(spec2);
    EXPECT_EQ(2, ovnpath_db_->Size());

    EXPECT_NE(0, ovnpath1->CompareTo(*ovnpath2));
    EXPECT_NE(0, ovnpath2->CompareTo(*ovnpath1));
}

TEST_F(BgpAttrTest, OriginVnPathPrependAndLocate) {
    OriginVnPathSpec spec;
    for (int idx = 2; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    OriginVnPathPtr ovnpath1 = ovnpath_db_->Locate(spec);
    EXPECT_EQ(1, ovnpath_db_->Size());

    OriginVn origin_vn(64512, 100 * 1);
    OriginVnPathPtr ovnpath2 = ovnpath_db_->PrependAndLocate(ovnpath1.get(),
        origin_vn.GetExtCommunity());
    EXPECT_EQ(2, ovnpath_db_->Size());

    EXPECT_NE(0, ovnpath1->CompareTo(*ovnpath2));
    EXPECT_NE(0, ovnpath2->CompareTo(*ovnpath1));

    EXPECT_FALSE(ovnpath1->Contains(origin_vn.GetExtCommunity()));
    EXPECT_TRUE(ovnpath2->Contains(origin_vn.GetExtCommunity()));
}

TEST_F(BgpAttrTest, OriginVnPathReplace) {
    OriginVnPathSpec spec;
    for (int idx = 2; idx < 5; idx++) {
        OriginVn origin_vn(64512, 100 * idx);
        spec.origin_vns.push_back(origin_vn.GetExtCommunityValue());
    }
    BgpAttrSpec attr_spec;
    attr_spec.push_back(&spec);
    BgpAttrPtr ptr1 = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, ovnpath_db_->Size());
    EXPECT_EQ(1, attr_db_->Size());

    OriginVn origin_vn(64512, 100 * 1);
    OriginVnPathPtr ovnpath = ovnpath_db_->PrependAndLocate(
        ptr1->origin_vn_path(), origin_vn.GetExtCommunity());
    BgpAttrPtr ptr2 =
        attr_db_->ReplaceOriginVnPathAndLocate(ptr1.get(), ovnpath);
    EXPECT_EQ(2, ovnpath_db_->Size());
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, OriginatorId1) {
    BgpAttrSpec attr_spec;
    error_code ec;
    Ip4Address originator_id = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId originator_id_spec(originator_id.to_ulong());
    attr_spec.push_back(&originator_id_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(originator_id, ptr->originator_id());
    BgpAttr attr(*(ptr.get()));
    EXPECT_EQ(attr.originator_id(), ptr->originator_id());
}

TEST_F(BgpAttrTest, OriginatorId2) {
    BgpAttrSpec attr_spec1;
    error_code ec1;
    Ip4Address originator_id1 = Ip4Address::from_string("10.1.1.1", ec1);
    BgpAttrOriginatorId originator_id_spec1(originator_id1.to_ulong());
    attr_spec1.push_back(&originator_id_spec1);
    BgpAttrPtr ptr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    error_code ec2;
    Ip4Address originator_id2 = Ip4Address::from_string("10.1.1.2", ec2);
    BgpAttrOriginatorId originator_id_spec2(originator_id2.to_ulong());
    attr_spec2.push_back(&originator_id_spec2);
    BgpAttrPtr ptr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_NE(0, ptr1->CompareTo(*ptr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, OriginatorId3) {
    BgpAttrSpec attr_spec;
    error_code ec1;
    Ip4Address originator_id1 = Ip4Address::from_string("10.1.1.1", ec1);
    BgpAttrOriginatorId originator_id_spec(originator_id1.to_ulong());
    attr_spec.push_back(&originator_id_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(originator_id1, ptr->originator_id());

    error_code ec2;
    Ip4Address originator_id2 = Ip4Address::from_string("10.1.1.2", ec2);
    ptr = attr_db_->ReplaceOriginatorIdAndLocate(ptr.get(), originator_id2);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(originator_id2, ptr->originator_id());
}

TEST_F(BgpAttrTest, OriginatorId4) {
    error_code ec;
    Ip4Address originator_id = Ip4Address::from_string("10.1.1.1", ec);
    BgpAttrOriginatorId originator_id_spec(originator_id.to_ulong());
    EXPECT_EQ("OriginatorId <code: 9, flags: 0x80> : 10.1.1.1",
        originator_id_spec.ToString());
}

TEST_F(BgpAttrTest, OriginatorIdCompareTo) {
    BgpAttrOriginatorId oid_spec1(0x0101010a);
    BgpAttrOriginatorId oid_spec2(0x0201010a);
    EXPECT_EQ(0, oid_spec1.CompareTo(oid_spec1));
    EXPECT_NE(0, oid_spec1.CompareTo(oid_spec2));
    EXPECT_NE(0, oid_spec2.CompareTo(oid_spec1));
}

TEST_F(BgpAttrTest, SourceRdBasic1) {
    BgpAttrSpec attr_spec;
    RouteDistinguisher rd = RouteDistinguisher::FromString("192.168.0.1:1");
    BgpAttrSourceRd rd_spec(rd);
    attr_spec.push_back(&rd_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_FALSE(ptr->source_rd().IsZero());
    EXPECT_EQ(rd, ptr->source_rd());
    BgpAttr attr(*(ptr.get()));
    EXPECT_EQ(attr.source_rd(), ptr->source_rd());
}

TEST_F(BgpAttrTest, SourceRdBasic2) {
    BgpAttrSpec attr_spec1;
    BgpAttrLocalPref lpref_spec1(100);
    attr_spec1.push_back(&lpref_spec1);
    BgpAttrSourceRd rd_spec1(RouteDistinguisher::FromString("192.168.0.1:1"));
    attr_spec1.push_back(&rd_spec1);
    BgpAttrPtr ptr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    BgpAttrLocalPref lpref_spec2(100);
    attr_spec2.push_back(&lpref_spec2);
    BgpAttrSourceRd rd_spec2(RouteDistinguisher::FromString("192.168.0.1:2"));
    attr_spec2.push_back(&rd_spec2);
    BgpAttrPtr ptr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, SourceRdBasic3) {
    BgpAttrSpec attr_spec;

    RouteDistinguisher rd1 = RouteDistinguisher::FromString("192.168.0.1:1");
    BgpAttrSourceRd rd_spec(rd1);
    attr_spec.push_back(&rd_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(rd1, ptr->source_rd());

    RouteDistinguisher rd2 = RouteDistinguisher::FromString("192.168.0.1:2");
    ptr = attr_db_->ReplaceSourceRdAndLocate(ptr.get(), rd2);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(rd2, ptr->source_rd());
}

TEST_F(BgpAttrTest, SourceRdCompareTo) {
    RouteDistinguisher rd1 = RouteDistinguisher::FromString("192.168.0.1:1");
    RouteDistinguisher rd2 = RouteDistinguisher::FromString("192.168.0.1:2");
    BgpAttrSourceRd rd_spec1(rd1);
    BgpAttrSourceRd rd_spec2(rd2);
    EXPECT_EQ(0, rd_spec1.CompareTo(rd_spec1));
    EXPECT_NE(0, rd_spec1.CompareTo(rd_spec2));
    EXPECT_NE(0, rd_spec2.CompareTo(rd_spec1));
}

TEST_F(BgpAttrTest, SourceRdToString) {
    RouteDistinguisher rd = RouteDistinguisher::FromString("192.168.0.1:1");
    BgpAttrSourceRd rd_spec(rd);
    EXPECT_EQ("SourceRd <subcode: 3> : 192.168.0.1:1", rd_spec.ToString());
}

TEST_F(BgpAttrTest, Esi1) {
    BgpAttrSpec attr_spec;
    EthernetSegmentId esi =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:09");
    BgpAttrEsi esi_spec(esi);
    attr_spec.push_back(&esi_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_FALSE(ptr->esi().IsZero());
    EXPECT_EQ(esi, ptr->esi());
    BgpAttr attr(*ptr);
    EXPECT_EQ(attr.esi(), ptr->esi());
}

TEST_F(BgpAttrTest, Esi2) {
    BgpAttrSpec attr_spec1;
    EthernetSegmentId esi1 =
        EthernetSegmentId::FromString("01:02:03:04:05:06:07:08:09:01");
    BgpAttrEsi esi_spec1(esi1);
    attr_spec1.push_back(&esi_spec1);
    BgpAttrPtr ptr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    EthernetSegmentId esi2 =
        EthernetSegmentId::FromString("01:02:03:04:05:06:07:08:09:02");
    BgpAttrEsi esi_spec2(esi2);
    attr_spec2.push_back(&esi_spec2);
    BgpAttrPtr ptr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_NE(0, ptr1->CompareTo(*ptr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, Esi3) {
    BgpAttrSpec attr_spec;
    EthernetSegmentId esi1 =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:01");
    BgpAttrEsi esi_spec(esi1);
    attr_spec.push_back(&esi_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(esi1, ptr->esi());

    EthernetSegmentId esi2 =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:02");
    ptr = attr_db_->ReplaceEsiAndLocate(ptr.get(), esi2);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(esi2, ptr->esi());
}

TEST_F(BgpAttrTest, EsiCompareTo) {
    EthernetSegmentId esi1 =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:01");
    EthernetSegmentId esi2 =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:02");
    BgpAttrEsi esi_spec1(esi1);
    BgpAttrEsi esi_spec2(esi2);
    EXPECT_EQ(0, esi_spec1.CompareTo(esi_spec1));
    EXPECT_NE(0, esi_spec1.CompareTo(esi_spec2));
    EXPECT_NE(0, esi_spec2.CompareTo(esi_spec1));
}

TEST_F(BgpAttrTest, EsiToString) {
    EthernetSegmentId esi =
        EthernetSegmentId::FromString("00:01:02:03:04:05:06:07:08:01");
    BgpAttrEsi esi_spec(esi);
    EXPECT_EQ("Esi <subcode: 4> : 00:01:02:03:04:05:06:07:08:01",
        esi_spec.ToString());
}

TEST_F(BgpAttrTest, Params1) {
    BgpAttrSpec attr_spec;
    uint64_t params = BgpAttrParams::TestFlag;
    BgpAttrParams params_spec(params);
    attr_spec.push_back(&params_spec);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(params, ptr->params());
    BgpAttr attr(*ptr);
    EXPECT_EQ(attr.params(), ptr->params());
}

TEST_F(BgpAttrTest, Params2) {
    BgpAttrSpec attr_spec1;
    uint64_t params1 = 0;
    BgpAttrParams params_spec1(params1);
    attr_spec1.push_back(&params_spec1);
    BgpAttrPtr ptr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    uint64_t params2 = BgpAttrParams::TestFlag;
    BgpAttrParams params_spec2(params2);
    attr_spec2.push_back(&params_spec2);
    BgpAttrPtr ptr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_NE(0, ptr1->CompareTo(*ptr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, ParamsCompareTo) {
    uint64_t params1 = BgpAttrParams::TestFlag;
    uint64_t params2 = 0;
    BgpAttrParams params_spec1(params1);
    BgpAttrParams params_spec2(params2);
    EXPECT_EQ(0, params_spec1.CompareTo(params_spec1));
    EXPECT_NE(0, params_spec1.CompareTo(params_spec2));
    EXPECT_NE(0, params_spec2.CompareTo(params_spec1));
}

TEST_F(BgpAttrTest, ParamsToString) {
    uint64_t params = BgpAttrParams::TestFlag;
    BgpAttrParams params_spec(params);
    EXPECT_EQ("Params <subcode: 5> : 0x0000000000000001",
        params_spec.ToString());
}

TEST_F(BgpAttrTest, SubProtocol) {
    BgpAttrSpec attr_spec;
    std::string sbp("interface-static");
    BgpAttrSubProtocol sp(sbp);
    attr_spec.push_back(&sp);
    BgpAttrPtr ptr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(sbp, ptr->sub_protocol());
}

TEST_F(BgpAttrTest, SubProtocolCompareTo) {
    std::string sp1 = "interface";
    std::string sp2 = "interface-static";
    BgpAttrSubProtocol sbp1(sp1);
    BgpAttrSubProtocol sbp2(sp2);
    EXPECT_EQ(0, sbp1.CompareTo(sbp1));
    EXPECT_NE(0, sbp1.CompareTo(sbp2));
    EXPECT_NE(0, sbp2.CompareTo(sbp1));
}

TEST_F(BgpAttrTest, SubProtcolToString) {
    std::string sp = "interface";
    BgpAttrSubProtocol sbp(sp);
    EXPECT_EQ("SubProtocol <subcode: 7> : interface", sbp.ToString());
}


TEST_F(BgpAttrTest, PmsiTunnelSpec) {
    PmsiTunnelSpec pmsi_spec;
    EXPECT_EQ(0, pmsi_spec.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec.flags);
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo1a) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    EXPECT_EQ(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_EQ(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo1b) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    error_code ec;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.label = 999;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    pmsi_spec2.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec2.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec2.label = 999;
    pmsi_spec2.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    EXPECT_EQ(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_EQ(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo2a) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::ARLeaf;
    pmsi_spec2.tunnel_flags = PmsiTunnelSpec::ARReplicator;
    EXPECT_NE(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_NE(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo2b) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec2.tunnel_type = PmsiTunnelSpec::AssistedReplicationContrail;
    EXPECT_NE(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_NE(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo2c) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    pmsi_spec1.label = 1001;
    pmsi_spec2.label = 2002;
    EXPECT_NE(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_NE(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecCompareTo2d) {
    PmsiTunnelSpec pmsi_spec1;
    PmsiTunnelSpec pmsi_spec2;
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    pmsi_spec2.SetIdentifier(Ip4Address::from_string("10.1.1.2", ec));
    EXPECT_NE(0, pmsi_spec1.CompareTo(pmsi_spec2));
    EXPECT_NE(0, pmsi_spec2.CompareTo(pmsi_spec1));
}

TEST_F(BgpAttrTest, PmsiTunnelSpecToString1) {
    PmsiTunnelSpec pmsi_spec;
    std::ostringstream oss;
    oss << "PmsiTunnel <code: 22, flags: 0xc0>";
    oss << " Tunnel Flags: 0x0 Tunnel Type: 0 Label: 0x";
    oss << std::hex << int(pmsi_spec.label) << std::dec;
    oss << " (" << int(pmsi_spec.label) << ") Identifier: 0.0.0.0";
    EXPECT_EQ(oss.str(), pmsi_spec.ToString());
}

TEST_F(BgpAttrTest, PmsiTunnelSpecToString2) {
    ExtCommunitySpec spec;
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    std::ostringstream oss;
    oss << "PmsiTunnel <code: 22, flags: 0xc0>";
    oss << " Tunnel Flags: 0x80 Tunnel Type: 6 Label: 0x";
    oss << std::hex << int(pmsi_spec.label) << std::dec;
    oss << " (" << int(pmsi_spec.label) << ") Identifier: 10.1.1.1";
    EXPECT_EQ(oss.str(), pmsi_spec.ToString());
}

TEST_F(BgpAttrTest, PmsiTunnelSpecTunnelTypeString) {
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::RsvpP2mpLsp;
    EXPECT_EQ("RsvpP2mpLsp", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::LdpP2mpLsp;
    EXPECT_EQ("LdpP2mpLsp", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::PimSsmTree;
    EXPECT_EQ("PimSsmTree", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::PimSmTree;
    EXPECT_EQ("PimSmTree", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::BidirPimTree;
    EXPECT_EQ("BidirPimTree", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    EXPECT_EQ("IngressReplication", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::MldpMp2mpLsp;
    EXPECT_EQ("MldpMp2mpLsp", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::AssistedReplicationContrail;
    EXPECT_EQ("AssistedReplication", pmsi_spec.GetTunnelTypeString());
    pmsi_spec.tunnel_type = PmsiTunnelSpec::NoTunnelInfo;
    EXPECT_EQ("Unknown(0)", pmsi_spec.GetTunnelTypeString());
}

TEST_F(BgpAttrTest, PmsiTunnelSpecTunnelArTypeString) {
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::RegularNVE;
    EXPECT_EQ("RegularNVE", pmsi_spec.GetTunnelArTypeString());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::ARReplicator;
    EXPECT_EQ("ARReplicator", pmsi_spec.GetTunnelArTypeString());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::ARLeaf;
    EXPECT_EQ("ARLeaf", pmsi_spec.GetTunnelArTypeString());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::AssistedReplicationType;
    EXPECT_EQ("Unknown", pmsi_spec.GetTunnelArTypeString());
}

TEST_F(BgpAttrTest, PmsiTunnelSpecTunnelFlagsStrings) {
    PmsiTunnelSpec pmsi_spec;
    vector<string> flags;
    flags = list_of("None").convert_to_container<vector<string> >();
    EXPECT_EQ(flags, pmsi_spec.GetTunnelFlagsStrings());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
    flags = list_of("LeafInfoRequired").convert_to_container<vector<string> >();
    EXPECT_EQ(flags, pmsi_spec.GetTunnelFlagsStrings());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    flags = list_of("EdgeReplicationSupported")
        .convert_to_container<vector<string> >();
    EXPECT_EQ(flags, pmsi_spec.GetTunnelFlagsStrings());
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
    pmsi_spec.tunnel_flags |= PmsiTunnelSpec::EdgeReplicationSupported;
    flags = list_of("LeafInfoRequired")("EdgeReplicationSupported")
        .convert_to_container<vector<string> >();
    EXPECT_EQ(flags, pmsi_spec.GetTunnelFlagsStrings());
}

TEST_F(BgpAttrTest, PmsiTunnel2) {
    ExtCommunitySpec spec;
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));

    EXPECT_EQ(4, pmsi_spec.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec.flags);
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported, pmsi_spec.tunnel_flags);
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication, pmsi_spec.tunnel_type);
    EXPECT_EQ(10000, pmsi_spec.GetLabel(&ext));
    EXPECT_EQ("10.1.1.1", pmsi_spec.GetIdentifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel3) {
    ExtCommunitySpec spec;
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));

    PmsiTunnelSpec pmsi_spec2(pmsi_spec1);
    EXPECT_EQ(4, pmsi_spec2.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec2.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec2.flags);
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported,
        pmsi_spec2.tunnel_flags);
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication, pmsi_spec2.tunnel_type);
    EXPECT_EQ(10000, pmsi_spec2.GetLabel(&ext));
    EXPECT_EQ("10.1.1.1", pmsi_spec2.GetIdentifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel4a) {
    ExtCommunitySpec spec;
    ExtCommunity ext(extcomm_db_, spec);
    BgpAttrSpec attr_spec;
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec.push_back(&pmsi_spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());

    const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported,
        pmsi_tunnel->tunnel_flags());
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
        pmsi_tunnel->tunnel_type());
    EXPECT_EQ(10000, pmsi_tunnel->GetLabel(&ext));
    EXPECT_EQ("10.1.1.1", pmsi_tunnel->identifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel4b) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    spec.communities.push_back(tun_encap.GetExtCommunityValue());
    ExtCommunity ext(extcomm_db_, spec);
    BgpAttrSpec attr_spec;
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(EvpnPrefix::kMaxVni, &ext);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec.push_back(&pmsi_spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());

    const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported,
        pmsi_tunnel->tunnel_flags());
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
        pmsi_tunnel->tunnel_type());
    EXPECT_EQ(EvpnPrefix::kMaxVni, pmsi_tunnel->GetLabel(&ext));
    EXPECT_EQ("10.1.1.1", pmsi_tunnel->identifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel5) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    BgpAttrSpec attr_spec;
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec.push_back(&pmsi_spec);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec);
    BgpAttr attr2(*attr1);
    EXPECT_EQ(attr1->pmsi_tunnel(), attr2.pmsi_tunnel());
}

TEST_F(BgpAttrTest, PmsiTunnel6) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    BgpAttrSpec attr_spec1;
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec1.push_back(&pmsi_spec1);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    pmsi_spec2.SetIdentifier(Ip4Address::from_string("10.1.1.2", ec));
    attr_spec2.push_back(&pmsi_spec2);
    BgpAttrPtr attr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(attr1, attr2);
    EXPECT_NE(0, attr1->CompareTo(*attr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel7) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    BgpAttrSpec attr_spec1;
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec1.push_back(&pmsi_spec1);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    attr_spec2.push_back(&pmsi_spec2);
    BgpAttrPtr attr2 = attr_db_->Locate(attr_spec2);

    EXPECT_EQ(attr1, attr2);
    EXPECT_EQ(0, attr1->CompareTo(*attr2));
    EXPECT_EQ(1, attr_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel8) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    PmsiTunnelPtr pmsi_tunnel1 = pmsi_tunnel_db_->Locate(pmsi_spec1);
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    PmsiTunnelPtr pmsi_tunnel2 = pmsi_tunnel_db_->Locate(pmsi_spec2);

    EXPECT_EQ(pmsi_tunnel1, pmsi_tunnel2);
    EXPECT_EQ(0, pmsi_tunnel1->CompareTo(*pmsi_tunnel2));
    EXPECT_EQ(1, pmsi_tunnel_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel9a) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    PmsiTunnelPtr pmsi_tunnel1 = pmsi_tunnel_db_->Locate(pmsi_spec1);
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    pmsi_spec2.tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
    PmsiTunnelPtr pmsi_tunnel2 = pmsi_tunnel_db_->Locate(pmsi_spec2);

    EXPECT_NE(pmsi_tunnel1, pmsi_tunnel2);
    EXPECT_NE(0, pmsi_tunnel1->CompareTo(*pmsi_tunnel2));
    EXPECT_EQ(2, pmsi_tunnel_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel9b) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    PmsiTunnelPtr pmsi_tunnel1 = pmsi_tunnel_db_->Locate(pmsi_spec1);
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    pmsi_spec2.tunnel_type = PmsiTunnelSpec::AssistedReplicationContrail;
    PmsiTunnelPtr pmsi_tunnel2 = pmsi_tunnel_db_->Locate(pmsi_spec2);

    EXPECT_NE(pmsi_tunnel1, pmsi_tunnel2);
    EXPECT_NE(0, pmsi_tunnel1->CompareTo(*pmsi_tunnel2));
    EXPECT_EQ(2, pmsi_tunnel_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel9c) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    PmsiTunnelPtr pmsi_tunnel1 = pmsi_tunnel_db_->Locate(pmsi_spec1);
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    pmsi_spec2.SetLabel(20000, &ext);
    PmsiTunnelPtr pmsi_tunnel2 = pmsi_tunnel_db_->Locate(pmsi_spec2);

    EXPECT_NE(pmsi_tunnel1, pmsi_tunnel2);
    EXPECT_NE(0, pmsi_tunnel1->CompareTo(*pmsi_tunnel2));
    EXPECT_EQ(2, pmsi_tunnel_db_->Size());
}

TEST_F(BgpAttrTest, PmsiTunnel9d) {
    ExtCommunitySpec spec;
    TunnelEncap tun_encap(TunnelEncapType::VXLAN);
    ExtCommunity ext(extcomm_db_, spec);
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000, &ext);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    PmsiTunnelPtr pmsi_tunnel1 = pmsi_tunnel_db_->Locate(pmsi_spec1);
    PmsiTunnelSpec pmsi_spec2 = pmsi_spec1;
    pmsi_spec2.SetIdentifier(Ip4Address::from_string("10.1.1.2", ec));
    PmsiTunnelPtr pmsi_tunnel2 = pmsi_tunnel_db_->Locate(pmsi_spec2);

    EXPECT_NE(pmsi_tunnel1, pmsi_tunnel2);
    EXPECT_NE(0, pmsi_tunnel1->CompareTo(*pmsi_tunnel2));
    EXPECT_EQ(2, pmsi_tunnel_db_->Size());
}

TEST_F(BgpAttrTest, EdgeDiscovery1) {
    EdgeDiscoverySpec edspec;
    EXPECT_EQ(0, edspec.edge_list.size());
}

TEST_F(BgpAttrTest, EdgeDiscovery2) {
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec.edge_list.push_back(edge);
    }

    EXPECT_EQ(2, edspec.edge_list.size());
    int idx = 1;
    for (EdgeDiscoverySpec::EdgeList::const_iterator it =
         edspec.edge_list.begin(); it != edspec.edge_list.end(); ++it, ++idx) {
        const EdgeDiscoverySpec::Edge *edge = *it;
        std::string addr_str = "10.1.1." + integerToString(idx);
        uint32_t first_label, last_label;
        edge->GetLabels(&first_label, &last_label);
        error_code ec;
        EXPECT_EQ(addr_str, edge->GetIp4Address().to_string(ec));
        EXPECT_EQ(1000 * idx, first_label);
        EXPECT_EQ(1000 * idx + 999, last_label);
    }
}

TEST_F(BgpAttrTest, EdgeDiscovery3) {
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }

    EdgeDiscoverySpec edspec2(edspec1);
    EXPECT_EQ(2, edspec2.edge_list.size());
    int idx = 1;
    for (EdgeDiscoverySpec::EdgeList::const_iterator it =
         edspec2.edge_list.begin(); it != edspec2.edge_list.end();
         ++it, ++idx) {
        const EdgeDiscoverySpec::Edge *edge = *it;
        std::string addr_str = "10.1.1." + integerToString(idx);
        uint32_t first_label, last_label;
        edge->GetLabels(&first_label, &last_label);
        error_code ec;
        EXPECT_EQ(addr_str, edge->GetIp4Address().to_string(ec));
        EXPECT_EQ(1000 * idx, first_label);
        EXPECT_EQ(1000 * idx + 999, last_label);
    }
}

TEST_F(BgpAttrTest, EdgeDiscovery4) {
    BgpAttrSpec attr_spec;
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&edspec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());

    const EdgeDiscovery *ediscovery = attr->edge_discovery();
    EXPECT_EQ(2, ediscovery->edge_list.size());
    int idx = 1;
    for (EdgeDiscovery::EdgeList::const_iterator it =
         ediscovery->edge_list.begin(); it != ediscovery->edge_list.end();
         ++it, ++idx) {
        const EdgeDiscovery::Edge *edge = *it;
        std::string addr_str = "10.1.1." + integerToString(idx);
        error_code ec;
        EXPECT_EQ(addr_str, edge->address.to_string(ec));
        EXPECT_EQ(1000 * idx, edge->label_block->first());
        EXPECT_EQ(1000 * idx + 999, edge->label_block->last());
    }
}

TEST_F(BgpAttrTest, EdgeDiscovery5) {
    BgpAttrSpec attr_spec;
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&edspec);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec);
    BgpAttr attr2(*attr1);
    EXPECT_EQ(attr1->edge_discovery(), attr2.edge_discovery());
}

TEST_F(BgpAttrTest, EdgeDiscovery6) {
    BgpAttrSpec attr_spec1;
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }
    attr_spec1.push_back(&edspec1);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    EdgeDiscoverySpec edspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "20.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec2.edge_list.push_back(edge);
    }
    attr_spec2.push_back(&edspec2);
    BgpAttrPtr attr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(attr1, attr2);
    EXPECT_NE(0, attr1->CompareTo(*attr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, EdgeDiscovery7a) {
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec.edge_list.push_back(edge);
    }
    EdgeDiscoveryPtr ediscovery1 = edge_discovery_db_->Locate(edspec);
    EdgeDiscoveryPtr ediscovery2 = edge_discovery_db_->Locate(edspec);
    EXPECT_EQ(1, edge_discovery_db_->Size());
    EXPECT_EQ(ediscovery1, ediscovery2);
}

TEST_F(BgpAttrTest, EdgeDiscovery7b) {
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }
    EdgeDiscoverySpec edspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec2.edge_list.push_back(edge);
    }

    EdgeDiscoveryPtr ediscovery1 = edge_discovery_db_->Locate(edspec1);
    EdgeDiscoveryPtr ediscovery2 = edge_discovery_db_->Locate(edspec2);
    EXPECT_EQ(1, edge_discovery_db_->Size());
    EXPECT_EQ(ediscovery1, ediscovery2);
}

TEST_F(BgpAttrTest, EdgeDiscovery7c) {
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }
    EdgeDiscoverySpec edspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(3 - idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * (3 - idx), 1000 * (3 - idx) + 999);
        edspec2.edge_list.push_back(edge);
    }

    EdgeDiscoveryPtr ediscovery1 = edge_discovery_db_->Locate(edspec1);
    EdgeDiscoveryPtr ediscovery2 = edge_discovery_db_->Locate(edspec2);
    EXPECT_EQ(1, edge_discovery_db_->Size());
    EXPECT_EQ(ediscovery1, ediscovery2);
}

TEST_F(BgpAttrTest, EdgeDiscovery8a) {
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }
    EdgeDiscoverySpec edspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec2.edge_list.push_back(edge);
    }

    EdgeDiscoveryPtr ediscovery1 = edge_discovery_db_->Locate(edspec1);
    EdgeDiscoveryPtr ediscovery2 = edge_discovery_db_->Locate(edspec2);
    EXPECT_EQ(2, edge_discovery_db_->Size());
    EXPECT_NE(ediscovery1, ediscovery2);
}

TEST_F(BgpAttrTest, EdgeDiscovery8b) {
    EdgeDiscoverySpec edspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec1.edge_list.push_back(edge);
    }
    EdgeDiscoverySpec edspec2;
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec2.edge_list.push_back(edge);
    }

    EdgeDiscoveryPtr ediscovery1 = edge_discovery_db_->Locate(edspec1);
    EdgeDiscoveryPtr ediscovery2 = edge_discovery_db_->Locate(edspec2);
    EXPECT_EQ(2, edge_discovery_db_->Size());
    EXPECT_NE(ediscovery1, ediscovery2);
}

TEST_F(BgpAttrTest, EdgeDiscoveryCompareTo) {
    EdgeDiscoverySpec edspec1;
    EdgeDiscoverySpec edspec2;
    EXPECT_EQ(0, edspec1.CompareTo(edspec1));
    EXPECT_EQ(0, edspec1.CompareTo(edspec2));
    EXPECT_EQ(0, edspec2.CompareTo(edspec1));
}

TEST_F(BgpAttrTest, EdgeDiscoveryToString1) {
    EdgeDiscoverySpec edspec;
    EXPECT_EQ("EdgeDiscovery <code: 241, flags: 0xc0>", edspec.ToString());
}

TEST_F(BgpAttrTest, EdgeDiscoveryToString2) {
    EdgeDiscoverySpec edspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        EdgeDiscoverySpec::Edge *edge = new(EdgeDiscoverySpec::Edge);
        std::string addr_str = "10.1.1." + integerToString(idx);
        edge->SetIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->SetLabels(1000 * idx, 1000 * idx + 999);
        edspec.edge_list.push_back(edge);
    }
    EXPECT_EQ("EdgeDiscovery <code: 241, flags: 0xc0>"
              " Edge[0] = (10.1.1.1, 1000-1999)"
              " Edge[1] = (10.1.1.2, 2000-2999)",
        edspec.ToString());
}

TEST_F(BgpAttrTest, BgpOList1a) {
    BgpOListSpec olist_spec(BgpAttribute::OList);
    EXPECT_EQ(BgpAttribute::OList, olist_spec.subcode);
    EXPECT_EQ(0, olist_spec.elements.size());
}

TEST_F(BgpAttrTest, BgpOList1b) {
    BgpOListSpec leaf_olist_spec(BgpAttribute::LeafOList);
    EXPECT_EQ(BgpAttribute::LeafOList, leaf_olist_spec.subcode);
    EXPECT_EQ(0, leaf_olist_spec.elements.size());
}

TEST_F(BgpAttrTest, BgpOList2a) {
    BgpOListSpec olist_spec(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec);
    EXPECT_EQ(1, olist_db_->Size());
    EXPECT_EQ(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList2b) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(1, olist_db_->Size());
    EXPECT_EQ(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList2c) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("udp")("gre");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(1, olist_db_->Size());
    EXPECT_EQ(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList2d) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(3 - idx);
        std::vector<std::string> encap = list_of("udp")("gre");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * (3 - idx), encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(1, olist_db_->Size());
    EXPECT_EQ(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList3a) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::LeafOList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList3b) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.2." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList3c) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 2000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList3d) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp-contrail");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList4a) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList4b) {
    BgpOListSpec olist_spec1(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec1.elements.push_back(elem);
    }
    BgpOListSpec olist_spec2(BgpAttribute::OList);
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec2.elements.push_back(elem);
    }
    BgpOListPtr olist1 = olist_db_->Locate(olist_spec1);
    BgpOListPtr olist2 = olist_db_->Locate(olist_spec2);
    EXPECT_EQ(2, olist_db_->Size());
    EXPECT_NE(olist1, olist2);
}

TEST_F(BgpAttrTest, BgpOList5) {
    BgpOListSpec olist_spec(BgpAttribute::OList);
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        std::vector<std::string> encap = list_of("gre")("udp");
        BgpOListElem elem(
            Ip4Address::from_string(addr_str, ec), 1000 * idx, encap);
        olist_spec.elements.push_back(elem);
    }
    EXPECT_EQ("OList <subcode: 1>"
              "OList[0] = (address: 10.1.1.1, label: 1000, encap-list: gre udp)"
              "OList[1] = (address: 10.1.1.2, label: 2000, encap-list: gre udp)",
               olist_spec.ToString());
}

TEST_F(BgpAttrTest, BgpOList6) {
    BgpOListSpec olist_spec1;
    BgpOListSpec olist_spec2;
    EXPECT_EQ(0, olist_spec1.CompareTo(olist_spec1));
    EXPECT_EQ(0, olist_spec1.CompareTo(olist_spec2));
    EXPECT_EQ(0, olist_spec2.CompareTo(olist_spec1));
}

TEST_F(BgpAttrTest, EdgeForwarding1) {
    EdgeForwardingSpec efspec;
    EXPECT_EQ(0, efspec.edge_list.size());
}

TEST_F(BgpAttrTest, EdgeForwarding2) {
    EdgeForwardingSpec efspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec.edge_list.push_back(edge);
    }

    EXPECT_EQ(2, efspec.edge_list.size());
    int idx = 1;
    for (EdgeForwardingSpec::EdgeList::const_iterator it =
         efspec.edge_list.begin(); it != efspec.edge_list.end(); ++it, ++idx) {
        const EdgeForwardingSpec::Edge *edge = *it;
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EXPECT_EQ("10.1.1.100", edge->GetInboundIp4Address().to_string(ec));
        EXPECT_EQ(100000, edge->inbound_label);
        EXPECT_EQ(addr_str, edge->GetOutboundIp4Address().to_string(ec));
        EXPECT_EQ(1000 * idx, edge->outbound_label);
    }
}

TEST_F(BgpAttrTest, EdgeForwarding3) {
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }

    EdgeForwardingSpec efspec2(efspec1);
    EXPECT_EQ(2, efspec2.edge_list.size());
    int idx = 1;
    for (EdgeForwardingSpec::EdgeList::const_iterator it =
         efspec2.edge_list.begin(); it != efspec2.edge_list.end();
         ++it, ++idx) {
        const EdgeForwardingSpec::Edge *edge = *it;
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EXPECT_EQ("10.1.1.100", edge->GetInboundIp4Address().to_string(ec));
        EXPECT_EQ(100000, edge->inbound_label);
        EXPECT_EQ(addr_str, edge->GetOutboundIp4Address().to_string(ec));
        EXPECT_EQ(1000 * idx, edge->outbound_label);
    }
}

TEST_F(BgpAttrTest, EdgeForwarding4) {
    BgpAttrSpec attr_spec;
    EdgeForwardingSpec efspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&efspec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());

    const EdgeForwarding *eforwarding = attr->edge_forwarding();
    EXPECT_EQ(2, eforwarding->edge_list.size());
    int idx = 1;
    for (EdgeForwarding::EdgeList::const_iterator it =
         eforwarding->edge_list.begin(); it != eforwarding->edge_list.end();
         ++it, ++idx) {
        const EdgeForwarding::Edge *edge = *it;
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EXPECT_EQ("10.1.1.100", edge->inbound_address.to_string(ec));
        EXPECT_EQ(100000, edge->inbound_label);
        EXPECT_EQ(addr_str, edge->outbound_address.to_string(ec));
        EXPECT_EQ(1000 * idx, edge->outbound_label);
    }
}

TEST_F(BgpAttrTest, EdgeForwarding5) {
    BgpAttrSpec attr_spec;
    EdgeForwardingSpec efspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec.edge_list.push_back(edge);
    }
    attr_spec.push_back(&efspec);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec);
    BgpAttr attr2(*attr1);
    EXPECT_EQ(attr1->edge_forwarding(), attr2.edge_forwarding());
}

TEST_F(BgpAttrTest, EdgeForwarding6) {
    BgpAttrSpec attr_spec1;
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }
    attr_spec1.push_back(&efspec1);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec1);

    BgpAttrSpec attr_spec2;
    EdgeForwardingSpec efspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "20.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("20.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec2.edge_list.push_back(edge);
    }
    attr_spec2.push_back(&efspec2);
    BgpAttrPtr attr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(attr1, attr2);
    EXPECT_NE(0, attr1->CompareTo(*attr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, EdgeForwarding7a) {
    EdgeForwardingSpec efspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding1 = edge_forwarding_db_->Locate(efspec);
    EdgeForwardingPtr eforwarding2 = edge_forwarding_db_->Locate(efspec);

    EXPECT_EQ(1, edge_forwarding_db_->Size());
    EXPECT_EQ(eforwarding1, eforwarding2);
}

TEST_F(BgpAttrTest, EdgeForwarding7b) {
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding1 = edge_forwarding_db_->Locate(efspec1);

    EdgeForwardingSpec efspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec2.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding2 = edge_forwarding_db_->Locate(efspec2);

    EXPECT_EQ(1, edge_forwarding_db_->Size());
    EXPECT_EQ(eforwarding1, eforwarding2);
}

TEST_F(BgpAttrTest, EdgeForwarding7c) {
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding1 = edge_forwarding_db_->Locate(efspec1);

    EdgeForwardingSpec efspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(3 - idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * (3 - idx);
        efspec2.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding2 = edge_forwarding_db_->Locate(efspec2);

    EXPECT_EQ(1, edge_forwarding_db_->Size());
    EXPECT_EQ(eforwarding1, eforwarding2);
}

TEST_F(BgpAttrTest, EdgeForwarding8a) {
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding1 = edge_forwarding_db_->Locate(efspec1);

    EdgeForwardingSpec efspec2;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec2.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding2 = edge_forwarding_db_->Locate(efspec2);

    EXPECT_EQ(2, edge_forwarding_db_->Size());
    EXPECT_NE(eforwarding1, eforwarding2);
}

TEST_F(BgpAttrTest, EdgeForwarding8b) {
    EdgeForwardingSpec efspec1;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec1.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding1 = edge_forwarding_db_->Locate(efspec1);

    EdgeForwardingSpec efspec2;
    for (int idx = 1; idx < 4; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec2.edge_list.push_back(edge);
    }
    EdgeForwardingPtr eforwarding2 = edge_forwarding_db_->Locate(efspec2);

    EXPECT_EQ(2, edge_forwarding_db_->Size());
    EXPECT_NE(eforwarding1, eforwarding2);
}

TEST_F(BgpAttrTest, EdgeForwardingCompareTo) {
    EdgeForwardingSpec efspec1;
    EdgeForwardingSpec efspec2;
    EXPECT_EQ(0, efspec1.CompareTo(efspec1));
    EXPECT_EQ(0, efspec1.CompareTo(efspec2));
    EXPECT_EQ(0, efspec2.CompareTo(efspec1));
}

TEST_F(BgpAttrTest, EdgeForwardingToString1) {
    EdgeForwardingSpec efspec;
    EXPECT_EQ("EdgeForwarding <code: 242, flags: 0xc0>", efspec.ToString());
}

TEST_F(BgpAttrTest, EdgeForwardingToString2) {
    EdgeForwardingSpec efspec;
    for (int idx = 1; idx < 3; ++idx) {
        error_code ec;
        std::string addr_str = "10.1.1." + integerToString(idx);
        EdgeForwardingSpec::Edge *edge = new(EdgeForwardingSpec::Edge);
        edge->SetInboundIp4Address(Ip4Address::from_string("10.1.1.100", ec));
        edge->inbound_label = 100000;
        edge->SetOutboundIp4Address(Ip4Address::from_string(addr_str, ec));
        edge->outbound_label = 1000 * idx;
        efspec.edge_list.push_back(edge);
    }
    EXPECT_EQ("EdgeForwarding <code: 242, flags: 0xc0>"
              " Edge[0] = (InAddress=10.1.1.100, InLabel=100000,"
              " OutAddress=10.1.1.1, OutLabel=1000)"
              " Edge[1] = (InAddress=10.1.1.100, InLabel=100000,"
              " OutAddress=10.1.1.2, OutLabel=2000)",
        efspec.ToString());
}

TEST_F(BgpAttrTest, BgpAttrDB) {
    BgpAttrSpec spec;
    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    spec.push_back(origin);

    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    spec.push_back(nexthop);

    BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
    spec.push_back(aa);

    BgpAttrAggregator *agg = new BgpAttrAggregator(0xface, 0xcafebabe);
    spec.push_back(agg);

    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(21);
    ps->path_segment.push_back(22);
    path_spec->path_segments.push_back(ps);
    spec.push_back(path_spec);

    CommunitySpec *community = new CommunitySpec;
    community->communities.push_back(0x87654321);
    spec.push_back(community);

    ExtCommunitySpec *ext_community = new ExtCommunitySpec;
    ext_community->communities.push_back(0x1020304050607080);
    spec.push_back(ext_community);

    OriginVnPathSpec *ovnpath = new OriginVnPathSpec;
    ovnpath->origin_vns.push_back(0x1020304050607080);
    spec.push_back(ovnpath);

    BgpAttrPtr ptr1 = attr_db_->Locate(spec);
    BgpAttrPtr ptr2 = attr_db_->Locate(spec);

    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());
    EXPECT_EQ(1, ovnpath_db_->Size());

    agg->address = 0xcafed00d;
    BgpAttrPtr ptr3 = attr_db_->Locate(spec);

    EXPECT_EQ(2, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());
    EXPECT_EQ(1, ovnpath_db_->Size());

    ptr1.reset();
    EXPECT_EQ(2, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());
    EXPECT_EQ(1, ovnpath_db_->Size());

    ptr2.reset();
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());
    EXPECT_EQ(1, ovnpath_db_->Size());

    ptr3.reset();
    EXPECT_EQ(0, attr_db_->Size());
    EXPECT_EQ(0, aspath_db_->Size());
    EXPECT_EQ(0, comm_db_->Size());
    EXPECT_EQ(0, extcomm_db_->Size());
    EXPECT_EQ(0, ovnpath_db_->Size());

    ptr1 = attr_db_->Locate(spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());
    EXPECT_EQ(1, ovnpath_db_->Size());

    STLDeleteValues(&spec);
}

// ----- Test multi-threaded issues in path attributes db.
// Launch a number of threads, that add and delete the same attribute content.
// Since many threads are launched, we get to uncover most of the concurrency
// issues during data base update, if any.

template <class Type, class TypeDB, class TypeSpec>
class AttributeMock : public Type {
public:
    AttributeMock(TypeDB *db, const TypeSpec &spec) : Type(db, spec) { }

    virtual void Remove() {
        // Inject artificial delay to detect concurrency issues, if any.
        usleep(10000);
        Type::Remove();
    }
};

template <class Type, class TypePtr, class TypeDB, class TypeSpec>
static void *ConcurrencyThreadRun(void *objp) {
    TypeDB *db = reinterpret_cast<TypeDB *>(objp);

    TypePtr ptr = db->Locate(new AttributeMock<Type, TypeDB, TypeSpec>(
                                  db, TypeSpec()));
    EXPECT_EQ(1, db->Size());
    return NULL;
}

template <class Type, class TypePtr, class TypeDB, class TypeSpec>
static void ConcurrencyTest(TypeDB *db) {
    std::vector<pthread_t> thread_ids;
    pthread_t tid;

    int thread_count = 1024;
    char *str = getenv("THREAD_COUNT");
    if (str) thread_count = strtoul(str, NULL, 0);

    for (int i = 0; i < thread_count; i++) {
        if (!pthread_create(&tid, NULL,
                            &ConcurrencyThreadRun<Type, TypePtr, TypeDB,
                                                  TypeSpec>,
                            db)) {
            thread_ids.push_back(tid);
        }
    }

    BOOST_FOREACH(tid, thread_ids) { pthread_join(tid, NULL); }
    TASK_UTIL_EXPECT_EQ(0, db->Size());
}

TEST_F(BgpAttrTest, BgpAttrDBConcurrency) {
    ConcurrencyTest<BgpAttr, BgpAttrPtr, BgpAttrDB, BgpAttrSpec>(attr_db_);
}

TEST_F(BgpAttrTest, AsPathDBConcurrency) {
    ConcurrencyTest<AsPath, AsPathPtr, AsPathDB, AsPathSpec>(aspath_db_);
}

TEST_F(BgpAttrTest, BgpOListDBConcurrency) {
    ConcurrencyTest<BgpOList, BgpOListPtr, BgpOListDB,
                    BgpOListSpec>(olist_db_);
}

TEST_F(BgpAttrTest, CommunityDBConcurrency) {
    ConcurrencyTest<Community, CommunityPtr, CommunityDB,
                    CommunitySpec>(comm_db_);
}

TEST_F(BgpAttrTest, ExtCommunityDBConcurrency) {
    ConcurrencyTest<ExtCommunity, ExtCommunityPtr, ExtCommunityDB,
                    ExtCommunitySpec>(extcomm_db_);
}

TEST_F(BgpAttrTest, OriginVnPathDBConcurrency) {
    ConcurrencyTest<OriginVnPath, OriginVnPathPtr, OriginVnPathDB,
                    OriginVnPathSpec>(ovnpath_db_);
}

TEST_F(BgpAttrTest, PmsiTunnelDBConcurrency) {
    ConcurrencyTest<PmsiTunnel, PmsiTunnelPtr, PmsiTunnelDB,
                    PmsiTunnelSpec>(pmsi_tunnel_db_);
}

TEST_F(BgpAttrTest, EdgeDiscoveryDBConcurrency) {
    ConcurrencyTest<EdgeDiscovery, EdgeDiscoveryPtr, EdgeDiscoveryDB,
                    EdgeDiscoverySpec>(edge_discovery_db_);
}

TEST_F(BgpAttrTest, EdgeForwardingDBConcurrency) {
    ConcurrencyTest<EdgeForwarding, EdgeForwardingPtr, EdgeForwardingDB,
                    EdgeForwardingSpec>(edge_forwarding_db_);
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
