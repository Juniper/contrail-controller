/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/ermvpn/ermvpn_table.h"

#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_multicast.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

using namespace std;
using namespace boost;

class McastTreeManagerMock : public McastTreeManager {
public:
    McastTreeManagerMock(ErmVpnTable *table) : McastTreeManager(table) {
    }
    ~McastTreeManagerMock() { }

    virtual void Initialize() { }
    virtual void Terminate() { }

    virtual UpdateInfo *GetUpdateInfo(ErmVpnRoute *route) { return NULL; }

private:
};

static const int kRouteCount = 8;

class ErmVpnTableTest : public ::testing::Test {
protected:
    ErmVpnTableTest()
        : server_(&evm_), blue_(NULL) {
    }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        adc_notification_ = 0;
        del_notification_ = 0;

        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance));
        blue_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "blue", "target:65412:1", "target:65412:1", "blue", 1));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(master_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(blue_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        blue_ = static_cast<ErmVpnTable *>(
            server_.database()->FindTable("blue.ermvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::ERMVPN, blue_->family());
        master_ = static_cast<ErmVpnTable *>(
            server_.database()->FindTable("bgp.ermvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::ERMVPN, master_->family());

        tid_ = blue_->Register(
            boost::bind(&ErmVpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(ErmVpnTable *table, string prefix_str,
            string rtarget_str = "", string source_rd_str = "",
            int virtual_network_index = -1) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));

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
        addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        addReq.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        addReq.data.reset(new ErmVpnTable::RequestData(attr, 0, 0));
        table->Enqueue(&addReq);
    }

    void DelRoute(ErmVpnTable *table, string prefix_str) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        delReq.key.reset(new ErmVpnTable::RequestKey(prefix, NULL));
        table->Enqueue(&delReq);
    }

    ErmVpnRoute *FindRoute(ErmVpnTable *table, string prefix_str) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        ErmVpnTable::RequestKey key(prefix, NULL);
        ErmVpnRoute *rt = dynamic_cast<ErmVpnRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyRouteExists(ErmVpnTable *table, string prefix_str,
            size_t count = 1) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        ErmVpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) != NULL);
        ErmVpnRoute *rt = dynamic_cast<ErmVpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(count, rt->count());
    }

    void VerifyRouteNoExists(ErmVpnTable *table, string prefix_str) {
        ErmVpnPrefix prefix(ErmVpnPrefix::FromString(prefix_str));
        ErmVpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) == NULL);
        ErmVpnRoute *rt = static_cast<ErmVpnRoute *>(table->Find(&key));
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
    ErmVpnTable *master_;
    ErmVpnTable *blue_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(ErmVpnTableTest, TreeManager) {
    TASK_UTIL_EXPECT_TRUE(master_->GetTreeManager() == NULL);
    TASK_UTIL_EXPECT_TRUE(blue_->GetTreeManager() != NULL);
}

class ErmVpnTableNativeTest : public ErmVpnTableTest {
};

TEST_F(ErmVpnTableNativeTest, AllocEntryStr) {
    string prefix_str("0-10.1.1.1:65535-0.0.0.0,224.1.2.3,192.168.1.1");
    std::auto_ptr<DBEntry> route = blue_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(ErmVpnTableNativeTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "0-10.1.1.1:65535-0.0.0.0,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());

    ErmVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::ErmVpn, rt->Safi());
    TASK_UTIL_EXPECT_EQ(BgpAf::Mcast, rt->XmppSafi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Prefixes differ only in the IP address field of the RD.
//
TEST_F(ErmVpnTableNativeTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1." << idx << ":65535-0.0.0.0,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the group field.
//
TEST_F(ErmVpnTableNativeTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the source field.
//
TEST_F(ErmVpnTableNativeTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(ErmVpnTableNativeTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "0-10.1.1.1:65535-0.0.0.0,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

class ErmVpnTableLocalTest : public ErmVpnTableTest {
};

TEST_F(ErmVpnTableLocalTest, AllocEntryStr) {
    string prefix_str("1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    std::auto_ptr<DBEntry> route = blue_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(ErmVpnTableLocalTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    ErmVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::ErmVpn, rt->Safi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Prefixes differ only in the IP address field of the RD.
//
TEST_F(ErmVpnTableLocalTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the group field.
//
TEST_F(ErmVpnTableLocalTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the source field.
//
TEST_F(ErmVpnTableLocalTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the router-id field.
//
TEST_F(ErmVpnTableLocalTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(ErmVpnTableLocalTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "1-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

//
// Basic - RD in VRF is null.
//
TEST_F(ErmVpnTableLocalTest, ReplicateRouteFromVPN1) {
    ostringstream repr1, repr2;
    repr1 << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "1-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different RouterIds result in different routes in VRF.
//
TEST_F(ErmVpnTableLocalTest, RouteReplicateFromVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "1-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "1-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "1-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different RDs result in different paths for same route in VRF.
//
TEST_F(ErmVpnTableLocalTest, RouteReplicateFromVPN3) {
    ostringstream repr2;
    repr2 << "1-0:0-20.1.1.1,224.168.1.255,192.168.1.1";

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
        task_util::WaitForIdle();
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str(), idx);
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    ErmVpnRoute *rt = FindRoute(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(kRouteCount, rt->count());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "1-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
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
// Incorrect OriginVn prevents replication to VRF, even if RT is fine.
//
TEST_F(ErmVpnTableLocalTest, ReplicateRouteFromVPN4) {
    ostringstream repr1, repr2;
    repr1 << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "1-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1", "", 1001);
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

class ErmVpnTableGlobalTest : public ErmVpnTableTest {
};

TEST_F(ErmVpnTableGlobalTest, AllocEntryStr) {
    string prefix_str("2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0");
    std::auto_ptr<DBEntry> route = blue_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(ErmVpnTableGlobalTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);

    ErmVpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, rt->Afi());
    TASK_UTIL_EXPECT_EQ(BgpAf::ErmVpn, rt->Safi());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
}

//
// Prefixes differ only in the IP address field of the RD.
//
TEST_F(ErmVpnTableGlobalTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the group field.
//
TEST_F(ErmVpnTableGlobalTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the source field.
//
TEST_F(ErmVpnTableGlobalTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1.255,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the router-id field.
//
TEST_F(ErmVpnTableGlobalTest, AddDeleteMultipleRoute4) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(ErmVpnTableGlobalTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < DB::PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "2-10.1.1.1:65535-20.1.1.1,224.168.1." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();
}

//
// Basic - RD of VPN route is set to provided source RD.
//
TEST_F(ErmVpnTableGlobalTest, ReplicateRouteToVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    VerifyRouteExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());

    DelRoute(blue_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(master_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different RouterIds result in different VPN routes.
//
TEST_F(ErmVpnTableGlobalTest, RouteReplicateToVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(blue_, repr1.str(), "", "10.1.1.1:65535");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(blue_, repr1.str());
        VerifyRouteExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(blue_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(blue_, repr1.str());
        VerifyRouteNoExists(master_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Basic
//
TEST_F(ErmVpnTableGlobalTest, ReplicateRouteFromVPN1) {
    ostringstream repr1, repr2;
    repr1 << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "2-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1");
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

//
// Different RouterIds result in different routes in VRF.
//
TEST_F(ErmVpnTableGlobalTest, RouteReplicateFromVPN2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(kRouteCount, blue_->Size());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        DelRoute(master_, repr1.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1, repr2;
        repr1 << "2-10.1.1.1:65535-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        repr2 << "2-0:0-20.1.1." << idx << ",224.168.1.255,192.168.1.1";
        VerifyRouteNoExists(master_, repr1.str());
        VerifyRouteNoExists(blue_, repr2.str());
    }
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
}

//
// Different RDs result in different paths for same route in VRF.
//
TEST_F(ErmVpnTableGlobalTest, RouteReplicateFromVPN3) {
    ostringstream repr2;
    repr2 << "2-0:0-20.1.1.1,224.168.1.255,192.168.1.1";

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
        AddRoute(master_, repr1.str(), "target:65412:1");
        task_util::WaitForIdle();
        VerifyRouteExists(master_, repr1.str());
        VerifyRouteExists(blue_, repr2.str(), idx);
    }
    TASK_UTIL_EXPECT_EQ(kRouteCount, master_->Size());
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());

    ErmVpnRoute *rt = FindRoute(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(kRouteCount, rt->count());

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr1;
        repr1 << "2-10.1.1." << idx << ":65535-20.1.1.1,224.168.1.255,192.168.1.1";
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
// Incorrect OriginVn prevents replication to VRF, even if RT is fine.
//
TEST_F(ErmVpnTableGlobalTest, ReplicateRouteFromVPN4) {
    ostringstream repr1, repr2;
    repr1 << "2-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    repr2 << "2-0:0-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(master_, repr1.str(), "target:65412:1", "", 1001);
    task_util::WaitForIdle();
    VerifyRouteExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());

    DelRoute(master_, repr1.str());
    task_util::WaitForIdle();
    VerifyRouteNoExists(master_, repr1.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    VerifyRouteNoExists(blue_, repr2.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
}

class ErmVpnNoLeakTest : public ErmVpnTableTest {
protected:
    ErmVpnNoLeakTest() : ErmVpnTableTest(), pink_(NULL) {
    }

    virtual void SetUp() {
        ErmVpnTableTest::SetUp();

        ConcurrencyScope scope("bgp::Config");
        pink_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig("pink",
            "target:65412:2 target:65412:1", "target:65412:2", "pink", 2));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(pink_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        pink_ = static_cast<ErmVpnTable *>(
            server_.database()->FindTable("pink.ermvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::ERMVPN, pink_->family());
    }

    virtual void TearDown() {
        ErmVpnTableTest::TearDown();
    }

    ErmVpnTable *pink_;
    scoped_ptr<BgpInstanceConfig> pink_cfg_;
};

//
// Blue VRF route doesn't get leaked to pink VRF even though pink has an
// an import RT which is the same as the blue export RT.
//
TEST_F(ErmVpnNoLeakTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "1-10.1.1.1:65535-20.1.1.1,192.168.1.255,0.0.0.0";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1, master_->Size());
    VerifyRouteNoExists(pink_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, pink_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0, master_->Size());
    VerifyRouteNoExists(pink_, repr.str());
    TASK_UTIL_EXPECT_EQ(0, pink_->Size());
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<McastTreeManager>(
        boost::factory<McastTreeManagerMock *>());
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
