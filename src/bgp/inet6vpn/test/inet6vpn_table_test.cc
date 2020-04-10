/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/inet6vpn/inet6vpn_table.h"

#include <boost/format.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_factory.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

static const size_t kRouteCount = 255;

class Inet6VpnTableTest : public ::testing::Test {
protected:
    Inet6VpnTableTest()
        : server_(&evm_), inet6_vpn_table_(NULL) {
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

        inet6_vpn_table_ = static_cast<Inet6VpnTable *>
            (routing_instance->GetTable(Address::INET6VPN));
        ASSERT_TRUE(inet6_vpn_table_ != NULL);

        tid_ = inet6_vpn_table_->Register(
            boost::bind(&Inet6VpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        inet6_vpn_table_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(std::string prefix_str) {
        Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        DBRequest addReq;
        addReq.key.reset(new Inet6VpnTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
        addReq.data.reset(new Inet6VpnTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        inet6_vpn_table_->Enqueue(&addReq);
    }

    void DelRoute(std::string prefix_str) {
        Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.key.reset(new Inet6VpnTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        inet6_vpn_table_->Enqueue(&delReq);
    }

    void VerifyRouteExists(std::string prefix_str) {
        Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
        Inet6VpnTable::RequestKey key(prefix, NULL);
        Inet6VpnRoute *rt =
            dynamic_cast<Inet6VpnRoute *>(inet6_vpn_table_->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(1U, rt->count());
    }

    void VerifyRouteNoExists(std::string prefix_str) {
        Inet6VpnPrefix prefix(Inet6VpnPrefix::FromString(prefix_str));
        Inet6VpnTable::RequestKey key(prefix, NULL);
        Inet6VpnRoute *rt =
            static_cast<Inet6VpnRoute *>(inet6_vpn_table_->Find(&key));
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
    Inet6VpnTable *inet6_vpn_table_;
    DBTableBase::ListenerId tid_;
    boost::scoped_ptr<BgpInstanceConfig> master_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(Inet6VpnTableTest, AddDeleteOneRoute) {
    AddRoute("100:65535:2001:0db8:85a3::8a2e:0370:aaaa/128");
    task_util::WaitForIdle();
    VerifyRouteExists("100:65535:2001:0db8:85a3::8a2e:0370:aaaa/128");
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute("100:65535:2001:0db8:85a3::8a2e:0370:aaaa/128");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists("100:65535:2001:0db8:85a3::8a2e:0370:aaaa/128");
    task_util::WaitForIdle();
}

TEST_F(Inet6VpnTableTest, AddDeleteMultipleRoute1) {
    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << idx << ":65535:";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << idx << ":65535:";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(adc_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), kRouteCount);

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << idx << ":65535:";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << idx << ":65535:";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(del_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), 0U);
}

TEST_F(Inet6VpnTableTest, AddDeleteMultipleRoute2) {
    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << "100:" << idx << ":";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << "100:" << idx << ":";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(adc_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), kRouteCount);

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << "100:" << idx << ":";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << "100:" << idx << ":";
        repr << "2001:0db8:85a3::8a2e:0370:aaaa/128";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(del_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), 0U);
}

TEST_F(Inet6VpnTableTest, AddDeleteMultipleRoute3) {
    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << "100:4294967295:";
        repr << "2001:0db8:85a3::8a2e:0370:" << idx << "/128";
        AddRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; ++idx) {
        std::ostringstream repr;
        repr << "100:4294967295:";
        repr << "2001:0db8:85a3::8a2e:0370:" << idx << "/128";
        VerifyRouteExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(adc_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), kRouteCount);

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << "100:4294967295:";
        repr << "2001:0db8:85a3::8a2e:0370:" << idx << "/128";
        DelRoute(repr.str());
    }
    task_util::WaitForIdle();

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << "100:4294967295:";
        repr << "2001:0db8:85a3::8a2e:0370:" << idx << "/128";
        VerifyRouteNoExists(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(static_cast<size_t>(del_notification_), kRouteCount);
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), 0U);
}

TEST_F(Inet6VpnTableTest, Hashing) {
    std::string rd_string = "100:4294967295:";
    std::string ip_address = "2001:0db8:85a3:fedc:ba09:8a2e:0370:";
    std::string plen = "/128";
    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << rd_string << ip_address << (boost::format("%04X") % idx) << plen;
        AddRoute(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), kRouteCount);

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition = static_cast<DBTablePartition *>
            (inet6_vpn_table_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0U, tbl_partition->size());
    }

    for (size_t idx = 1; idx <= kRouteCount; idx++) {
        std::ostringstream repr;
        repr << rd_string << ip_address << (boost::format("%04X") % idx) << plen;
        DelRoute(repr.str());
    }
    TASK_UTIL_EXPECT_EQ(inet6_vpn_table_->Size(), 0U);
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
