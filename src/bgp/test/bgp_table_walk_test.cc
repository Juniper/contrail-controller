/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "db/db_table_walk_mgr.h"
#include "db/db_table.h"

using namespace boost;
using namespace std;

class BgpTableWalkTest : public ::testing::Test {
public:
    BgpTableWalkTest()
        : server_(&evm_),
          red_(NULL),
          blue_(NULL),
          purple_(NULL) {
          walk_count_ = 0;
          walk_count_1_ = 0;
          walk_count_2_ = 0;
          walk_done_count_ = 0;
          walk_done_ = false;
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("blue",
                "target:1:1", "target:1:1"));
        red_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("red",
                "target:1:2", "target:1:2"));
        purple_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("purple",
                "target:1:3", "target:1:3"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(
                master_cfg_.get());
        server_.rtarget_group_mgr()->Initialize();
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(red_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(
                purple_cfg_.get());
        scheduler->Start();

        blue_ = static_cast<BgpTable *>(
            server_.database()->FindTable("blue.inet.0"));
        purple_ = static_cast<BgpTable *>(
            server_.database()->FindTable("purple.inet.0"));
        red_ = static_cast<BgpTable *>(
            server_.database()->FindTable("red.inet.0"));
    }

    void AddInetRoute(BgpTable *table, std::string prefix_str,
                      bool find = true) {
        BgpAttrPtr attr_ptr;

        BgpAttrSpec attr_spec;

        BgpAttrOrigin origin(BgpAttrOrigin::IGP);
        attr_spec.push_back(&origin);

        AsPathSpec path_spec;
        AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
        path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
        path_seg->path_segment.push_back(65534);
        path_spec.path_segments.push_back(path_seg);
        attr_spec.push_back(&path_spec);

        BgpAttrNextHop nexthop(0x7f00007f);
        attr_spec.push_back(&nexthop);

        BgpAttrLocalPref local_pref(100);
        attr_spec.push_back(&local_pref);

        attr_ptr = server_.attr_db()->Locate(attr_spec);

        const Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));

        const InetTable::RequestKey key(prefix, NULL);

        DBRequest req;

        // Add prefix
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new InetTable::RequestKey(prefix, NULL));
        req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
        table->Enqueue(&req);
        task_util::WaitForIdle();

        if (find)
            TASK_UTIL_ASSERT_TRUE(table->Find(&key) != NULL);
    }

    void DeleteInetRoute(BgpTable *table, std::string prefix_str,
                         bool find = true) {
        const Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        const InetTable::RequestKey key(prefix, NULL);
        DBRequest req;

        // Delete prefix
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new InetTable::RequestKey(prefix, NULL));
        table->Enqueue(&req);
        task_util::WaitForIdle();

        if (find)
            TASK_UTIL_ASSERT_TRUE(table->Find(&key) == NULL);
    }

    bool WalkTableCallback(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        walk_count_++;
        return true;
    }

    // Return false
    bool WalkTableDoneCallback(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        walk_count_++;
        return false;
    }

    // Walk Callback in "bgp::Config" task context
    bool WalkTableInConfigTaskCtxt(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("bgp::Config");
        walk_count_++;
        return true;
    }

    bool WalkTableCallback_1(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        walk_count_1_++;
        return false;
    }

    bool WalkTableCallback_2(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        walk_count_2_++;
        return true;
    }

    void WalkDone(DBTable::DBTableWalkRef walker, DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        walk_done_ = true;
        walk_done_count_++;
    }

    void WalkDoneToStopSched(DBTable::DBTableWalkRef walker,
                             DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        walk_done_ = true;
        walk_done_count_++;
        TaskScheduler::GetInstance()->Stop();
    }

    void ResetWalkStats() {
        walk_done_ = false;
        walk_count_ = 0;
        walk_done_count_ = 0;
    }

    void WalkTable(BgpTable *table, DBTable::DBTableWalkRef walker) {
        task_util::TaskFire(
           boost::bind(&DBTable::WalkTable, table, walker), "bgp::Config");
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    EventManager evm_;
    BgpServer server_;

    BgpTable *red_;
    BgpTable *blue_;
    BgpTable *purple_;

    tbb::atomic<long> walk_count_;
    tbb::atomic<long> walk_count_1_;
    tbb::atomic<long> walk_count_2_;
    tbb::atomic<bool> walk_done_;
    tbb::atomic<long> walk_done_count_;

    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    scoped_ptr<BgpInstanceConfigTest> purple_cfg_;
    scoped_ptr<BgpInstanceConfigTest> blue_cfg_;
    scoped_ptr<BgpInstanceConfigTest> red_cfg_;
};

//
// Walk the table and verify that walk callbacks are triggered
//
TEST_F(BgpTableWalkTest, Basic) {
    AddInetRoute(red_, "1.1.1.0/24");
    AddInetRoute(blue_, "2.2.2.0/24");
    AddInetRoute(purple_, "3.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    WalkTable(red_, walk_ref);

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);

    DeleteInetRoute(red_, "1.1.1.0/24");
    DeleteInetRoute(blue_, "2.2.2.0/24");
    DeleteInetRoute(purple_, "3.3.3.0/24");
}

//
// Use same walk ref to trigger the walk after previous walk completion
//
TEST_F(BgpTableWalkTest, Basic_1) {
    AddInetRoute(red_, "1.1.1.0/24");
    AddInetRoute(blue_, "2.2.2.0/24");
    AddInetRoute(purple_, "3.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    WalkTable(red_, walk_ref);
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);

    ResetWalkStats();

    WalkTable(red_, walk_ref);
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);

    DeleteInetRoute(red_, "1.1.1.0/24");
    DeleteInetRoute(blue_, "2.2.2.0/24");
    DeleteInetRoute(purple_, "3.3.3.0/24");
}

//
// Use the walk ref as scoped variable and ensure that walk callback is called
// even after scope is gone
//
TEST_F(BgpTableWalkTest, Basic_2) {
    AddInetRoute(red_, "1.1.1.0/24");
    AddInetRoute(blue_, "2.2.2.0/24");
    AddInetRoute(purple_, "3.3.3.0/24");

    {
        DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
             boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
             boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

        WalkTable(red_, walk_ref);
    }

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);

    DeleteInetRoute(red_, "1.1.1.0/24");
    DeleteInetRoute(blue_, "2.2.2.0/24");
    DeleteInetRoute(purple_, "3.3.3.0/24");
}

//
// Allocate multiple walkers and start walk on same table
// Verify that table is walked only once for multiple walk requests and
// all walker callback functions are triggerred
//
TEST_F(BgpTableWalkTest, ClubWalk) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_2 = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    TaskScheduler::GetInstance()->Stop();
    red_->WalkTable(walk_ref_1);
    red_->WalkTable(walk_ref_2);
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Trigger walk on multiple tables at same time.
// verify that walk is performed in serial manner
//
TEST_F(BgpTableWalkTest, SerialWalk) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDoneToStopSched, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_2 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the scheduler on walk complete of RED table
    TaskScheduler::GetInstance()->Stop();
    red_->WalkTable(walk_ref_1);
    blue_->WalkTable(walk_ref_2);
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());
    // Ensure that walk did not start on BLUE table
    TASK_UTIL_EXPECT_EQ(0, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(0, blue_->walk_complete_count());

    // Enable the scheduler to start walk on BLUE
    TaskScheduler::GetInstance()->Start();
    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, blue_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Trigger walk on multiple tables at same time.
// verify that walk is performed in serial manner
// Additionally verify that multiple walk requests are clubbed in on table walk
//
TEST_F(BgpTableWalkTest, SerialWalk_1) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDoneToStopSched, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_2 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_3 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the scheduler on walk complete of RED table
    TaskScheduler::GetInstance()->Stop();
    red_->WalkTable(walk_ref_1);
    blue_->WalkTable(walk_ref_2);
    blue_->WalkTable(walk_ref_3);
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());
    // Ensure that walk did not start on BLUE table
    TASK_UTIL_EXPECT_EQ(0, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(0, blue_->walk_complete_count());

    ResetWalkStats();

    // Enable the scheduler to start walk on BLUE
    TaskScheduler::GetInstance()->Start();
    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    // Ensure that table is walked only once
    TASK_UTIL_EXPECT_EQ(1, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, blue_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Release the walker after starting the walk.
// Verify that actual table walk is not started and walker is not notified
//
TEST_F(BgpTableWalkTest, StopWalk) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the scheduler
    TaskScheduler::GetInstance()->Stop();

    red_->WalkTable(walk_ref);
    red_->ReleaseWalker(walk_ref);

    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_FALSE(walk_done_);

    TASK_UTIL_EXPECT_EQ(0, walk_count_);
    TASK_UTIL_EXPECT_EQ(0, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(0, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Release the walker after starting the walk.
// Start multiple walks on the same table and stop walk only from one of the
// clients. Validate that table is walked once and only active walker is
// notified
//
TEST_F(BgpTableWalkTest, StopWalk_1) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the scheduler
    TaskScheduler::GetInstance()->Stop();

    red_->WalkTable(walk_ref);
    red_->WalkTable(walk_ref_1);
    red_->ReleaseWalker(walk_ref);

    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());

    ResetWalkStats();

    WalkTable(red_, walk_ref_1);

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(2, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Issue WalkAgain on ongoing walk request and verify that table is walked again
// Client is notified only once at the end of WalkAgain()
//
TEST_F(BgpTableWalkTest, WalkAgain) {
    red_->SetWalkIterationToYield(1);

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    red_->WalkTable(walk_ref);

    TASK_UTIL_EXPECT_TRUE(walk_count_ > 0);

    TaskScheduler::GetInstance()->Stop();
    red_->WalkAgain(walk_ref);
    ResetWalkStats();
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(2, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2, red_->walk_complete_count());

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Verify that walker is not notified for all entries of the table if the walk
// callback funtion return "False".
//
TEST_F(BgpTableWalkTest, WalkDone) {
    red_->SetWalkIterationToYield(1);

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableDoneCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    WalkTable(red_, walk_ref);

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_TRUE(walk_count_ < 255);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Verify that walker is not notified for all entries of the table if the walk
// callback funtion return "False".
// If there are multiple walkers on the same table and only one walker callback
// returns False, walk is continued for remaining walkers who requested walk
//
TEST_F(BgpTableWalkTest, WalkDone_1) {
    red_->SetWalkIterationToYield(1);

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    // This walker returns after visiting first entry
    // Since multiple partition would call Walk function concurrently,
    // walk callback will be called for multiple db entry
    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback_1, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback_2, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    TaskScheduler::GetInstance()->Stop();
    red_->WalkTable(walk_ref);
    red_->WalkTable(walk_ref_1);
    TaskScheduler::GetInstance()->Start();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_TRUE(walk_count_1_ < 255);
    TASK_UTIL_EXPECT_EQ(255, walk_count_2_);
    // Walk Done is called twice
    TASK_UTIL_EXPECT_EQ(2, walk_done_count_);
    // Table is walked once
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Verify that TableWalk is performed in specific task context as requested by
// application
//
TEST_F(BgpTableWalkTest, WalkTaskContext) {
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"));

    AddInetRoute(red_, "11.1.1.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableInConfigTaskCtxt, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    WalkTable(red_, walk_ref);

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(1, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
