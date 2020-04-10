/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_multicast.h"

#include "base/task_annotations.h"
#include "bgp/bgp_update.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"

using namespace std;
using namespace boost;

class XmppPeerMock : public IPeer {
public:
    XmppPeerMock(BgpServer *server, string address_str)
        : server_(server),
          address_str_(address_str),
          label_block_(new LabelBlock(1000, 1500 -1)) {
        boost::system::error_code ec;
        BgpAttrSpec attr_spec;
        address_ = Ip4Address::from_string(address_str.c_str(), ec);
        BgpAttrNextHop nexthop(address_.to_ulong());
        attr_spec.push_back(&nexthop);
        BgpAttrLabelBlock label_block(label_block_);
        attr_spec.push_back(&label_block);
        ExtCommunitySpec ext;
        TunnelEncap tunnel(TunnelEncapType::GRE);
        ext.communities.push_back(tunnel.GetExtCommunityValue());
        attr_spec.push_back(&ext);
        attr = server_->attr_db()->Locate(attr_spec);
    }
    virtual ~XmppPeerMock() { }

    void AddRoute(ErmVpnTable *table, string group_str, string source_str) {
        boost::system::error_code ec;
        RouteDistinguisher rd(address_.to_ulong(), 65535);
        Ip4Address group = Ip4Address::from_string(group_str.c_str(), ec);
        Ip4Address source = Ip4Address::from_string(source_str.c_str(), ec);

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        ErmVpnPrefix prefix(ErmVpnPrefix::NativeRoute, rd, group, source);
        req.key.reset(new ErmVpnTable::RequestKey(prefix, this));
        req.data.reset(new ErmVpnTable::RequestData(attr, 0, 0));
        table->Enqueue(&req);
    }

    void AddRoute(ErmVpnTable *table, string group_str) {
        AddRoute(table, group_str, "0.0.0.0");
    }

    void DelRoute(ErmVpnTable *table, string group_str, string source_str) {
        boost::system::error_code ec;
        RouteDistinguisher rd(address_.to_ulong(), 65535);
        Ip4Address group = Ip4Address::from_string(group_str.c_str(), ec);
        Ip4Address source = Ip4Address::from_string(source_str.c_str(), ec);

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        ErmVpnPrefix prefix(ErmVpnPrefix::NativeRoute, rd, group, source);
        req.key.reset(new ErmVpnTable::RequestKey(prefix, this));
        table->Enqueue(&req);
    }

    void DelRoute(ErmVpnTable *table, string group_str) {
        DelRoute(table, group_str, "0.0.0.0");
    }

    virtual const std::string &ToString() const { return address_str_; }
    virtual const std::string &ToUVEKey() const { return address_str_; }
    virtual BgpServer *server() { return server_; }
    virtual BgpServer *server() const { return server_; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerClose *peer_close() const { return NULL; }
    virtual void UpdateCloseRouteStats(Address::Family family,
        const BgpPath *old_path, uint32_t path_flags) const {
    }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual const IPeerDebugStats *peer_stats() const { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return true; }
    virtual bool IsRegistrationRequired() const { return false; }
    virtual void Close(bool graceful) { }
    virtual BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return address_.to_ulong(); }
    virtual const std::string GetStateName() const { return ""; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual void UpdateTotalPathCount(int count) const { }
    virtual int GetTotalPathCount() const { return 0; }
    virtual bool IsAs4Supported() const { return false; }
    virtual void UpdatePrimaryPathCount(int count,
        Address::Family family) const { }
    virtual int GetPrimaryPathCount() const { return 0; }
    virtual void ProcessPathTunnelEncapsulation(const BgpPath *path,
        BgpAttr *attr, ExtCommunityDB *extcomm_db, const BgpTable *table)
        const {
    }
    virtual const std::vector<std::string> GetDefaultTunnelEncap(
        Address::Family family) const {
        return std::vector<std::string>();
    }
    virtual void MembershipRequestCallback(BgpTable *table) { }
    virtual bool MembershipPathCallback(DBTablePartBase *tpart,
        BgpRoute *route, BgpPath *path) { return false; }
    virtual bool CanUseMembershipManager() const { return true; }
    virtual bool IsInGRTimerWaitState() const { return false; }

private:
    BgpServer *server_;
    string address_str_;
    Ip4Address address_;
    LabelBlockPtr label_block_;
    BgpAttrPtr attr;
};

class BgpMulticastTest : public ::testing::Test {
protected:
    static const int kPeerCount = 1 + McastTreeManager::kDegree +
        McastTreeManager::kDegree * McastTreeManager::kDegree;
    static const int kEvenPeerCount = (kPeerCount + 1) / 2;
    static const int kOddPeerCount = kPeerCount / 2;

    BgpMulticastTest() : server_(&evm_) { }

    virtual void SetUp() {
        ConcurrencyScope scope("bgp::Config");

        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance, "", ""));
        red_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "red", "target:1.2.3.4:1", "target:1.2.3.4:1"));
        green_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            "green", "target:1.2.3.4:2", "target:1.2.3.4:2"));

        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Stop();
        server_.routing_instance_mgr()->CreateRoutingInstance(red_cfg_.get());
        server_.routing_instance_mgr()->CreateRoutingInstance(green_cfg_.get());
        scheduler->Start();
        task_util::WaitForIdle();

        red_table_ = static_cast<ErmVpnTable *>(
            server_.database()->FindTable("red.ermvpn.0"));
        red_tm_ = red_table_->tree_manager_;
        TASK_UTIL_EXPECT_EQ(red_table_, red_tm_->table_);
        TASK_UTIL_EXPECT_EQ(red_table_->PartitionCount(),
                            (int)red_tm_->partitions_.size());
        TASK_UTIL_EXPECT_NE(-1, red_tm_->listener_id_);

        green_table_ = static_cast<ErmVpnTable *>(
            server_.database()->FindTable("green.ermvpn.0"));
        green_tm_ = green_table_->tree_manager_;
        TASK_UTIL_EXPECT_EQ(green_table_, green_tm_->table_);
        TASK_UTIL_EXPECT_EQ(green_table_->PartitionCount(),
                            (int)green_tm_->partitions_.size());
        TASK_UTIL_EXPECT_NE(-1, green_tm_->listener_id_);

        CreatePeers();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        server_.Shutdown();
        task_util::WaitForIdle();
        STLDeleteValues(&peers_);
    }

    void CreatePeers() {
        for (int idx = 0; idx < kPeerCount; idx++) {
            std::ostringstream repr;
            repr << "10.1.1." << (idx+1);
            XmppPeerMock *peer = new XmppPeerMock(&server_, repr.str());
            peers_.push_back(peer);
        }
    }

    void VerifyRoute(ErmVpnTable *table, string group_str, string source_str) {
        boost::system::error_code ec;
        Ip4Address group = Ip4Address::from_string(group_str.c_str(), ec);
        Ip4Address source = Ip4Address::from_string(source_str.c_str(), ec);

        ErmVpnPrefix prefix(ErmVpnPrefix::LocalTreeRoute,
                            RouteDistinguisher::kZeroRd, group, source);
        ErmVpnTable::RequestKey key(prefix, NULL);
        ErmVpnRoute *rt = dynamic_cast<ErmVpnRoute *>(table->Find(&key));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        const BgpAttr* attr = rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_TRUE(attr != NULL);
        TASK_UTIL_EXPECT_TRUE(attr->ext_community() != NULL);
        std::vector<string> encaps = attr->ext_community()->GetTunnelEncap();
        TASK_UTIL_EXPECT_TRUE(encaps.size() != 0);
        ErmVpnPrefix prefix2(ErmVpnPrefix::GlobalTreeRoute,
                            RouteDistinguisher::kZeroRd, group, source);
        ErmVpnTable::RequestKey key2(prefix2, NULL);
        rt = dynamic_cast<ErmVpnRoute *>(table->Find(&key2));
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        attr = rt->BestPath()->GetAttr();
        TASK_UTIL_EXPECT_TRUE(attr != NULL);
        TASK_UTIL_EXPECT_TRUE(attr->ext_community() != NULL);
        encaps = attr->ext_community()->GetTunnelEncap();
        TASK_UTIL_EXPECT_TRUE(encaps.size() != 0);
    }

    void VerifyRoute(ErmVpnTable *table, string group_str) {
        VerifyRoute(table, group_str, "0.0.0.0");
    }

    void AddRoutePeers(ErmVpnTable *table,
            string group_str, string source_str, bool even, bool odd) {
        for (vector<XmppPeerMock *>::iterator it = peers_.begin();
             it != peers_.end(); ++it) {
            if ((((it - peers_.begin()) % 2 == 0) == even) ||
                (((it - peers_.begin()) % 2 == 1) == odd)) {
                (*it)->AddRoute(table, group_str, source_str);
            }
        }
    }

    void AddRouteAllPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        AddRoutePeers(table, group_str, source_str, true, true);
    }

    void AddRouteAllPeers(ErmVpnTable *table, string group_str) {
        AddRouteAllPeers(table, group_str, "0.0.0.0");
    }

    void AddRouteEvenPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        AddRoutePeers(table, group_str, source_str, true, false);
    }

    void AddRouteEvenPeers(ErmVpnTable *table, string group_str) {
        AddRouteEvenPeers(table, group_str, "0.0.0.0");
    }

    void AddRouteOddPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        AddRoutePeers(table, group_str, source_str, false, true);
    }

    void AddRouteOddPeers(ErmVpnTable *table, string group_str) {
        AddRouteOddPeers(table, group_str, "0.0.0.0");
    }

    void DelRoutePeers(ErmVpnTable *table,
            string group_str, string source_str, bool even, bool odd) {
        for (vector<XmppPeerMock *>::iterator it = peers_.begin();
             it != peers_.end(); ++it) {
            if ((((it - peers_.begin()) % 2 == 0) == even) ||
                (((it - peers_.begin()) % 2 == 1) == odd)) {
                (*it)->DelRoute(table, group_str, source_str);
            }
        }
    }

    void DelRouteAllPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        DelRoutePeers(table, group_str, source_str, true, true);
    }

    void DelRouteAllPeers(ErmVpnTable *table, string group_str) {
        DelRouteAllPeers(table, group_str, "0.0.0.0");
    }

    void DelRouteEvenPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        DelRoutePeers(table, group_str, source_str, true, false);
    }

    void DelRouteEvenPeers(ErmVpnTable *table, string group_str) {
        DelRouteEvenPeers(table, group_str, "0.0.0.0");
    }

    void DelRouteOddPeers(ErmVpnTable *table,
            string group_str, string source_str) {
        DelRoutePeers(table, group_str, source_str, false, true);
    }

    void DelRouteOddPeers(ErmVpnTable *table, string group_str) {
        DelRouteOddPeers(table, group_str, "0.0.0.0");
    }

    void VerifyRouteCount(ErmVpnTable *table, size_t count) {
        TASK_UTIL_EXPECT_EQ(count, table->Size());
    }

    void VerifySGCount(McastTreeManager *tm, size_t count) {
        size_t total = 0;
        for (McastTreeManager::PartitionList::iterator it =
                tm->partitions_.begin();
             it != tm->partitions_.end(); ++it) {
            total += (*it)->sg_list_.size();
        }
        TASK_UTIL_EXPECT_EQ(count, total);
    }

    void VerifyForwarderProperties(ErmVpnTable *table,
            McastForwarder *forwarder) {
        ConcurrencyScope scope("db::DBTable");

        EXPECT_GE(forwarder->label(), 1000U);
        EXPECT_LE(forwarder->label(), 1499U);
        EXPECT_GE(forwarder->tree_links_.size(), 1U);
        EXPECT_LE(forwarder->tree_links_.size(),
                  static_cast<size_t>(McastTreeManager::kDegree + 1));
        TASK_UTIL_EXPECT_TRUE(forwarder->route() != NULL);
        TASK_UTIL_EXPECT_EQ(1U, forwarder->route()->count());

        boost::scoped_ptr<UpdateInfo> uinfo(forwarder->GetUpdateInfo(table));
        TASK_UTIL_EXPECT_TRUE(uinfo.get() != NULL);
        BgpOList *olist = uinfo->roattr.attr()->olist().get();
        TASK_UTIL_EXPECT_TRUE(olist != NULL);
        EXPECT_GE(olist->elements().size(), 1U);
        EXPECT_LE(olist->elements().size(),
                  static_cast<size_t>(McastTreeManager::kDegree + 1));
    }

    void VerifyOnlyForwarderProperties(ErmVpnTable *table,
            McastForwarder *forwarder) {
        ConcurrencyScope scope("db::DBTable");

        TASK_UTIL_EXPECT_NE(0U, forwarder->label());
        TASK_UTIL_EXPECT_EQ(0U, forwarder->tree_links_.size());
        TASK_UTIL_EXPECT_TRUE(forwarder->route() != NULL);
        TASK_UTIL_EXPECT_EQ(1U, forwarder->route()->count());

        boost::scoped_ptr<UpdateInfo> uinfo(forwarder->GetUpdateInfo(table));
        TASK_UTIL_EXPECT_TRUE(uinfo.get() != NULL);
    }

    void VerifyForwarderLinks(McastForwarder *forwarder) {
        BGP_DEBUG_UT("    McastForwarder     " << forwarder->ToString());
        for (McastForwarderList::iterator it = forwarder->tree_links_.begin();
             it != forwarder->tree_links_.end(); ++it) {
            BGP_DEBUG_UT("      Link to " << (*it)->ToString());
        }
    }

    void VerifyForwarderCount(McastTreeManager *tm,
            string group_str, string source_str, size_t count) {
        boost::system::error_code ec;
        Ip4Address group = Ip4Address::from_string(group_str.c_str(), ec);
        Ip4Address source = Ip4Address::from_string(source_str.c_str(), ec);

        BGP_DEBUG_UT("Table " << tm->table_->name());
        for (McastTreeManager::PartitionList::iterator it =
             tm->partitions_.begin(); it != tm->partitions_.end(); ++it) {
            McastSGEntry *sg_entry = (*it)->FindSGEntry(group, source);
            if (sg_entry) {
                McastSGEntry::ForwarderSet *forwarders =
                    sg_entry->forwarder_sets_[McastTreeManager::LevelNative];
                TASK_UTIL_EXPECT_EQ(count, forwarders->size());
                if (forwarders->size() > 1) {

                    BGP_DEBUG_UT("  McastSGEntry " << sg_entry->ToString() <<
                                 "  partition " << (*it)->part_id_);
                }
                for (McastSGEntry::ForwarderSet::iterator it =
                     forwarders->begin(); it != forwarders->end(); ++it) {
                    if (forwarders->size() > 1) {
                        VerifyForwarderProperties(tm->table_, *it);
                        VerifyForwarderLinks(*it);
                    } else {
                        VerifyOnlyForwarderProperties(tm->table_, *it);
                    }
                }
                return;
            }
        }
        TASK_UTIL_EXPECT_EQ(0U, count);
    }

    void VerifyForwarderCount(McastTreeManager *tm,
            string group_str, size_t count) {
        VerifyForwarderCount(tm, group_str, "0.0.0.0", count);
    }

    size_t VerifyTreeUpdateCount(McastTreeManager *tm) {
        size_t total = 0;
        for (int idx = 0; idx < ErmVpnTable::kPartitionCount; idx++) {
            total += tm->partitions_[idx]->update_count_;
        }

        return total;
    }

    EventManager evm_;
    BgpServer server_;
    ErmVpnTable *red_table_;
    ErmVpnTable *green_table_;
    McastTreeManager *red_tm_;
    McastTreeManager *green_tm_;
    scoped_ptr<BgpInstanceConfig> master_cfg_;
    scoped_ptr<BgpInstanceConfig> red_cfg_;
    scoped_ptr<BgpInstanceConfig> green_cfg_;
    std::vector<XmppPeerMock *> peers_;
};

TEST_F(BgpMulticastTest, Noop) {
}

TEST_F(BgpMulticastTest, Basic) {
    peers_[0]->AddRoute(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 3);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 1);

    peers_[1]->AddRoute(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 4);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 2);
    VerifyRoute(red_table_, "192.168.1.255");

    peers_[0]->DelRoute(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 3);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 1);

    peers_[1]->DelRoute(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroup) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    VerifyRoute(red_table_, "192.168.1.255");

    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroupAddDel) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroupDuplicateAdd) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    VerifyRoute(red_table_, "192.168.1.255");

    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroupIncrementalAdd) {
    AddRouteEvenPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kEvenPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kEvenPeerCount);

    AddRouteOddPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroupIncrementalDel) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteEvenPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kOddPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kOddPeerCount);

    DelRouteOddPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, SingleGroupRepeatedDelAdd) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    for (int idx = 0; idx < 5; idx++) {
        DelRouteEvenPeers(red_table_, "192.168.1.255");
        task_util::WaitForIdle();
        VerifyRouteCount(red_table_, kOddPeerCount + 2);
        VerifySGCount(red_tm_, 1);
        VerifyForwarderCount(red_tm_, "192.168.1.255", kOddPeerCount);

        AddRouteEvenPeers(red_table_, "192.168.1.255");
        task_util::WaitForIdle();
        VerifyRouteCount(red_table_, kPeerCount + 2);
        VerifySGCount(red_tm_, 1);
        VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

        DelRouteOddPeers(red_table_, "192.168.1.255");
        task_util::WaitForIdle();
        VerifyRouteCount(red_table_, kEvenPeerCount + 2);
        VerifySGCount(red_tm_, 1);
        VerifyForwarderCount(red_tm_, "192.168.1.255", kEvenPeerCount);

        AddRouteOddPeers(red_table_, "192.168.1.255");
        task_util::WaitForIdle();
        VerifyRouteCount(red_table_, kPeerCount + 2);
        VerifySGCount(red_tm_, 1);
        VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    }

    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleGroup) {
    AddRouteAllPeers(red_table_, "192.168.1.253");
    AddRouteAllPeers(red_table_, "192.168.1.254");
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    VerifyRoute(red_table_, "192.168.1.253");
    VerifyRoute(red_table_, "192.168.1.254");
    VerifyRoute(red_table_, "192.168.1.255");

    DelRouteAllPeers(red_table_, "192.168.1.253");
    DelRouteAllPeers(red_table_, "192.168.1.254");
    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.253", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleGroupDuplicateAdd) {
    AddRouteAllPeers(red_table_, "192.168.1.253");
    AddRouteAllPeers(red_table_, "192.168.1.254");
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    AddRouteAllPeers(red_table_, "192.168.1.253");
    AddRouteAllPeers(red_table_, "192.168.1.254");
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.253");
    DelRouteAllPeers(red_table_, "192.168.1.254");
    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.253", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleGroupIncrementalAdd) {
    AddRouteOddPeers(red_table_, "192.168.1.253");
    AddRouteOddPeers(red_table_, "192.168.1.254");
    AddRouteOddPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kOddPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kOddPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kOddPeerCount);

    AddRouteEvenPeers(red_table_, "192.168.1.253");
    AddRouteEvenPeers(red_table_, "192.168.1.254");
    AddRouteEvenPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.253");
    DelRouteAllPeers(red_table_, "192.168.1.254");
    DelRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.253", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleGroupIncrementalDel) {
    AddRouteAllPeers(red_table_, "192.168.1.253");
    AddRouteAllPeers(red_table_, "192.168.1.254");
    AddRouteAllPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);

    DelRouteOddPeers(red_table_, "192.168.1.253");
    DelRouteOddPeers(red_table_, "192.168.1.254");
    DelRouteOddPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 3);
    VerifyForwarderCount(red_tm_, "192.168.1.253", kEvenPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kEvenPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kEvenPeerCount);

    DelRouteEvenPeers(red_table_, "192.168.1.253");
    DelRouteEvenPeers(red_table_, "192.168.1.254");
    DelRouteEvenPeers(red_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifySGCount(red_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.253", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleTableSingleGroup) {
    AddRouteAllPeers(red_table_, "192.168.1.255");
    AddRouteAllPeers(green_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, kPeerCount + 2);
    VerifyRouteCount(green_table_, kPeerCount + 2);
    VerifySGCount(red_tm_, 1);
    VerifySGCount(green_tm_, 1);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    VerifyForwarderCount(green_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.255");
    DelRouteAllPeers(green_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifyRouteCount(green_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifySGCount(green_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
    VerifyForwarderCount(green_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, MultipleTableMultipleGroup) {
    AddRouteAllPeers(red_table_, "192.168.1.254");
    AddRouteAllPeers(red_table_, "192.168.1.255");
    AddRouteAllPeers(green_table_, "192.168.1.254");
    AddRouteAllPeers(green_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 2 * (kPeerCount + 2));
    VerifyRouteCount(green_table_, 2 * (kPeerCount + 2));
    VerifySGCount(red_tm_, 2);
    VerifySGCount(green_tm_, 2);
    VerifyForwarderCount(red_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(red_tm_, "192.168.1.255", kPeerCount);
    VerifyForwarderCount(green_tm_, "192.168.1.254", kPeerCount);
    VerifyForwarderCount(green_tm_, "192.168.1.255", kPeerCount);

    DelRouteAllPeers(red_table_, "192.168.1.254");
    DelRouteAllPeers(red_table_, "192.168.1.255");
    DelRouteAllPeers(green_table_, "192.168.1.254");
    DelRouteAllPeers(green_table_, "192.168.1.255");
    task_util::WaitForIdle();
    VerifyRouteCount(red_table_, 0);
    VerifyRouteCount(green_table_, 0);
    VerifySGCount(red_tm_, 0);
    VerifySGCount(green_tm_, 0);
    VerifyForwarderCount(red_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(red_tm_, "192.168.1.255", 0);
    VerifyForwarderCount(green_tm_, "192.168.1.254", 0);
    VerifyForwarderCount(green_tm_, "192.168.1.255", 0);
}

TEST_F(BgpMulticastTest, TreeUpdateCompression) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    AddRouteAllPeers(red_table_, "192.168.1.255");
    scheduler->Start();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2U, VerifyTreeUpdateCount(red_tm_));

    scheduler->Stop();
    DelRouteOddPeers(red_table_, "192.168.1.255");
    scheduler->Start();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4U, VerifyTreeUpdateCount(red_tm_));

    scheduler->Stop();
    DelRouteEvenPeers(red_table_, "192.168.1.255");
    scheduler->Start();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(6U, VerifyTreeUpdateCount(red_tm_));
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
