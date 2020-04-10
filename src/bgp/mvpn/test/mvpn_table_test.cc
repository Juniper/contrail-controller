/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/mvpn/mvpn_table.h"


#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_multicast.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"

using namespace std;
using namespace boost;

static const int kRouteCount = 8;

class MvpnTableTest : public ::testing::Test {
protected:
    MvpnTableTest()
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

        blue_ = static_cast<MvpnTable *>(
            server_.database()->FindTable("blue.mvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::MVPN, blue_->family());
        master_ = static_cast<MvpnTable *>(
            server_.database()->FindTable("bgp.mvpn.0"));
        TASK_UTIL_EXPECT_EQ(Address::MVPN, master_->family());

        tid_ = blue_->Register(
            boost::bind(&MvpnTableTest::TableListener, this, _1, _2));
    }

    virtual void TearDown() {
        blue_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void AddRoute(MvpnTable *table, string prefix_str,
            string rtarget_str = "", string source_rd_str = "",
            int virtual_network_index = -1) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));

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
        addReq.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        addReq.data.reset(new MvpnTable::RequestData(attr, 0, 0));
        table->Enqueue(&addReq);
    }

    void DelRoute(MvpnTable *table, string prefix_str) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));

        DBRequest delReq;
        delReq.oper = DBRequest::DB_ENTRY_DELETE;
        delReq.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        table->Enqueue(&delReq);
    }

    MvpnRoute *FindRoute(MvpnTable *table, string prefix_str) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        MvpnTable::RequestKey key(prefix, NULL);
        MvpnRoute *rt = dynamic_cast<MvpnRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyRouteExists(MvpnTable *table, string prefix_str,
            size_t count = 1) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        MvpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) != NULL);
        MvpnRoute *rt = dynamic_cast<MvpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        TASK_UTIL_EXPECT_EQ(count, rt->count());
    }

    void VerifyRouteNoExists(MvpnTable *table, string prefix_str) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        MvpnTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_TRUE(table->Find(&key) == NULL);
        MvpnRoute *rt = static_cast<MvpnRoute *>(table->Find(&key));
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
    MvpnTable *master_;
    MvpnTable *blue_;
    DBTableBase::ListenerId tid_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
    scoped_ptr<BgpInstanceConfig> blue_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(MvpnTableTest, AllocEntryStr) {
    string prefix_str("3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1");
    std::auto_ptr<DBEntry> route = blue_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(MvpnTableTest, AddDeleteSingleRoute) {
    ostringstream repr;
    repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 1);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
}

//
// Prefixes differ only in the IP address field of the RD.
//
TEST_F(MvpnTableTest, AddDeleteMultipleRoute1) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535,192.168.1.1,224.1.2.3,9.8.7.6";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535,192.168.1.1,224.1.2.3,9.8.7.6";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535,192.168.1.1,224.1.2.3,9.8.7.6";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1." << idx << ":65535,192.168.1.1,224.1.2.3,9.8.7.6";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the group field.
//
TEST_F(MvpnTableTest, AddDeleteMultipleRoute2) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

//
// Prefixes differ only in the source field.
//
TEST_F(MvpnTableTest, AddDeleteMultipleRoute3) {
    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1." << idx;
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1." << idx;
        VerifyRouteExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(adc_notification_, kRouteCount);

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1." << idx;
        DelRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 1; idx <= kRouteCount; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1." << idx;
        VerifyRouteNoExists(blue_, repr.str());
    }
    TASK_UTIL_EXPECT_EQ(del_notification_, kRouteCount);
}

TEST_F(MvpnTableTest, CreateType4LeafADRoutePrefix) {
    ostringstream repr;
    repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2.3,192.168.1.1";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    MvpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));

    MvpnPrefix type4_prefix = blue_->CreateType4LeafADRoutePrefix(rt);
    const Ip4Address ip(Ip4Address::from_string("20.1.1.1"));
    type4_prefix.set_originator(ip);
    string prefix_str = type4_prefix.ToString();
    AddRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(2U, master_->Size());

    MvpnRoute *type4_rt = FindRoute(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(4, type4_rt->GetPrefix().type());
    TASK_UTIL_EXPECT_EQ(rt->GetPrefix().source(),
        type4_rt->GetPrefix().source());
    TASK_UTIL_EXPECT_EQ(rt->GetPrefix().group(),
        type4_rt->GetPrefix().group());

    DelRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 2);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
}

TEST_F(MvpnTableTest, CreateType3SPMSIRoutePrefix) {
    ostringstream repr;
    repr << "7-10.1.1.1:65535,12345,224.1.2.3,192.168.1.1";
    AddRoute(blue_, repr.str());
    task_util::WaitForIdle();
    VerifyRouteExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(adc_notification_, 1);
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    MvpnRoute *rt = FindRoute(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));

    MvpnPrefix type3_prefix = blue_->CreateType3SPMSIRoutePrefix(rt);
    string prefix_str = type3_prefix.ToString();
    AddRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(2U, master_->Size());

    MvpnRoute *type3_rt = FindRoute(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(3, type3_rt->GetPrefix().type());
    TASK_UTIL_EXPECT_EQ(rt->GetPrefix().source(),
        type3_rt->GetPrefix().source());
    TASK_UTIL_EXPECT_EQ(rt->GetPrefix().group(),
        type3_rt->GetPrefix().group());
    TASK_UTIL_EXPECT_EQ(blue_->server()->bgp_identifier(),
        rt->GetPrefix().originator().to_ulong());

    DelRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    DelRoute(blue_, repr.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(del_notification_, 2);
    VerifyRouteNoExists(blue_, repr.str());
    TASK_UTIL_EXPECT_EQ(0U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
}

TEST_F(MvpnTableTest, CreateType2RoutePrefix) {
    MvpnPrefix type2_prefix = blue_->CreateType2ADRoutePrefix();
    string prefix_str = type2_prefix.ToString();
    AddRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    MvpnRoute *rt = FindRoute(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(2, rt->GetPrefix().type());
    TASK_UTIL_EXPECT_EQ(blue_->server()->autonomous_system(),
        rt->GetPrefix().asn());

    DelRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(0U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
}

TEST_F(MvpnTableTest, CreateType1RoutePrefix) {
    MvpnPrefix type1_prefix = blue_->CreateType1ADRoutePrefix();
    string prefix_str = type1_prefix.ToString();
    AddRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(1U, master_->Size());

    MvpnRoute *rt = FindRoute(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(BgpAf::IPv4, BgpAf::FamilyToAfi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(BgpAf::MVpn, BgpAf::FamilyToSafi(blue_->family()));
    TASK_UTIL_EXPECT_EQ(1, rt->GetPrefix().type());
    TASK_UTIL_EXPECT_EQ(blue_->server()->bgp_identifier(),
        rt->GetPrefix().originator().to_ulong());

    DelRoute(blue_, prefix_str);
    task_util::WaitForIdle();
    VerifyRouteNoExists(blue_, prefix_str);
    TASK_UTIL_EXPECT_EQ(0U, blue_->Size());
    TASK_UTIL_EXPECT_EQ(0U, master_->Size());
}

TEST_F(MvpnTableTest, Hashing) {
    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        AddRoute(blue_, repr.str());
    }
    task_util::WaitForIdle();

    for (int idx = 0; idx < blue_->PartitionCount(); idx++) {
        DBTablePartition *tbl_partition =
            static_cast<DBTablePartition *>(blue_->GetTablePartition(idx));
        TASK_UTIL_EXPECT_NE(0U, tbl_partition->size());
    }

    for (int idx = 1; idx <= 255; idx++) {
        ostringstream repr;
        repr << "3-10.1.1.1:65535,9.8.7.6,224.1.2." << idx << ",192.168.1.1";
        DelRoute(blue_, repr.str());
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
