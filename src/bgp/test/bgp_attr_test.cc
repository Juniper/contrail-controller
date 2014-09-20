/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_attr.h"

#include <boost/foreach.hpp>
#include <pthread.h>
#include <sstream>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using boost::system::error_code;

class BgpAttrTest : public ::testing::Test {
protected:
    BgpAttrTest()
        : server_(&evm_),
          attr_db_(server_.attr_db()),
          aspath_db_(server_.aspath_db()),
          comm_db_(server_.comm_db()),
          extcomm_db_(server_.extcomm_db()) {
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
    AsPathDB *aspath_db_;
    CommunityDB *comm_db_;
    ExtCommunityDB *extcomm_db_;
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

    // Add to an empty path
    AsPathSpec *p2 = path.Add(1000);
    EXPECT_EQ(1, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    // populate the path with some entries
    for (int i = 0; i < 10; i++) {
        AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
        ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        for (int j = 0; j < 10; j++) {
            ps->path_segment.push_back((i<<4) + j);
        }
        path.path_segments.push_back(ps);
    }

    p2 = path.Add(1000);
    EXPECT_EQ(10, p2->path_segments.size());
    EXPECT_EQ(11, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    path.path_segments[0]->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    p2 = path.Add(1000);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

    path.path_segments[0]->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    for (int j = 11; j < 256; j++) {
        path.path_segments[0]->path_segment.push_back(j);
    }

    p2 = path.Add(1000);
    EXPECT_EQ(11, p2->path_segments.size());
    EXPECT_EQ(1, p2->path_segments[0]->path_segment.size());
    EXPECT_EQ(1000, p2->path_segments[0]->path_segment[0]);
    delete p2;

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
    extcomm1.Append(list);

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
    extcomm1.Append(list);

    ExtCommunitySpec spec2;
    for (int idx = 1; idx < 5; idx++)
        spec2.communities.push_back(100 * idx);
    ExtCommunity extcomm2(extcomm_db_, spec2);

    EXPECT_EQ(0, extcomm1.CompareTo(extcomm2));
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

TEST_F(BgpAttrTest, Params1) {
    BgpAttrSpec attr_spec;
    uint64_t params = BgpAttrParams::EdgeReplicationNotSupported;
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
    uint64_t params2 = BgpAttrParams::EdgeReplicationNotSupported;
    BgpAttrParams params_spec2(params2);
    attr_spec2.push_back(&params_spec2);
    BgpAttrPtr ptr2 = attr_db_->Locate(attr_spec2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_NE(0, ptr1->CompareTo(*ptr2));
    EXPECT_EQ(2, attr_db_->Size());
}

TEST_F(BgpAttrTest, BgpAttrParamsToString) {
    uint64_t params = BgpAttrParams::EdgeReplicationNotSupported;
    BgpAttrParams params_spec(params);
    EXPECT_EQ("Params <subcode: 5> : 0x0000000000000001",
        params_spec.ToString());
}

TEST_F(BgpAttrTest, PmsiTunnel1) {
    PmsiTunnelSpec pmsi_spec;
    EXPECT_EQ(0, pmsi_spec.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec.flags);
}

TEST_F(BgpAttrTest, PmsiTunnel2) {
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));

    EXPECT_EQ(4, pmsi_spec.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec.flags);
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported, pmsi_spec.tunnel_flags);
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication, pmsi_spec.tunnel_type);
    EXPECT_EQ(10000, pmsi_spec.GetLabel());
    EXPECT_EQ("10.1.1.1", pmsi_spec.GetIdentifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel3) {
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000);
    error_code ec;
    pmsi_spec1.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));

    PmsiTunnelSpec pmsi_spec2(pmsi_spec1);
    EXPECT_EQ(4, pmsi_spec2.identifier.size());
    EXPECT_EQ(BgpAttribute::PmsiTunnel, pmsi_spec2.code);
    EXPECT_EQ(BgpAttribute::Optional | BgpAttribute::Transitive,
        pmsi_spec2.flags);
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported, pmsi_spec2.tunnel_flags);
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication, pmsi_spec2.tunnel_type);
    EXPECT_EQ(10000, pmsi_spec2.GetLabel());
    EXPECT_EQ("10.1.1.1", pmsi_spec2.GetIdentifier().to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel4) {
    BgpAttrSpec attr_spec;
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec.push_back(&pmsi_spec);
    BgpAttrPtr attr = attr_db_->Locate(attr_spec);
    EXPECT_EQ(1, attr_db_->Size());

    const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
    EXPECT_EQ(PmsiTunnelSpec::EdgeReplicationSupported,
        pmsi_tunnel->tunnel_flags);
    EXPECT_EQ(PmsiTunnelSpec::IngressReplication,
        pmsi_tunnel->tunnel_type);
    EXPECT_EQ(10000, pmsi_tunnel->label);
    EXPECT_EQ("10.1.1.1", pmsi_tunnel->identifier.to_string());
}

TEST_F(BgpAttrTest, PmsiTunnel5) {
    BgpAttrSpec attr_spec;
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    attr_spec.push_back(&pmsi_spec);
    BgpAttrPtr attr1 = attr_db_->Locate(attr_spec);
    BgpAttr attr2(*attr1);
    EXPECT_EQ(attr1->pmsi_tunnel(), attr2.pmsi_tunnel());
}

TEST_F(BgpAttrTest, PmsiTunnel6) {
    BgpAttrSpec attr_spec1;
    PmsiTunnelSpec pmsi_spec1;
    pmsi_spec1.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec1.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec1.SetLabel(10000);
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

TEST_F(BgpAttrTest, PmsiTunnelSpecToString1) {
    PmsiTunnelSpec pmsi_spec;
    EXPECT_EQ("PmsiTunnel <code: 22, flags: 0xc0>"
              " Tunnel Flags: 0x0 Tunnel Type: 0 Label: 0 Identifier: 0.0.0.0",
        pmsi_spec.ToString());
}

TEST_F(BgpAttrTest, PmsiTunnelSpecToString2) {
    PmsiTunnelSpec pmsi_spec;
    pmsi_spec.tunnel_flags = PmsiTunnelSpec::EdgeReplicationSupported;
    pmsi_spec.tunnel_type = PmsiTunnelSpec::IngressReplication;
    pmsi_spec.SetLabel(10000);
    error_code ec;
    pmsi_spec.SetIdentifier(Ip4Address::from_string("10.1.1.1", ec));
    EXPECT_EQ("PmsiTunnel <code: 22, flags: 0xc0>"
              " Tunnel Flags: 0x80 Tunnel Type: 6 Label: 10000"
              " Identifier: 10.1.1.1",
        pmsi_spec.ToString());
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
         edspec2.edge_list.begin(); it != edspec2.edge_list.end(); ++it, ++idx) {
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
         efspec2.edge_list.begin(); it != efspec2.edge_list.end(); ++it, ++idx) {
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

    BgpAttrPtr ptr1 = attr_db_->Locate(spec);
    BgpAttrPtr ptr2 = attr_db_->Locate(spec);

    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());

    agg->address = 0xcafed00d;
    BgpAttrPtr ptr3 = attr_db_->Locate(spec);

    EXPECT_EQ(2, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());

    ptr1.reset();
    EXPECT_EQ(2, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());

    ptr2.reset();
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());

    ptr3.reset();
    EXPECT_EQ(0, attr_db_->Size());
    EXPECT_EQ(0, aspath_db_->Size());
    EXPECT_EQ(0, comm_db_->Size());
    EXPECT_EQ(0, extcomm_db_->Size());

    ptr1 = attr_db_->Locate(spec);
    EXPECT_EQ(1, attr_db_->Size());
    EXPECT_EQ(1, aspath_db_->Size());
    EXPECT_EQ(1, comm_db_->Size());
    EXPECT_EQ(1, extcomm_db_->Size());

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

// Instantiate the template functions.
template void ConcurrencyTest<BgpAttr, BgpAttrPtr, BgpAttrDB, BgpAttrSpec>(
                  BgpAttrDB *);
template void ConcurrencyTest<AsPath, AsPathPtr, AsPathDB, AsPathSpec>(
                  AsPathDB *);
template void ConcurrencyTest<Community, CommunityPtr, CommunityDB,
                              CommunitySpec>(CommunityDB *);
template void ConcurrencyTest<ExtCommunity, ExtCommunityPtr, ExtCommunityDB,
                              ExtCommunitySpec>(ExtCommunityDB *);

TEST_F(BgpAttrTest, BgpAttrDBConcurrency) {
    ConcurrencyTest<BgpAttr, BgpAttrPtr, BgpAttrDB, BgpAttrSpec>(attr_db_);
}

TEST_F(BgpAttrTest, AsPathDBConcurrency) {
    ConcurrencyTest<AsPath, AsPathPtr, AsPathDB, AsPathSpec>(aspath_db_);
}

TEST_F(BgpAttrTest, CommunityDBConcurrency) {
    ConcurrencyTest<Community, CommunityPtr, CommunityDB,
                    CommunitySpec>(comm_db_);
}

TEST_F(BgpAttrTest, ExtCommunityDBConcurrency) {
    ConcurrencyTest<ExtCommunity, ExtCommunityPtr, ExtCommunityDB,
                    ExtCommunitySpec>(extcomm_db_);
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
