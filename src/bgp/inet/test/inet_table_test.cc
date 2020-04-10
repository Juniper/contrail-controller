/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet/inet_table.h"


#include "base/task_annotations.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using std::ostringstream;
using std::string;

static const int kRouteCount = 255;

class InetTableTest : public ::testing::Test {
protected:
    InetTableTest() : server_(&evm_), blue_(NULL), tid_(-1) {
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

        blue_ = static_cast<InetTable *>(
            server_.database()->FindTable("blue.inet.0"));
        TASK_UTIL_EXPECT_EQ(Address::INET, blue_->family());

        tid_ = blue_->Register(
            boost::bind(&InetTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    bool AddRoute(string prefix_str) {
        boost::system::error_code error;
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str, &error));
        if (error) {
            return false;
        }
        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new InetTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new InetTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        blue_->Enqueue(&addReq);
        return true;
    }

    bool DelRoute(string prefix_str) {
        boost::system::error_code error;
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        DBRequest delReq;
        delReq.key.reset(new InetTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        blue_->Enqueue(&delReq);
        return true;
    }

    bool VerifyRouteExists(string prefix_str) {
        boost::system::error_code error;
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        InetTable::RequestKey key(prefix, NULL);
        InetRoute *rt = dynamic_cast<InetRoute *>(blue_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1U, rt->count());
        return true;
    }

    bool VerifyRouteNoExists(string prefix_str) {
        boost::system::error_code error;
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        if (error) {
            return false;
        }
        InetTable::RequestKey key(prefix, NULL);
        InetRoute *rt = static_cast<InetRoute *>(blue_->Find(&key));
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
    InetTable *blue_;
    DBTableBase::ListenerId tid_;
    boost::scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(InetTableTest, AddDeleteOneRoute) {
    AddRoute("192.168.1.1/32");
    task_util::WaitForIdle();
    VerifyRouteExists("192.168.1.1/32");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute("192.168.1.1/32");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("192.168.1.1/32");
    task_util::WaitForIdle();
}

TEST_F(InetTableTest, AddDeleteMultipleRoute1) {
    string ip_address = "192.168.1.";
    string plen = "/32";
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(InetTableTest, Hashing) {
    string ip_address = "192.168.1.";
    string plen = "/32";
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0U, tbl_partition->size());
    }

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << ip_address << idx << plen;
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpConfigManager>(
        boost::factory<BgpIfmapConfigManager *>());
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
