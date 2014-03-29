/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/rtarget/rtarget_table.h"

#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/rtarget/rtarget_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;

class RTargetTableTest : public ::testing::Test {
protected:
    RTargetTableTest()
            : server_(&evm_), rtable_(NULL) {
    }
              
    virtual void SetUp() {
        master_cfg_.reset(new BgpInstanceConfig(BgpConfigManager::kMasterInstance));
        server_.routing_instance_mgr()->CreateRoutingInstance(
                master_cfg_.get());
        RoutingInstance *rti =
                server_.routing_instance_mgr()->GetRoutingInstance(
                    BgpConfigManager::kMasterInstance);
        ASSERT_TRUE(rti != NULL);

        rtable_ = static_cast<RTargetTable *>(rti->GetTable(Address::RTARGET));
        ASSERT_TRUE(rtable_ != NULL);

        tid_ = rtable_->Register(
            boost::bind(&RTargetTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        rtable_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        task_util::WaitForIdle();
    }

    void TableListener(DBTablePartBase *root, DBEntryBase *entry) {
        bool del_notify = entry->IsDeleted();
        if (del_notify)
            del_notification_++;
        else
            adc_notification_++;
    }

    EventManager evm_;
    BgpServer server_;
    RTargetTable *rtable_;
    DBTableBase::ListenerId tid_;
    std::auto_ptr<BgpInstanceConfig> master_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(RTargetTableTest, Basic) {
    adc_notification_ = 0;
    del_notification_ = 0;
    // Create a route prefix
    RTargetPrefix prefix(RTargetPrefix::FromString("123:target:1:2"));

    // Create a set of route attributes
    BgpAttrSpec attrs;

    TASK_UTIL_EXPECT_EQ(rtable_, server_.database()->FindTable("bgp.rtarget.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new RTargetTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rtable_->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    RTargetTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) != NULL));

    // Enqueue the delete
    DBRequest delReq;
    delReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rtable_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);

    TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) == NULL));
}

TEST_F(RTargetTableTest, DupDelete) {
    adc_notification_ = 0;
    del_notification_ = 0;
    // Create a route prefix
    RTargetPrefix prefix(RTargetPrefix::FromString("123:target:1:2"));

    // Create a set of route attributes
    BgpAttrSpec attrs;

    TASK_UTIL_EXPECT_EQ(rtable_, server_.database()->FindTable("bgp.rtarget.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new RTargetTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rtable_->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    RTargetTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) != NULL));

    
    Route *rt_entry = static_cast<Route *>(rtable_->Find(&key));
    rt_entry->SetState(rtable_, tid_, NULL);

    // Enqueue the delete
    DBRequest delReq;
    delReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rtable_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);

    // Enqueue a duplicate the delete
    delReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rtable_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) != NULL));

    rt_entry = static_cast<Route *>(rtable_->Find(&key));
    rt_entry->ClearState(rtable_, tid_);
    TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) == NULL));
}

TEST_F(RTargetTableTest, SinglePartition) {
    adc_notification_ = 0;
    del_notification_ = 0;
    TASK_UTIL_EXPECT_EQ(rtable_, server_.database()->FindTable("bgp.rtarget.0"));
    // Create a set of route attributes
    BgpAttrSpec attrs;
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);

    for (int i = 1; i <= 1000; i++) {
        // Create a route prefix
        ostringstream rtstr;
        rtstr << "100:target:" << i << ":1";
        RTargetPrefix prefix(RTargetPrefix::FromString(rtstr.str()));
        DBRequest addReq;
        addReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
        addReq.data.reset(new RTargetTable::RequestData(attr, 0, 20));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        rtable_->Enqueue(&addReq);
    }

    TASK_UTIL_EXPECT_EQ(adc_notification_, 1000);

    for (int i = 1; i <= 1000; i++) {
        // Create a route prefix
        ostringstream rtstr;
        rtstr << "100:target:" << i << ":1";
        RTargetPrefix prefix(RTargetPrefix::FromString(rtstr.str()));
        RTargetTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) != NULL));
    }

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(rtable_->GetTablePartition(idx));
        if (idx) TASK_UTIL_EXPECT_EQ(0, tbl_partition->size());
        else TASK_UTIL_EXPECT_EQ(1000, tbl_partition->size());
    }

    for (int i = 1; i <= 1000; i++) {
        // Create a route prefix
        ostringstream rtstr;
        rtstr << "100:target:" << i << ":1";
        RTargetPrefix prefix(RTargetPrefix::FromString(rtstr.str()));
        // Enqueue the delete
        DBRequest delReq;
        delReq.key.reset(new RTargetTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        rtable_->Enqueue(&delReq);
    }

    TASK_UTIL_EXPECT_EQ(del_notification_, 1000);

    for (int i = 1; i <= 1000; i++) {
        // Create a route prefix
        ostringstream rtstr;
        rtstr << "100:target:" << i << ":1";
        RTargetPrefix prefix(RTargetPrefix::FromString(rtstr.str()));
        RTargetTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE((static_cast<Route *>(rtable_->Find(&key)) == NULL));
    }
}

TEST_F(RTargetTableTest, AllocEntryStr) {
    string prefix_str("64512:target:64512:1");
    std::auto_ptr<DBEntry> route = rtable_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
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
