/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/enet/enet_table.h"

#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_log.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;

static const int kRouteCount = 255;

class EnetTableTest : public ::testing::Test {
protected:
    EnetTableTest()
        : server_(&evm_), blue_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        master_cfg_ = BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance, "", "");
        blue_cfg_ = BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:1.2.3.4:1", "target:1.2.3.4:1");

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(master_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<EnetTable *>(
            server_.database()->FindTable("blue.enet.0"));
        TASK_UTIL_EXPECT_EQ(Address::ENET, blue_->family());

        tid_ = blue_->Register(
            boost::bind(&EnetTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(string prefix_str) {
        EnetPrefix prefix(EnetPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new EnetTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new EnetTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
    }

    void DelRoute(string prefix_str) {
        EnetPrefix prefix(EnetPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.key.reset(new EnetTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
    }

    void VerifyRouteExists(string prefix_str) {
        EnetPrefix prefix(EnetPrefix::FromString(prefix_str));
        EnetTable::RequestKey key(prefix, NULL);
        EnetRoute *rt = dynamic_cast<EnetRoute *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1, rt->count());
    }

    void VerifyRouteNoExists(string prefix_str) {
        EnetPrefix prefix(EnetPrefix::FromString(prefix_str));
        EnetTable::RequestKey key(prefix, NULL);
        EnetRoute *rt = static_cast<EnetRoute *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt == NULL);
    }

    void TableListener(DBTablePartBase *tpart, DBEntryBase *entry) {
        bool del_notify = entry->IsDeleted();
        if (del_notify) {
            del_notification_++;
        } else {
            adc_notification_++;
        }
    }

    EventManager evm_;
    BgpServer server_;
    EnetTable *blue_;
    DBTableBase::ListenerId tid_;
    std::auto_ptr<BgpInstanceConfig> master_cfg_;
    std::auto_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(EnetTableTest, AddDeleteSingleRoute) {
    AddRoute("11:12:13:14:15:16,192.168.1.1/32");
    task_util::WaitForIdle();
    VerifyRouteExists("11:12:13:14:15:16,192.168.1.1/32");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute("11:12:13:14:15:16,192.168.1.1/32");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("11:12:13:14:15:16,192.168.1.1/32");
}

// Prefixes differ only in the mac_addr field.
TEST_F(EnetTableTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the ip_addr field.
TEST_F(EnetTableTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(EnetTableTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
