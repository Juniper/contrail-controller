/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_factory.h"
#include "bgp/ipeer.h"
#include "bgp/bgp_mvpn.h"
#include "bgp/bgp_server.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using boost::scoped_ptr;
using std::string;

class BgpMvpnTest : public ::testing::Test {
protected:
    BgpMvpnTest() : server_(&evm_) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        boost::system::error_code err;
        UpdateBgpIdentifier("127.0.0.1");
        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        red_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("red",
                "target:127.0.0.1:1", "target:127.0.0.1:1"));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("blue",
                "target:127.0.0.1:2", "target:127.0.0.1:2"));

        // Green imports routes from both red and blue RIs.
        green_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("green",
                "target:127.0.0.1:3,target:127.0.0.1:1,target:127.0.0.1:2",
                "target:127.0.0.1:3"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(
                master_cfg_.get());
        server_.rtarget_group_mgr()->Initialize();
        server_.routing_instance_mgr()->CreateRoutingInstance(red_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(green_cfg_.get());
        scheduler->Start();

        master_ = static_cast<BgpTable *>(
            server_.database()->FindTable("bgp.mvpn.0"));
        red_ = static_cast<MvpnTable *>(
            server_.database()->FindTable("red.mvpn.0"));
        blue_ = static_cast<MvpnTable *>(
            server_.database()->FindTable("blue.mvpn.0"));
        green_ = static_cast<MvpnTable *>(
            server_.database()->FindTable("green.mvpn.0"));
    }

    void UpdateBgpIdentifier(const string &address) {
        boost::system::error_code err;
        task_util::TaskFire(boost::bind(&BgpServer::UpdateBgpIdentifier,
            &server_, Ip4Address::from_string(address, err)), "bgp::Config");
    }

    void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddMvpnRoute(BgpTable *table, const string &prefix_str,
                      const string &target) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));

        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);

        BgpAttrPtr attr = server_.attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        task_util::WaitForIdle();
    }

    void DeleteMvpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    BgpTable *master_;
    MvpnTable *red_;
    MvpnTable *blue_;
    MvpnTable *green_;
    scoped_ptr<BgpInstanceConfig> red_cfg_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;
    scoped_ptr<BgpInstanceConfig> green_cfg_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
};

// Ensure that Type1 AD routes are created inside the mvpn table.
TEST_F(BgpMvpnTest, Type1ADLocal) {
    TASK_UTIL_EXPECT_EQ(3, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, red_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    boost::system::error_code err;
    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(red_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(red_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.1", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(blue_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(blue_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.1", err), neighbor.originator());
}

// Change Identifier and ensure that routes have updated originator id.
TEST_F(BgpMvpnTest, Type1ADLocalWithIdentifierChanged) {
    boost::system::error_code err;
    UpdateBgpIdentifier("127.0.0.2");
    TASK_UTIL_EXPECT_EQ(3, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, red_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_NE(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(red_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(red_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.2", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    *(blue_->routing_instance()->GetRD()), &neighbor));
    EXPECT_EQ(*(blue_->routing_instance()->GetRD()), neighbor.rd());
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("127.0.0.2", err), neighbor.originator());
}

// Reset BGP Identifier and ensure that Type1 route is no longer generated.
TEST_F(BgpMvpnTest, Type1ADLocalWithIdentifierRemoved) {
    boost::system::error_code err;
    UpdateBgpIdentifier("0.0.0.0");
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    TASK_UTIL_EXPECT_EQ(0, red_->Size());
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        red_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        blue_->FindType1ADRoute());

    TASK_UTIL_EXPECT_EQ(0, green_->Size()); // 1 green + 1 red + 1 blue
    TASK_UTIL_EXPECT_EQ(static_cast<MvpnRoute *>(NULL),
                        green_->FindType1ADRoute());

    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, green_->manager()->neighbors().size());
}

// Add Type1AD route from a mock bgp peer into bgp.mvpn.0 table.
TEST_F(BgpMvpnTest, Type1AD_Remote) {
    // Verify that only green has discovered a neighbor from red.
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());

    // Inject a Type1 route from a mock peer into bgp.mvpn.0 table with red
    // route-target.
    string prefix = "1-10.1.1.1:65535,9.8.7.6";
    AddMvpnRoute(master_, prefix, "target:127.0.0.1:1");

    TASK_UTIL_EXPECT_EQ(4, master_->Size()); // 3 local + 1 remote
    TASK_UTIL_EXPECT_EQ(2, red_->Size()); // 1 local + 1 remote(red)
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(4, green_->Size()); // 1 local + 1 remote(red)

    // Verify that neighbor is detected.
    TASK_UTIL_EXPECT_EQ(1, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(3, green_->manager()->neighbors().size());

    MvpnNeighbor neighbor;
    boost::system::error_code err;

    EXPECT_TRUE(red_->manager()->FindNeighbor(
                    RouteDistinguisher::FromString("10.1.1.1:65535", &err),
                    &neighbor));
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), neighbor.originator());

    EXPECT_TRUE(green_->manager()->FindNeighbor(
                    RouteDistinguisher::FromString("10.1.1.1:65535", &err),
                    &neighbor));
    EXPECT_EQ(0, neighbor.source_as());
    EXPECT_EQ(IpAddress::from_string("9.8.7.6", err), neighbor.originator());

    DeleteMvpnRoute(master_, prefix);

    // Verify that neighbor is deleted.
    TASK_UTIL_EXPECT_EQ(3, master_->Size()); // 3 local
    TASK_UTIL_EXPECT_EQ(1, red_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(1, blue_->Size()); // 1 local
    TASK_UTIL_EXPECT_EQ(3, green_->Size()); // 1 local + 1 red + 1 blue
    TASK_UTIL_EXPECT_EQ(0, red_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(0, blue_->manager()->neighbors().size());
    TASK_UTIL_EXPECT_EQ(2, green_->manager()->neighbors().size());
}

static void SetUp() {
    bgp_log_test::init();
    MvpnManager::set_enable(true);
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
