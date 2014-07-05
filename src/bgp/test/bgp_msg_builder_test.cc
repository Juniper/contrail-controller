/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "testing/gunit.h"

#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/l3vpn/inetvpn_address.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"

using namespace std;

namespace {

class BgpPeerMock : public BgpPeer {
public:
    BgpPeerMock(BgpServer *server, RoutingInstance *instance,
                const BgpNeighborConfig *config) :
        BgpPeer(server, instance, config) {
    }

    bool IsReady() const { return true; }
};

class BgpMsgBuilderTest : public testing::Test {
protected:
    BgpMsgBuilderTest()
        : server_(&evm_),
          instance_config_(BgpConfigManager::kMasterInstance),
          config_(&instance_config_, "test-peer", "local", &router_, NULL) {
        ConcurrencyScope scope("bgp::Config");
        RoutingInstance *rti =
                server_.routing_instance_mgr()->CreateRoutingInstance(
                    &instance_config_);
        peer_ = rti->peer_manager()->PeerLocate(&server_, &config_);
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    BgpServer server_;
    BgpInstanceConfig instance_config_;
    autogen::BgpRouter router_;
    BgpNeighborConfig config_;
    BgpPeer *peer_;
};

TEST_F(BgpMsgBuilderTest, Build) {
    BgpAttrSpec attr;
    BgpAttrNextHop *nexthop = new BgpAttrNextHop(0xabcdef01);
    attr.push_back(nexthop);

    BgpAttrOrigin *origin = new BgpAttrOrigin(BgpAttrOrigin::INCOMPLETE);
    attr.push_back(origin);

    BgpAttrMultiExitDisc *med = new BgpAttrMultiExitDisc(1);
    attr.push_back(med);

    BgpAttrLocalPref *lp = new BgpAttrLocalPref(2);
    attr.push_back(lp);

    BgpAttrAtomicAggregate *aa = new BgpAttrAtomicAggregate;
    attr.push_back(aa);

    BgpAttrAggregator *agg = new BgpAttrAggregator(0xface, 0xcafebabe);
    attr.push_back(agg);

    AsPathSpec *path_spec = new AsPathSpec;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SET;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(21);
    ps->path_segment.push_back(22);
    path_spec->path_segments.push_back(ps);
    attr.push_back(path_spec);

    CommunitySpec *community = new CommunitySpec;
    community->communities.push_back(0x87654321);
    attr.push_back(community);

    ExtCommunitySpec *ext_community = new ExtCommunitySpec;
    ext_community->communities.push_back(0x1020304050607080);
    attr.push_back(ext_community);

    RibOutAttr rib_out_attr;
    rib_out_attr.set_attr(server_.attr_db()->Locate(attr));

    InetVpnPrefix p1 = InetVpnPrefix::FromString("12345:2:1.1.1.1/24");
    InetVpnRoute route(p1);
    BgpPath *path = 
        new BgpPath(peer_, BgpPath::BGP_XMPP, rib_out_attr.attr(), 0, 0);
    route.InsertPath(path);
    BgpMessage message;
    message.Start(&rib_out_attr, &route);

    size_t length;
    const uint8_t *data = message.GetData(NULL, &length);
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");

    const BgpProto::Update *result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, length));
    ASSERT_TRUE(result != NULL);
    EXPECT_EQ(attr.size(), result->path_attributes.size());
    for (size_t i = 0; i < attr.size()-1; i++) {
        int ret = attr[i+1]->CompareTo(*result->path_attributes[i]);
        EXPECT_EQ(0, ret);
        if (ret != 0) {
            cout << "Unequal " << TYPE_NAME(*attr[i]) << " "
                    << TYPE_NAME(*result->path_attributes[i])<< endl;
        }
    }

    BgpMpNlri *nlri = static_cast<BgpMpNlri *>(*(result->path_attributes.end() - 1));
    EXPECT_TRUE(nlri != NULL);
    EXPECT_EQ(1, nlri->nlri.size());
    BgpProtoPrefix prefix;
    route.BuildProtoPrefix(&prefix, 0);
    EXPECT_EQ(prefix.prefixlen, nlri->nlri[0]->prefixlen);
    EXPECT_EQ(prefix.prefix, nlri->nlri[0]->prefix);
    route.RemovePath(peer_);

    p1 = InetVpnPrefix::FromString("64.64.64.64:2:1.1.1.1/24");
    InetVpnRoute route2(p1);
    BgpPath *path2 = 
        new BgpPath(peer_, BgpPath::BGP_XMPP, rib_out_attr.attr(), 0, 0);
    route2.InsertPath(path2);
    message.AddRoute(&route2, &rib_out_attr);

    data = message.GetData(NULL, &length);
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");

    delete result;
    result = static_cast<const BgpProto::Update *>(BgpProto::Decode(data, length));
    EXPECT_TRUE(result != NULL);
    EXPECT_EQ(attr.size(), result->path_attributes.size());
    for (size_t i = 0; i < attr.size()-1; i++) {
        int ret = attr[i+1]->CompareTo(*result->path_attributes[i]);
        EXPECT_EQ(0, ret);
        if (ret != 0) {
            cout << "Unequal " << TYPE_NAME(*attr[i]) << " "
                    << TYPE_NAME(*result->path_attributes[i])<< endl;
        }
    }

    nlri = static_cast<BgpMpNlri *>(*(result->path_attributes.end() - 1));
    EXPECT_TRUE(nlri != NULL);
    EXPECT_EQ(2, nlri->nlri.size());
    route2.BuildProtoPrefix(&prefix, 0);
    EXPECT_EQ(prefix.prefixlen, nlri->nlri[1]->prefixlen);
    EXPECT_EQ(prefix.prefix, nlri->nlri[1]->prefix);

    route2.RemovePath(peer_);
    delete nexthop;
    delete origin;
    delete med;
    delete lp;
    delete aa;
    delete agg;
    delete path_spec;
    delete community;
    delete ext_community;
    delete result;
}
}  // namespace

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpPeer>(boost::factory<BgpPeerMock *>());
}

static void TearDown() {
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
