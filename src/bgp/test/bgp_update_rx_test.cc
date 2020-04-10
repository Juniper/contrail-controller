/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/assign/list_of.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"

using namespace std;
namespace ip = boost::asio::ip;

class BgpPeerMock : public BgpPeer {
public:
    BgpPeerMock(BgpServer *server, RoutingInstance *instance,
                const BgpNeighborConfig *config) :
        BgpPeer(server, instance, config) {
    }

    bool IsReady() const { return true; }
    void TriggerPrefixLimitCheck() const { }
};

class BgpUpdateRxTest : public ::testing::Test {
protected:
    BgpUpdateRxTest()
        : server_(&evm_),
          master_(NULL),
          peer_(NULL),
          rib1_(NULL), rib2_(NULL),
          tid1_(DBTableBase::kInvalidId), tid2_(DBTableBase::kInvalidId),
          route_origin_override_(false), route_origin_("IGP") {
        ConcurrencyScope scope("bgp::Config");
        BgpInstanceConfig instance_config(BgpConfigManager::kMasterInstance);
        boost::system::error_code ec;
        local_identifier_ = Ip4Address::from_string("10.1.1.1", ec);
        master_ = server_.routing_instance_mgr()->CreateRoutingInstance(
            &instance_config);
        rib1_ = master_->GetTable(Address::INET);
        rib2_ = master_->GetTable(Address::INETVPN);
        adc_notification_ = 0;
        del_notification_ = 0;
    }

    virtual void SetUp() {
        tid1_ = rib1_->Register(
            boost::bind(&BgpUpdateRxTest::TableListener, this, _1, _2));
        tid2_ = rib2_->Register(
            boost::bind(&BgpUpdateRxTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        rib1_->Unregister(tid1_);
        rib2_->Unregister(tid2_);
        rib1_->ManagedDelete();
        rib2_->ManagedDelete();
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        task_util::WaitForIdle();
    }

    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        bool del_notify = entry->IsDeleted();
        if (root->parent() == rib1_) BGP_DEBUG_UT("Inet table notification");
        if (root->parent() == rib2_) BGP_DEBUG_UT("InetVpn table notification");
        BGP_DEBUG_UT("Listener = " << del_notify << " " << entry->ToString());
        if (del_notify)
            del_notification_++;
        else
            adc_notification_++;
    }

    void CreatePeer(uint32_t local_as = 64512, uint32_t peer_as = 64512,
        uint8_t loop_count = 0) {
        ConcurrencyScope scope("bgp::Config");
        BgpNeighborConfig nbr_config;
        nbr_config.set_name("test-peer");
        nbr_config.set_instance_name(BgpConfigManager::kMasterInstance);
        nbr_config.set_local_identifier(htonl(local_identifier_.to_ulong()));
        nbr_config.set_local_as(local_as);
        nbr_config.set_peer_as(peer_as);
        nbr_config.set_loop_count(loop_count);
        nbr_config.set_cluster_id(local_identifier_.to_ulong());
        nbr_config.SetOriginOverride(route_origin_override_, route_origin_);

        BgpNeighborConfig::FamilyAttributesList family_attributes_list;
        BgpFamilyAttributesConfig family_attributes1("inet");
        family_attributes_list.push_back(family_attributes1);
        BgpFamilyAttributesConfig family_attributes2("inet-vpn");
        family_attributes_list.push_back(family_attributes2);
        nbr_config.set_family_attributes_list(family_attributes_list);
        peer_ = master_->peer_manager()->PeerLocate(&server_, &nbr_config);
    }

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;

    EventManager evm_;
    BgpServer server_;
    Ip4Address local_identifier_;
    RoutingInstance *master_;
    BgpPeer *peer_;

    BgpTable *rib1_;
    BgpTable *rib2_;

    DBTableBase::ListenerId tid1_;
    DBTableBase::ListenerId tid2_;

    bool route_origin_override_;
    std::string route_origin_;
};

TEST_F(BgpUpdateRxTest, AdvertiseWithdraw) {
    CreatePeer();
    adc_notification_ = 0;
    del_notification_ = 0;

    EXPECT_EQ(rib1_, server_.database()->FindTable("inet.0"));
    EXPECT_EQ(rib2_, server_.database()->FindTable("bgp.l3vpn.0"));

    BgpProto::OpenMessage open;
    uint8_t capc[] = {0, 1, 0, 128};
    BgpProto::OpenMessage::Capability *cap =
                new BgpProto::OpenMessage::Capability(
                                BgpProto::OpenMessage::Capability::MpExtension,
                    capc, 4);
    BgpProto::OpenMessage::OptParam *opt = new BgpProto::OpenMessage::OptParam;
    opt->capabilities.push_back(cap);
    open.opt_params.push_back(opt);
    peer_->SetCapabilities(&open);

    BgpProto::Update update;
    char p[] = {0x1, 0x2, 0x3};
    int  plen[] = {4, 10};

    BgpProtoPrefix *prefix = new BgpProtoPrefix;
    prefix->prefixlen = 9;
    prefix->prefix = vector<uint8_t>(p, p + 2);
    update.withdrawn_routes.push_back(prefix);
    prefix = new BgpProtoPrefix;
    prefix->prefixlen = 20;
    prefix->prefix = vector<uint8_t>(p, p + 3);
    update.withdrawn_routes.push_back(prefix);

    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);

    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);

    BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
    update.path_attributes.push_back(aa);

    BgpAttrAggregator *agg = new BgpAttrAggregator(0xface, 0xcafebabe);
    update.path_attributes.push_back(agg);

    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(21);
    ps->path_segment.push_back(22);
    path_spec->path_segments.push_back(ps);
    update.path_attributes.push_back(path_spec);

    BgpMpNlri *mp_nlri = new BgpMpNlri;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPReachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[12] = {0,0,0,0,0,0,0,0,192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[12]);
    mp_nlri->nlri.push_back(bpp);
    update.path_attributes.push_back(mp_nlri);

    vector<BgpProtoPrefix *> nlri_list;
    int i, repeat, nlri_count = 2;

    for (i = 0; i < nlri_count; i++) {
        nlri_list.push_back(new BgpProtoPrefix);
        nlri_list.back()->prefixlen = plen[i];
        nlri_list.back()->prefix = vector<uint8_t>(p, p + i + 1);
        update.nlri.push_back(nlri_list.back());
    }

    //
    // Send the same update multiple times and verify
    //
    for (repeat = 0; repeat < 3; repeat++) {
        peer_->ProcessUpdate(&update);
        task_util::WaitForIdle();
        EXPECT_EQ(nlri_count + 1, adc_notification_);

        for (i = 0; i < nlri_count; i++) {
            Ip4Prefix ip_prefix;
            assert(Ip4Prefix::FromProtoPrefix(*nlri_list[i], &ip_prefix) == 0);
            InetTable::RequestKey key(ip_prefix, peer_);
            BgpRoute *rt = static_cast<BgpRoute *>(rib1_->Find(&key));
            EXPECT_TRUE(rt != NULL);
            EXPECT_TRUE(rt->count() == 1);
        }
    }

    InetVpnTable::RequestKey key2(iv_prefix, peer_);
    Route *rt2 = static_cast<Route *>(rib2_->Find(&key2));
    EXPECT_TRUE(rt2 != NULL);

    {

    //
    // Now withdraw the prefixes and make sure that they get deleted from the
    // table. Send withdraws repeatedly to make sure that duplicate withdraws
    // are properly handled
    //
    BgpProto::Update withdraw;
    BgpMpNlri *mp_nlri = new BgpMpNlri;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPUnreachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    mp_nlri->nlri.push_back(bpp);
    withdraw.path_attributes.push_back(mp_nlri);

    vector<BgpProtoPrefix *> nlri_list;
    for (i = 0; i < nlri_count; i++) {
        nlri_list.push_back(new BgpProtoPrefix);
        nlri_list.back()->prefixlen = plen[i];
        nlri_list.back()->prefix = vector<uint8_t>(p, p + i + 1);
        withdraw.withdrawn_routes.push_back(nlri_list.back());
    }

    for (int repeat = 0; repeat < 3; repeat++) {
        peer_->ProcessUpdate(&withdraw);
        task_util::WaitForIdle();

        for (int i = 0; i < nlri_count; i++) {
            Ip4Prefix ip_prefix;
            assert(Ip4Prefix::FromProtoPrefix(*nlri_list[i], &ip_prefix) == 0);
            InetTable::RequestKey key(ip_prefix, peer_);
            Route *rt = static_cast<Route *>(rib1_->Find(&key));
            EXPECT_TRUE(rt == NULL);
        }

        InetVpnTable::RequestKey key2(iv_prefix, peer_);
        Route *rt2 = static_cast<Route *>(rib2_->Find(&key2));
        EXPECT_TRUE(rt2 == NULL);
    }

    }

    peer_->ResetCapabilities();
}

// Parameterize originator id to be same vs. different.
class BgpUpdateRxParamTest1:
    public BgpUpdateRxTest,
    public ::testing::WithParamInterface<bool> {
};

TEST_P(BgpUpdateRxParamTest1, OriginatorIdLoop) {
    CreatePeer();
    EXPECT_EQ(rib2_, server_.database()->FindTable("bgp.l3vpn.0"));

    BgpProto::OpenMessage open;
    uint8_t capc[] = {0, 1, 0, 128};
    BgpProto::OpenMessage::Capability *cap =
        new BgpProto::OpenMessage::Capability(
            BgpProto::OpenMessage::Capability::MpExtension, capc, 4);
    BgpProto::OpenMessage::OptParam *opt = new BgpProto::OpenMessage::OptParam;
    opt->capabilities.push_back(cap);
    open.opt_params.push_back(opt);
    peer_->SetCapabilities(&open);

    // Advertise the prefix.
    BgpProto::Update update;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);
    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);
    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    path_spec->path_segments.push_back(ps);
    update.path_attributes.push_back(path_spec);
    uint32_t identifier = local_identifier_.to_ulong();
    BgpAttrOriginatorId *originator_id =
        new BgpAttrOriginatorId(GetParam() ? identifier : identifier + 1);
    update.path_attributes.push_back(originator_id);

    BgpMpNlri *mp_nlri = new BgpMpNlri;
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPReachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[12] = {0,0,0,0,0,0,0,0,192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[12]);
    mp_nlri->nlri.push_back(bpp);
    update.path_attributes.push_back(mp_nlri);

    peer_->ProcessUpdate(&update);
    task_util::WaitForIdle();
    InetVpnTable::RequestKey key(iv_prefix, peer_);
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) != NULL);
    BgpRoute *rt = static_cast<BgpRoute *>(rib2_->Find(&key));
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    const BgpPath *path = rt->BestPath();
    if (GetParam()) {
        EXPECT_TRUE((path->GetFlags() & BgpPath::OriginatorIdLooped) != 0);
    } else {
        EXPECT_TRUE((path->GetFlags() & BgpPath::OriginatorIdLooped) == 0);
    }

    // Withdraw the prefix.
    BgpProto::Update withdraw;
    BgpMpNlri *mp_nlri2 = new BgpMpNlri;
    BgpProtoPrefix *bpp2 = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp2);
    mp_nlri2->code = BgpAttribute::MPUnreachNlri;
    mp_nlri2->afi = 1;
    mp_nlri2->safi = 128;
    mp_nlri2->nlri.push_back(bpp2);
    withdraw.path_attributes.push_back(mp_nlri2);

    peer_->ProcessUpdate(&withdraw);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) == NULL);

    peer_->ResetCapabilities();
}

TEST_P(BgpUpdateRxParamTest1, ClusterListLoop) {
    CreatePeer();
    EXPECT_EQ(rib2_, server_.database()->FindTable("bgp.l3vpn.0"));

    BgpProto::OpenMessage open;
    uint8_t capc[] = {0, 1, 0, 128};
    BgpProto::OpenMessage::Capability *cap =
        new BgpProto::OpenMessage::Capability(
            BgpProto::OpenMessage::Capability::MpExtension, capc, 4);
    BgpProto::OpenMessage::OptParam *opt = new BgpProto::OpenMessage::OptParam;
    opt->capabilities.push_back(cap);
    open.opt_params.push_back(opt);
    peer_->SetCapabilities(&open);

    // Advertise the prefix.
    BgpProto::Update update;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);
    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);
    uint32_t cluster_id = local_identifier_.to_ulong();
    server_.set_cluster_id(cluster_id);
    ClusterListSpec *cluster_list_spec = new ClusterListSpec;
    cluster_list_spec->cluster_list.push_back(100);
    cluster_list_spec->cluster_list.push_back(
        GetParam() ? cluster_id : cluster_id + 1);
    cluster_list_spec->cluster_list.push_back(300);
    update.path_attributes.push_back(cluster_list_spec);

    BgpMpNlri *mp_nlri = new BgpMpNlri;
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPReachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[12] = {0,0,0,0,0,0,0,0,192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[12]);
    mp_nlri->nlri.push_back(bpp);
    update.path_attributes.push_back(mp_nlri);

    peer_->ProcessUpdate(&update);
    task_util::WaitForIdle();
    InetVpnTable::RequestKey key(iv_prefix, peer_);
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) != NULL);
    BgpRoute *rt = static_cast<BgpRoute *>(rib2_->Find(&key));
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    const BgpPath *path = rt->BestPath();
    if (GetParam()) {
        EXPECT_TRUE((path->GetFlags() & BgpPath::ClusterListLooped) != 0);
        EXPECT_FALSE(path->IsFeasible());
    } else {
        EXPECT_TRUE((path->GetFlags() & BgpPath::ClusterListLooped) == 0);
        EXPECT_TRUE(path->IsFeasible());
    }

    // Withdraw the prefix.
    BgpProto::Update withdraw;
    BgpMpNlri *mp_nlri2 = new BgpMpNlri;
    BgpProtoPrefix *bpp2 = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp2);
    mp_nlri2->code = BgpAttribute::MPUnreachNlri;
    mp_nlri2->afi = 1;
    mp_nlri2->safi = 128;
    mp_nlri2->nlri.push_back(bpp2);
    withdraw.path_attributes.push_back(mp_nlri2);

    peer_->ProcessUpdate(&withdraw);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) == NULL);

    peer_->ResetCapabilities();
}

// Parameterize loop count.
class BgpUpdateRxParamTest2:
    public BgpUpdateRxTest,
    public ::testing::WithParamInterface<uint8_t> {
};

TEST_P(BgpUpdateRxParamTest2, AsPathLoop) {
    CreatePeer(64512, 64513, GetParam());
    EXPECT_EQ(rib2_, server_.database()->FindTable("bgp.l3vpn.0"));

    BgpProto::OpenMessage open;
    uint8_t capc[] = {0, 1, 0, 128};
    BgpProto::OpenMessage::Capability *cap =
        new BgpProto::OpenMessage::Capability(
            BgpProto::OpenMessage::Capability::MpExtension, capc, 4);
    BgpProto::OpenMessage::OptParam *opt = new BgpProto::OpenMessage::OptParam;
    opt->capabilities.push_back(cap);
    open.opt_params.push_back(opt);
    peer_->SetCapabilities(&open);

    // Advertise the prefix.
    BgpProto::Update update;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);
    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);
    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(64512);
    ps->path_segment.push_back(64514);
    ps->path_segment.push_back(64512);
    path_spec->path_segments.push_back(ps);
    update.path_attributes.push_back(path_spec);

    BgpMpNlri *mp_nlri = new BgpMpNlri;
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPReachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[12] = {0,0,0,0,0,0,0,0,192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[12]);
    mp_nlri->nlri.push_back(bpp);
    update.path_attributes.push_back(mp_nlri);

    peer_->ProcessUpdate(&update);
    task_util::WaitForIdle();
    InetVpnTable::RequestKey key(iv_prefix, peer_);
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) != NULL);
    BgpRoute *rt = static_cast<BgpRoute *>(rib2_->Find(&key));
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    const BgpPath *path = rt->BestPath();
    if (GetParam() < 2) {
        EXPECT_TRUE((path->GetFlags() & BgpPath::AsPathLooped) != 0);
    } else {
        EXPECT_TRUE((path->GetFlags() & BgpPath::AsPathLooped) == 0);
    }

    // Withdraw the prefix.
    BgpProto::Update withdraw;
    BgpMpNlri *mp_nlri2 = new BgpMpNlri;
    BgpProtoPrefix *bpp2 = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp2);
    mp_nlri2->code = BgpAttribute::MPUnreachNlri;
    mp_nlri2->afi = 1;
    mp_nlri2->safi = 128;
    mp_nlri2->nlri.push_back(bpp2);
    withdraw.path_attributes.push_back(mp_nlri2);

    peer_->ProcessUpdate(&withdraw);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) == NULL);

    peer_->ResetCapabilities();
}

TEST_P(BgpUpdateRxParamTest1, RouteOriginOverride) {
    route_origin_override_ = true;
    if (GetParam()) {
        route_origin_ = "EGP";
    } else {
        route_origin_ = "IGP";
    }

    CreatePeer();
    EXPECT_EQ(rib2_, server_.database()->FindTable("bgp.l3vpn.0"));

    BgpProto::OpenMessage open;
    uint8_t capc[] = {0, 1, 0, 128};
    BgpProto::OpenMessage::Capability *cap =
        new BgpProto::OpenMessage::Capability(
            BgpProto::OpenMessage::Capability::MpExtension, capc, 4);
    BgpProto::OpenMessage::OptParam *opt = new BgpProto::OpenMessage::OptParam;
    opt->capabilities.push_back(cap);
    open.opt_params.push_back(opt);
    peer_->SetCapabilities(&open);

    // Advertise the prefix.
    BgpProto::Update update;
    InetVpnPrefix iv_prefix(InetVpnPrefix::FromString("2:20:192.168.24.0/24"));
    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    update.path_attributes.push_back(origin);
    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    update.path_attributes.push_back(nexthop);
    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    path_spec->path_segments.push_back(ps);
    update.path_attributes.push_back(path_spec);

    BgpMpNlri *mp_nlri = new BgpMpNlri;
    BgpProtoPrefix *bpp = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp);
    mp_nlri->code = BgpAttribute::MPReachNlri;
    mp_nlri->afi = 1;
    mp_nlri->safi = 128;
    uint8_t nh[12] = {0,0,0,0,0,0,0,0,192,168,1,1};
    mp_nlri->nexthop.assign(&nh[0], &nh[12]);
    mp_nlri->nlri.push_back(bpp);
    update.path_attributes.push_back(mp_nlri);

    peer_->ProcessUpdate(&update);
    task_util::WaitForIdle();
    InetVpnTable::RequestKey key(iv_prefix, peer_);
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) != NULL);
    BgpRoute *rt = static_cast<BgpRoute *>(rib2_->Find(&key));
    TASK_UTIL_EXPECT_TRUE(rt->BestPath() != NULL);
    const BgpPath *path = rt->BestPath();

    if (GetParam()) {
        EXPECT_TRUE(path->GetAttr()->origin() == BgpAttrOrigin::EGP);
    } else {
        EXPECT_TRUE(path->GetAttr()->origin() == BgpAttrOrigin::IGP);
    }

    // Withdraw the prefix.
    BgpProto::Update withdraw;
    BgpMpNlri *mp_nlri2 = new BgpMpNlri;
    BgpProtoPrefix *bpp2 = new BgpProtoPrefix;
    iv_prefix.BuildProtoPrefix(12, bpp2);
    mp_nlri2->code = BgpAttribute::MPUnreachNlri;
    mp_nlri2->afi = 1;
    mp_nlri2->safi = 128;
    mp_nlri2->nlri.push_back(bpp2);
    withdraw.path_attributes.push_back(mp_nlri2);

    peer_->ProcessUpdate(&withdraw);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(rib2_->Find(&key) == NULL);

    peer_->ResetCapabilities();
    route_origin_override_ = false;
}

INSTANTIATE_TEST_CASE_P(Instance, BgpUpdateRxParamTest1, ::testing::Bool());
INSTANTIATE_TEST_CASE_P(Instance,
                        BgpUpdateRxParamTest2,
                        ::testing::Values(static_cast<uint8_t>(0),
                                          static_cast<uint8_t>(1),
                                          static_cast<uint8_t>(2),
                                          static_cast<uint8_t>(3)));

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpPeer>(boost::factory<BgpPeerMock *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
