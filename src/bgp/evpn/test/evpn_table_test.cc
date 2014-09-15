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
#include "bgp/bgp_evpn.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;
using namespace boost;

class EvpnManagerMock : public EvpnManager {
public:
    EvpnManagerMock(EvpnTable *table) : EvpnManager(table) {
    }
    ~EvpnManagerMock() { }

    virtual void Initialize() { }
    virtual void Terminate() { }

    virtual UpdateInfo *GetUpdateInfo(EvpnRoute *route) { return NULL; }

private:
};

static const int kRouteCount = 255;

class EvpnTableTest : public ::testing::Test {
protected:
    EvpnTableTest()
        : server_(&evm_), master_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:64512:1", "target:64512:1", "blue", 1));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(master_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<EvpnTable *>(
            server_.database()->FindTable("blue.evpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::EVPN, blue_->family());
        master_ = static_cast<EvpnTable *>(
            server_.database()->FindTable("bgp.evpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::EVPN, master_->family());

        tid_ = master_->Register(
            boost::bind(&EvpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        master_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_ASSERT_EQ(0, server_.routing_instance_mgr()->count());
    }

    void AddRoute(EvpnTable *table, string prefix_str,
        string rtarget_str = "", string source_rd_str = "",
        int virtual_network_index = -1) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));

        BgpAttrSpec attrs;
        ExtCommunitySpec ext_comm;
        if (!rtarget_str.empty()) {
            RouteTarget rtarget = RouteTarget::FromString(rtarget_str);
            ext_comm.communities.push_back(rtarget.GetExtCommunityValue());
        }

        int vn_index;
        if (virtual_network_index != -1) {
            vn_index = virtual_network_index;
        } else {
            vn_index = table->routing_instance()->virtual_network_index();
        }
        OriginVn origin_vn(server_.autonomous_system(), vn_index);
        if (vn_index) {
            ext_comm.communities.push_back(origin_vn.GetExtCommunityValue());
        }
        attrs.push_back(&ext_comm);

        BgpAttrSourceRd source_rd;
        if (!source_rd_str.empty()) {
            source_rd = BgpAttrSourceRd(
                RouteDistinguisher::FromString(source_rd_str));
            attrs.push_back(&source_rd);
        }

        BgpAttrPtr attr = server_.attr_db()->Locate(attrs);

        DBRequest addReq;
        addReq.key.reset(new EvpnTable::RequestKey(prefix, NULL));
        addReq.data.reset(new EvpnTable::RequestData(attr, 0, 0));
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&addReq);
    }

    void DelRoute(EvpnTable *table, string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.key.reset(new EvpnTable::RequestKey(prefix, NULL));
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delReq);
    }

    void VerifyRouteExists(EvpnTable *table, string prefix_str,
        size_t count = 1) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnTable::RequestKey key(prefix, NULL);
        EvpnRoute *rt = dynamic_cast<EvpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(count, rt->count());
    }

    void VerifyRouteNoExists(EvpnTable *table, string prefix_str) {
        EvpnPrefix prefix(EvpnPrefix::FromString(prefix_str));
        EvpnTable::RequestKey key(prefix, NULL);
        EvpnRoute *rt = static_cast<EvpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt == NULL);
    }

    void VerifyTablePartitionCommon(EvpnTable *table,
        bool empty, int start_idx, int end_idx) {
        if (end_idx == -1)
            end_idx = start_idx + 1;
        TASK_UTIL_EXPECT_TRUE(start_idx < end_idx);
        for (int idx = start_idx; idx < end_idx; ++idx) {
            DBTablePartition *tbl_partition =
                static_cast<DBTablePartition *>(table->GetTablePartition(idx));
            if (empty) {
                TASK_UTIL_EXPECT_EQ(0, tbl_partition->size());
            } else {
                TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
            }
        }
    }

    void VerifyTablePartitionEmpty(EvpnTable *table,
        int start_idx, int end_idx = -1) {
        VerifyTablePartitionCommon(table, true, start_idx, end_idx);
    }

    void VerifyTablePartitionNonEmpty(EvpnTable *table,
        int start_idx, int end_idx = -1) {
        VerifyTablePartitionCommon(table, false, start_idx, end_idx);
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
    EvpnTable *master_;
    EvpnTable *blue_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

class EvpnTableAutoDiscoveryTest : public EvpnTableTest {
};

TEST_F(EvpnTableAutoDiscoveryTest, AllocEntryStr) {
    string prefix_str("1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-65536");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableAutoDiscoveryTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-65536";
    AddRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(master_, repr.str());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(EvpnTableAutoDiscoveryTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-65536";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-65536";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-65536";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-65536";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}


// Prefixes differ only in the esi field.
TEST_F(EvpnTableAutoDiscoveryTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-65536";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-65536";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-65536";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-65536";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the tag field.
TEST_F(EvpnTableAutoDiscoveryTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-" << idx;
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-" << idx;
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-" << idx;
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-" << idx;
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Route is not replicated from VPN.
//
TEST_F(EvpnTableAutoDiscoveryTest, ReplicateRouteFromVPN) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-65536";
    AddRoute(master_, repr.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Route is not replicated to VPN.
//
TEST_F(EvpnTableAutoDiscoveryTest, ReplicateRouteToVPN) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-65536";
    AddRoute(blue_, repr.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

class EvpnTableMacAdvertisementTest : public EvpnTableTest {
};

TEST_F(EvpnTableMacAdvertisementTest, AllocEntryStr1) {
    string prefix_str("2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1.1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableMacAdvertisementTest, AllocEntryStr2) {
    string prefix_str("2-10.1.1.1:65535-100000-11:12:13:14:15:16,192.168.1.1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableMacAdvertisementTest, AllocEntryStr3) {
    string prefix_str("2-10.1.1.1:65535-100000-11:12:13:14:15:16,0.0.0.0");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableMacAdvertisementTest, AllocEntryStr4) {
    string prefix_str("2-10.1.1.1:65535-0-11:12:13:14:15:16,2001:db8:0:9::1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableMacAdvertisementTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1.1";
    AddRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(master_, repr.str());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(EvpnTableMacAdvertisementTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-";
        repr << "0-07:00:00:00:00:01,192.168.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-";
        repr << "0-07:00:00:00:00:01,192.168.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-";
        repr << "0-07:00:00:00:00:01,192.168.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-";
        repr << "0-07:00:00:00:00:01,192.168.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}


// Prefixes differ only in the mac_addr field.
TEST_F(EvpnTableMacAdvertisementTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the ip_addr field.
TEST_F(EvpnTableMacAdvertisementTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:01,192.168.1." << idx;
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:01,192.168.1." << idx;
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:01,192.168.1." << idx;
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:01,192.168.1." << idx;
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(EvpnTableMacAdvertisementTest, AddDeleteBroadcastRoute) {
    ostringstream repr1, repr2;
    repr1 << "2-10.1.1.1:65535-0-FF:FF:FF:FF:FF:FF,0.0.0.0";
    repr2 << "2-0:0-0-FF:FF:FF:FF:FF:FF,0.0.0.0";

    AddRoute(blue_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr1.str());
    VerifyRouteNoExists(master_, repr1.str());
    VerifyRouteNoExists(master_, repr2.str());

    DelRoute(blue_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr1.str());
}

TEST_F(EvpnTableMacAdvertisementTest, Hashing) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    VerifyTablePartitionNonEmpty(master_, 0, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-";
        repr << "0-07:00:00:00:00:" << hex << idx << ",192.168.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();
}

//
// Basic - RD in VRF is null.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteFromVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1.1";
    repr2 << "2-0:0-0-11:12:13:14:15:16,192.168.1.1";
    AddRoute(master_, repr1.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different RDs result in different paths for same route in VRF.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteFromVPN2) {
    ostringstream repr2;
    repr2 << "2-0:0-0-11:12:13:14:15:16,192.168.1.1";

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-0-11:12:13:14:15:16,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:64512:1");
        task_util::WaitForIdle();
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str(), idx);
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-0-11:12:13:14:15:16,192.168.1.1";
        DelRoute(master_, repr1.str());
        task_util::WaitForIdle();
        VerifyRouteNoExists(master_, repr1.str());
        if (idx == kRouteCount) {
            VerifyRouteNoExists(blue_, repr2.str());
        } else {
            VerifyRouteExists(blue_, repr2.str(), kRouteCount - idx);
        }
    }

    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different MAC addresses result in different routes in VRF.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteFromVPN3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        AddRoute(master_, repr1.str(), "target:64512:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        repr2 << "2-0:0-0-";
        repr2 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        repr2 << "2-0:0-0-";
        repr2 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different IP addresses result in different routes in VRF.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteFromVPN4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        AddRoute(master_, repr1.str(), "target:64512:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        repr2 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        repr2 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Basic - RD of VPN route is set to provided source RD.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteToVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-0:0-0-11:12:13:14:15:16,192.168.1.1";
    repr2 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1.1";
    AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    DelRoute(blue_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different MAC addresses result in different routes in VPN.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteToVPN3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        repr2 << "2-10.1.1.1:65535-0-";
        repr2 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr1.str());
        VerifyRouteExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        DelRoute(blue_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-0-";
        repr1 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        repr2 << "2-10.1.1.1:65535-0-";
        repr2 << "11:12:13:14:15:" << hex << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr1.str());
        VerifyRouteNoExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different IP addresses result in different routes in VPN.
//
TEST_F(EvpnTableMacAdvertisementTest, ReplicateRouteToVPN4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        repr2 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        VerifyRouteExists(blue_, repr1.str());
        VerifyRouteExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        DelRoute(blue_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-0-11:12:13:14:15:16,192.168.1." << idx;
        repr2 << "2-10.1.1.1:65535-0-11:12:13:14:15:16,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr1.str());
        VerifyRouteNoExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

class EvpnTableInclusiveMulticastTest : public EvpnTableTest {
};

TEST_F(EvpnTableInclusiveMulticastTest, AllocEntryStr1) {
    string prefix_str("3-10.1.1.1:65535-0-192.1.1.1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableInclusiveMulticastTest, AllocEntryStr2) {
    string prefix_str("3-10.1.1.1:65535-65536-192.1.1.1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableInclusiveMulticastTest, AllocEntryStr3) {
    string prefix_str("3-10.1.1.1:65535-65536-2001:db8:0:9::1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableInclusiveMulticastTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "3-10.1.1.1:65535-65536-192.1.1.1";
    AddRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(master_, repr.str());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(EvpnTableInclusiveMulticastTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}


// Prefixes differ only in the tag field.
TEST_F(EvpnTableInclusiveMulticastTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-" << idx << "-192.1.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-" << idx << "-192.1.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-" << idx << "-192.1.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-" << idx << "-192.1.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the IP address field.
TEST_F(EvpnTableInclusiveMulticastTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Basic - RD is preserved in VRF.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteFromVPN1) {
    ostringstream repr;
    repr << "3-10.1.1.1:65535-65536-192.1.1.1";
    AddRoute(master_, repr.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different RDs result in different routes in VRF.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteFromVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        AddRoute(master_, repr.str(), "target:64512:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteExists(master_, repr.str());
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteNoExists(master_, repr.str());
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different IP addresses result in different routes in VRF.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteFromVPN3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        AddRoute(master_, repr.str(), "target:64512:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteExists(master_, repr.str());
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteNoExists(master_, repr.str());
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Basic - RD is preserved in VPN.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteToVPN1) {
    ostringstream repr;
    repr << "3-10.1.1.1:65535-65536-192.1.1.1";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different RDs result in different routes in VRF.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteToVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteExists(blue_, repr.str());
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535-65536-192.1.1.1";
        VerifyRouteNoExists(blue_, repr.str());
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different IP addresses result in different routes in VPN.
//
TEST_F(EvpnTableInclusiveMulticastTest, ReplicateRouteToVPN3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteExists(blue_, repr.str());
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535-65536-192.1.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

class EvpnTableSegmentTest : public EvpnTableTest {
};

TEST_F(EvpnTableSegmentTest, AllocEntryStr1) {
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableSegmentTest, AllocEntryStr2) {
    string prefix_str(
        "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-2001:db8:0:9::1");
    auto_ptr<DBEntry> route = master_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(EvpnTableSegmentTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1";
    AddRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(master_, repr.str());
}

// Prefixes differ only in the IP address field of the RD.
TEST_F(EvpnTableSegmentTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1." << idx << ":65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}


// Prefixes differ only in the esi field.
TEST_F(EvpnTableSegmentTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-192.1.1.1";
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-192.1.1.1";
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-192.1.1.1";
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:" << hex << idx << "-192.1.1.1";
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

// Prefixes differ only in the IP address field.
TEST_F(EvpnTableSegmentTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1." << idx;
        AddRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1." << idx;
        VerifyRouteExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    VerifyTablePartitionNonEmpty(master_, 0);
    VerifyTablePartitionEmpty(master_, 1, DB::PartitionCount());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1." << idx;
        DelRoute(master_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "4-10.1.1.1:65535-";
        repr << "00:01:02:03:04:05:06:07:08:09-192.1.1." << idx;
        VerifyRouteNoExists(master_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Route is not replicated from VPN.
//
TEST_F(EvpnTableSegmentTest, ReplicateRouteFromVPN) {
    ostringstream repr;
    repr << "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1";
    AddRoute(master_, repr.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());

    DelRoute(master_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Route is not replicated to VPN.
//
TEST_F(EvpnTableSegmentTest, ReplicateRouteToVPN) {
    ostringstream repr;
    repr << "4-10.1.1.1:65535-00:01:02:03:04:05:06:07:08:09-192.1.1.1";
    AddRoute(blue_, repr.str(), "target:64512:1");
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(master_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<EvpnManager>(
        boost::factory<EvpnManagerMock *>());
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
