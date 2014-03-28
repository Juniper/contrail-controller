/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_table.h"

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
using namespace boost;

static const int kRouteCount = 255;

class EvpnTableTest : public ::testing::Test {
protected:
    EvpnTableTest()
        : server_(&evm_), evpn_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        master_cfg_.reset(
            new BgpInstanceConfig(BgpConfigManager::kMasterInstance));
        server_.routing_instance_mgr()->CreateRoutingInstance(
            master_cfg_.get());
        RoutingInstance *routing_instance =
            server_.routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance);
        ASSERT_TRUE(routing_instance != NULL);

        evpn_ = static_cast<EvpnTable *>(routing_instance->GetTable(Address::EVPN));
        ASSERT_TRUE(evpn_ != NULL);

        tid_ = evpn_->Register(
            boost::bind(&EvpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        evpn_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_ASSERT_EQ(0, server_.routing_instance_mgr()->count());
    }

    void AddRoute(string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new EvpnTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        evpn_->Enqueue(&addReq);
    }

    void DelRoute(string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        evpn_->Enqueue(&delReq);
    }

    void VerifyRouteExists(string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnTable::RequestKey key(prefix, NULL);
        EvpnRoute *rt = dynamic_cast<EvpnRoute *>(evpn_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1, rt->count());
    }

    void VerifyRouteNoExists(string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnTable::RequestKey key(prefix, NULL);
        EvpnRoute *rt = static_cast<EvpnRoute *>(evpn_->Find(&key));
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
    EvpnTable *evpn_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(EvpnTableTest, AddDeleteSingleRoute) {
    AddRoute("10.1.1.1:65535-11:12:13:14:15:16,192.168.1.1/32");
    task_util::WaitForIdle();
    VerifyRouteExists("10.1.1.1:65535-11:12:13:14:15:16,192.168.1.1/32");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute("10.1.1.1:65535-11:12:13:14:15:16,192.168.1.1/32");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("10.1.1.1:65535-11:12:13:14:15:16,192.168.1.1/32");
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(EvpnTableTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1." << idx << ":65535-";
        repr << "07:00:00:00:00:01,192.168.1.1/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1." << idx << ":65535-";
        repr << "07:00:00:00:00:01,192.168.1.1/32";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1." << idx << ":65535-";
        repr << "07:00:00:00:00:01,192.168.1.1/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1." << idx << ":65535-";
        repr << "07:00:00:00:00:01,192.168.1.1/32";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}


// Prefixes differ only in the mac_addr field.
TEST_F(EvpnTableTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the ip_addr field.
TEST_F(EvpnTableTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:01,192.168.1." << idx << "/32";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(EvpnTableTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
        repr << "07:00:00:00:00:" << hex << idx << ",192.168.1.1/32";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(evpn_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "10.1.1.1:65535-";
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
