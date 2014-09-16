/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6/inet6_table.h"

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

static const int kRouteCount = 255;

class Inet6TableTest : public ::testing::Test {
protected:
    Inet6TableTest() : server_(&evm_), blue_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");
        adc_notification_ = 0;
        del_notification_ = 0;

        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:1.2.3.4:1", "target:1.2.3.4:1"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<Inet6Table *>(
            server_.database()->FindTable("blue.inet6.0"));
        TASK_UTIL_EXPECT_EQ(Address::INET6, blue_->family());

        tid_ = blue_->Register(
            boost::bind(&Inet6TableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    bool AddRoute(std::string prefix_str) {
        boost::system::error_code error;
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str, &error));
        if (error) {
            return false;
        }
        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new Inet6Table::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new Inet6Table::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
        return true;
    }

    bool DelRoute(std::string prefix_str) {
        boost::system::error_code error;
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        DBRequest delReq;
        delReq.key.reset(new Inet6Table::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
        return true;
    }

    bool VerifyRouteExists(std::string prefix_str) {
        boost::system::error_code error;
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        Inet6Table::RequestKey key(prefix, NULL);
        Inet6Route *rt = dynamic_cast<Inet6Route *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1, rt->count());
        return true;
    }

    bool VerifyRouteNoExists(std::string prefix_str) {
        boost::system::error_code error;
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        Inet6Table::RequestKey key(prefix, NULL);
        Inet6Route *rt = static_cast<Inet6Route *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt == NULL);
        return true;
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
    Inet6Table *blue_;
    DBTableBase::ListenerId tid_;
    boost::scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(Inet6TableTest, AddDeleteOneRoute) {
    AddRoute("2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/64");
    task_util::WaitForIdle();
    VerifyRouteExists("2001:0db8:85a3::8a2e:0370:aaaa/64");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute("2001:0db8:85a3::8a2e:0370:aaaa/64");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/64");
    task_util::WaitForIdle();
}

TEST_F(Inet6TableTest, AddDeleteMultipleRoute1) {
    std::string ip_address = "2001:0db8:85a3::8a2e:0370:";
    std::string plen = "/64";
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(Inet6TableTest, Hashing) {
    std::string ip_address = "2001:0db8:85a3:fedc:ba09:8a2e:0370:";
    std::string plen = "/64";
    for (int idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << ip_address << idx << plen;
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
