/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "db/db_table.h"
#include "db/db_table_walk_mgr.h"

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
          walk_done_count_1_ = 0;
          walk_done_ = false;
          walk_done_1_ = false;
          current_walk_seq_ = 0;
          current_done_seq_ = 0;
          pause_walk_ = false;
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
        walk_count_++;
        return true;
    }

    void WalkDone(DBTable::DBTableWalkRef walk_ref, DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        walk_done_ = true;
        walk_done_count_++;
    }

    // Verify the order of Walk Callback
    bool VerifyWalkCbOrder(int seq, DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        TASK_UTIL_EXPECT_TRUE(seq > current_walk_seq_);
        current_walk_seq_ = seq;
        return true;
    }

    // Verify the order of Walk Done Callback
    void VerifyWalkDoneCbOrder(int seq, DBTable::DBTableWalkRef walk_ref,
                               DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        TASK_UTIL_EXPECT_TRUE(seq > current_done_seq_);
        current_done_seq_ = seq;
    }

    // Return false to stop walker from continuing
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
        walk_count_1_++;
        return true;
    }

    bool WalkTableCallback_2(DBTablePartBase *root, DBEntryBase *entry) {
        CHECK_CONCURRENCY("db::DBTable");
        walk_count_2_++;
        return false;
    }

    void WalkDone_1(DBTable::DBTableWalkRef walk_ref, DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        walk_done_1_ = true;
        walk_done_count_1_++;
    }

    void WalkDoneToStopWalkProcessing(DBTable::DBTableWalkRef walk_ref,
                             DBTableBase *table) {
        CHECK_CONCURRENCY("db::Walker");
        walk_done_ = true;
        walk_done_count_++;
        // Disable the walk done processing to validate new walk is done in serial
        // manner
        DisableWalkProcessingInline();
    }

    void ResetWalkStats() {
        walk_done_ = false;
        walk_done_1_ = false;
        walk_count_ = 0;
        walk_count_1_ = 0;
        walk_count_2_ = 0;
        walk_done_count_ = 0;
        walk_done_count_1_ = 0;
        current_walk_seq_ = 0;
        current_done_seq_ = 0;
    }

    void WalkTable(BgpTable *table, DBTable::DBTableWalkRef walk_ref) {
        task_util::TaskFire(
           boost::bind(&DBTable::WalkTable, table, walk_ref), "bgp::Config");
    }

    void WalkAgain(BgpTable *table, DBTable::DBTableWalkRef walk_ref) {
        task_util::TaskFire(
           boost::bind(&DBTable::WalkAgain, table, walk_ref), "bgp::Config");
    }

    void DisableWalkProcessingInline() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        walk_mgr->DisableWalkProcessing();
    }

    void EnableWalkProcessingInline() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        walk_mgr->EnableWalkProcessing();
    }

    void DisableWalkProcessing() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        task_util::TaskFire(
            boost::bind(&DBTableWalkMgr::DisableWalkProcessing, walk_mgr),
            "bgp::Config");
    }

    void EnableWalkProcessing() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        task_util::TaskFire(
            boost::bind(&DBTableWalkMgr::EnableWalkProcessing, walk_mgr),
            "bgp::Config");
    }

    void DisableWalkDoneProcessing() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        task_util::TaskFire(
            boost::bind(&DBTableWalkMgr::DisableWalkDoneTrigger, walk_mgr),
            "bgp::Config");
    }

    void EnableWalkDoneProcessing() {
        DBTableWalkMgr *walk_mgr = server_.database()->GetWalkMgr();
        task_util::TaskFire(
            boost::bind(&DBTableWalkMgr::EnableWalkDoneTrigger, walk_mgr),
            "bgp::Config");
    }

    void PauseTableWalk() {
        pause_walk_ = true;
    }

    void ResumeTableWalk() {
        pause_walk_ = false;
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
    tbb::atomic<bool> walk_done_1_;
    tbb::atomic<long> walk_done_count_;
    tbb::atomic<long> walk_done_count_1_;
    tbb::atomic<long> current_walk_seq_;
    tbb::atomic<long> current_done_seq_;

    tbb::atomic<bool> pause_walk_;

    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    scoped_ptr<BgpInstanceConfigTest> purple_cfg_;
    scoped_ptr<BgpInstanceConfigTest> blue_cfg_;
    scoped_ptr<BgpInstanceConfigTest> red_cfg_;
    class WalkPauseTask;
};

//
// Pause task to ensure walk task is not invoked
// Run method of this task would wait for pause_walk_ to go false
//
class BgpTableWalkTest::WalkPauseTask : public Task {
public:
    explicit WalkPauseTask(BgpTableWalkTest *test, int pause_task_id)
        : Task(pause_task_id, 0), parent_(test) {
    }
    bool Run() {
        while (parent_->pause_walk_) {
            usleep(2000);
        }
        return true;
    }
    std::string Description() const { return "WalkPauseTask::WalkPauseTask"; }

private:
    BgpTableWalkTest *parent_;
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
// Use same walk ref to trigger the walk before previous walk completion
//
TEST_F(BgpTableWalkTest, Basic_3) {
    AddInetRoute(red_, "1.1.1.0/24");
    AddInetRoute(blue_, "2.2.2.0/24");
    AddInetRoute(purple_, "3.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DisableWalkProcessing();
    WalkTable(red_, walk_ref);
    WalkTable(red_, walk_ref);
    EnableWalkProcessing();

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

    DisableWalkProcessing();
    WalkTable(red_, walk_ref_1);
    WalkTable(red_, walk_ref_2);
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

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
    boost::bind(&BgpTableWalkTest::WalkDoneToStopWalkProcessing, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_2 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the walk processing till we start both walks
    DisableWalkProcessing();
    WalkTable(red_, walk_ref_1);
    WalkTable(blue_, walk_ref_2);
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());
    // Ensure that walk did not start on BLUE table
    TASK_UTIL_EXPECT_EQ(0U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(0U, blue_->walk_complete_count());

    // Enable the Walk trigger.
    // Note: WalkDoneToStopWalkProcessing which is the WalkCompleteCallback,
    // disables the walk processing task trigger
    EnableWalkProcessing();
    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_complete_count());

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
              boost::bind(&BgpTableWalkTest::WalkDoneToStopWalkProcessing, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_2 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));
    DBTable::DBTableWalkRef walk_ref_3 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the walk processing till we start both walks
    DisableWalkProcessing();
    WalkTable(red_, walk_ref_1);
    WalkTable(blue_, walk_ref_2);
    WalkTable(blue_, walk_ref_3);
    // Enable the Walk processing trigger
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());
    // Ensure that walk did not start on BLUE table
    TASK_UTIL_EXPECT_EQ(0U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(0U, blue_->walk_complete_count());

    ResetWalkStats();

    // Enable the Walk trigger.
    // Note: WalkDoneToStopWalkProcessing which is the WalkCompleteCallback,
    // disables the walk processing task trigger
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_EQ(2, walk_count_);
    // Ensure that table is walked only once
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Release the walker after starting the walk and before infra actually started
// the walk.
// Verify that actual table walk is not started and walker is not notified
//
TEST_F(BgpTableWalkTest, StopWalk) {
    AddInetRoute(red_, "11.1.1.0/24");
    AddInetRoute(blue_, "22.2.2.0/24");
    AddInetRoute(purple_, "33.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Disable the Walk trigger to ensure that new walks are not started
    DisableWalkProcessing();
    WalkTable(red_, walk_ref);
    red_->ReleaseWalker(walk_ref);
    EnableWalkProcessing();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_FALSE(walk_done_);

    TASK_UTIL_EXPECT_EQ(0, walk_count_);
    TASK_UTIL_EXPECT_EQ(0U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(0U, red_->walk_complete_count());

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

    // Disable the walk processing and start walk
    DisableWalkProcessing();
    WalkTable(red_, walk_ref);
    WalkTable(red_, walk_ref_1);
    red_->ReleaseWalker(walk_ref);
    EnableWalkProcessing();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    // Table is walked only for walk_ref_1
    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

    ResetWalkStats();

    // Walk the table again for walk_ref that had finished the walk
    WalkTable(red_, walk_ref_1);

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
    DeleteInetRoute(blue_, "22.2.2.0/24");
    DeleteInetRoute(purple_, "33.3.3.0/24");
}

//
// Release the walker after starting the walk.
// Verify that
//    1. not all entries are notified
//    2. Walk Complete callback is not notified
//    3. Table is walked and walk completes on the table
//
TEST_F(BgpTableWalkTest, StopWalk_2) {
    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }
    // Configure red table such that walk runs in context of bgp::ResolverPath
    // Walk Pause task runs in the context of bgp::ResolverNexthop
    // Walk is requested from bgp::RTFilter task
    // Pause task will ensure that walk task is not started as bgp::ResolverPath
    // and bgp:ResolverNexthop are mutually exclusive
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"));

    // Pause the table walk
    PauseTableWalk();

    int pause_task =
        TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop");
    WalkPauseTask *task = new WalkPauseTask(this, pause_task);
    TaskScheduler::GetInstance()->Enqueue(task);

    // Disable the Walk trigger to ensure that new walks are not started
    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    // Walk started?
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());

    // After the walk has started, ReleaseWalker the walker
    // ReleaseWalker can be invoked from any task context
    red_->ReleaseWalker(walk_ref);

    // Resume the Table walk after ReleaseWalker
    ResumeTableWalk();

    TASK_UTIL_EXPECT_FALSE(walk_done_);
    TASK_UTIL_EXPECT_EQ(walk_count_, 0);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(0, walk_done_count_);

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Create two walk_ref and start walk on two tables
// Release the walker after starting the walk for one walk_ref.
// Validate that WalkComplete is not invoked for walk_ref which is stopped and
// second table walk completes.
//
TEST_F(BgpTableWalkTest, StopWalk_3) {
    AddInetRoute(blue_, "22.2.2.0/24");

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback_1, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = blue_->AllocWalker(
              boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
              boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Configure red table such that walk runs in context of bgp::ResolverPath
    // Walk Pause task runs in the context of bgp::ResolverNexthop
    // Walk is requested from bgp::RTFilter task
    // Pause task will ensure that walk task is not started as bgp::ResolverPath
    // and bgp:ResolverNexthop are mutually exclusive
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"));

    int pause_task =
        TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop");
    // Pause RED table walk only
    // PauseTask and Walk task for RED table are mutually exclusive
    PauseTableWalk();
    WalkPauseTask *task = new WalkPauseTask(this, pause_task);
    TaskScheduler::GetInstance()->Enqueue(task);

    // Disable the walk processing and start walk
    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref),
                        "bgp::RTFilter");
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, blue_, walk_ref_1),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    // Wait till walk has been initiated
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());

    // ReleaseWalker the walker
    red_->ReleaseWalker(walk_ref);

    ResumeTableWalk();

    // Blue walk finished ?
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(walk_count_, 1);

    // Red is walked but no DBEntries are notified as ReleaseWalker was called
    TASK_UTIL_EXPECT_EQ(walk_count_1_, 0 );
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
    DeleteInetRoute(blue_, "22.2.2.0/24");
}

//
// Issue WalkAgain on ongoing walk request and verify that table is walked again
// Client is notified about WalkDone only once at the end of WalkAgain()
//
TEST_F(BgpTableWalkTest, WalkAgain) {
    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Configure red table such that walk runs in context of bgp::ResolverPath
    // Walk Pause task runs in the context of bgp::ResolverNexthop
    // Walk is requested from bgp::RTFilter task
    // Pause task will ensure that walk task is not started as bgp::ResolverPath
    // and bgp:ResolverNexthop are mutually exclusive
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"));

    int pause_task =
        TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop");
    // Pause RED table walk
    // PauseTask and Walk task for RED table are mutually exclusive
    PauseTableWalk();
    WalkPauseTask *task = new WalkPauseTask(this, pause_task);
    TaskScheduler::GetInstance()->Enqueue(task);

    // Disable the walk processing and start walk
    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());

    // Call WalkAgain() and validate that table is walked mutliple times and
    // walk_done is invoked only once
    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkAgain, red_, walk_ref),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    // Stop the PauseTask to resume table walk
    ResumeTableWalk();

    TASK_UTIL_EXPECT_EQ(2U, red_->walk_count());
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(walk_count_,  255);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_complete_count());

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Issue WalkAgain on walk request for which walk has not yet started and verify
// that table is walked only once.
// Client is notified about WalkDone only once at the end of WalkAgain()
//
TEST_F(BgpTableWalkTest, WalkAgain_1) {
    AddInetRoute(red_, "1.1.1.0/24");
    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DisableWalkProcessing();
    WalkTable(red_, walk_ref);
    WalkAgain(red_, walk_ref);
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(1, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

    DeleteInetRoute(red_, "1.1.1.0/24");
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
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

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
     boost::bind(&BgpTableWalkTest::WalkTableCallback_2, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback_1, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DisableWalkProcessing();
    WalkTable(red_, walk_ref);
    WalkTable(red_, walk_ref_1);
    EnableWalkProcessing();

    TASK_UTIL_EXPECT_TRUE(walk_done_);

    TASK_UTIL_EXPECT_TRUE(walk_count_2_ < 255);
    TASK_UTIL_EXPECT_EQ(255, walk_count_1_);
    // Walk Done is called twice
    TASK_UTIL_EXPECT_EQ(2, walk_done_count_);
    // Table is walked once
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

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
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

    DeleteInetRoute(red_, "11.1.1.0/24");
}

//
// Verify walk request on table with ongoing walk results in two DBTable walk
//
TEST_F(BgpTableWalkTest, WalkInprogress) {
    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback_1, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone_1, this, _1, _2));

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        AddInetRoute(red_, prefix, false);
    }

    // Configure red table such that walk runs in context of bgp::ResolverPath
    // Walk Pause task runs in the context of bgp::ResolverNexthop
    // Walk is requested from bgp::RTFilter task
    // Pause task will ensure that walk task is not started as bgp::ResolverPath
    // and bgp:ResolverNexthop are mutually exclusive
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"));

    int pause_task =
        TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop");
    // Pause RED table walk
    // PauseTask and Walk task for RED table are mutually exclusive
    PauseTableWalk();
    WalkPauseTask *task = new WalkPauseTask(this, pause_task);
    TaskScheduler::GetInstance()->Enqueue(task);

    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());

    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref_1),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    // Resume the table walk
    ResumeTableWalk();

    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_TRUE(walk_done_1_);

    TASK_UTIL_EXPECT_EQ(255, walk_count_);
    TASK_UTIL_EXPECT_EQ(255, walk_count_1_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_1_);
    // Table is walked twice
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_complete_count());

    for (int idx = 0; idx < 255; idx++) {
        string prefix = string("10.1.1.") + integerToString(idx % 255) + "/32";
        DeleteInetRoute(red_, prefix, false);
    }
}

//
// Verify walk order
// Walk red and blue tables, and then request another walk of red with a
// different walk ref after the first walk has started but not yet finished.
// Verify that the order of the walks is red, blue, red.
//
TEST_F(BgpTableWalkTest, WalkInprogress_1) {
    AddInetRoute(red_, "1.1.1.0/24");
    AddInetRoute(blue_, "2.2.2.0/24");
    AddInetRoute(purple_, "3.3.3.0/24");

    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::VerifyWalkCbOrder, this, 1, _1, _2),
     boost::bind(&BgpTableWalkTest::VerifyWalkDoneCbOrder, this, 1, _1, _2));

    DBTable::DBTableWalkRef walk_ref_1 = blue_->AllocWalker(
     boost::bind(&BgpTableWalkTest::VerifyWalkCbOrder, this, 2, _1, _2),
     boost::bind(&BgpTableWalkTest::VerifyWalkDoneCbOrder, this, 2, _1, _2));

    DBTable::DBTableWalkRef walk_ref_2 = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::VerifyWalkCbOrder, this, 3, _1, _2),
     boost::bind(&BgpTableWalkTest::VerifyWalkDoneCbOrder, this, 3, _1, _2));

    WalkTable(red_, walk_ref);
    WalkTable(blue_, walk_ref_1);
    WalkTable(red_, walk_ref_2);

    TASK_UTIL_EXPECT_EQ(2U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(2U, red_->walk_complete_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, blue_->walk_complete_count());

    DeleteInetRoute(red_, "1.1.1.0/24");
    DeleteInetRoute(blue_, "2.2.2.0/24");
    DeleteInetRoute(purple_, "3.3.3.0/24");
}

//
// Verify walk request on empty table
//
TEST_F(BgpTableWalkTest, WalkEmptyTable) {
    DBTable::DBTableWalkRef walk_ref = red_->AllocWalker(
     boost::bind(&BgpTableWalkTest::WalkTableCallback, this, _1, _2),
     boost::bind(&BgpTableWalkTest::WalkDone, this, _1, _2));

    // Configure red table such that walk runs in context of bgp::ResolverPath
    // Walk Pause task runs in the context of bgp::ResolverNexthop
    // Walk is requested from bgp::RTFilter task
    // Pause task will ensure that walk task is not started as bgp::ResolverPath
    // and bgp:ResolverNexthop are mutually exclusive
    //
    // Verify that Walk completes without starting Walk on any DB table
    // partition
    red_->SetWalkTaskId(TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"));

    int pause_task =
        TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop");
    // Pause RED table walk
    // PauseTask and Walk task for RED table are mutually exclusive
    PauseTableWalk();
    WalkPauseTask *task = new WalkPauseTask(this, pause_task);
    TaskScheduler::GetInstance()->Enqueue(task);

    DisableWalkProcessingInline();
    task_util::TaskFire(boost::bind(&DBTable::WalkTable, red_, walk_ref),
                        "bgp::RTFilter");
    EnableWalkProcessingInline();

    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());

    // Resume the table walk
    ResumeTableWalk();
    task_util::WaitForIdle();

    // Table is walked
    TASK_UTIL_EXPECT_TRUE(walk_done_);
    TASK_UTIL_EXPECT_EQ(0, walk_count_);
    TASK_UTIL_EXPECT_EQ(1, walk_done_count_);
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_count());
    TASK_UTIL_EXPECT_EQ(1U, red_->walk_complete_count());
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
