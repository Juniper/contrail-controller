/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/l3vpn/inetvpn_table.h"

#include <boost/bind.hpp>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

class BgpPeerMock : public IPeer {
public:
    virtual std::string ToString() const { return "test-peer"; }
    virtual std::string ToUVEKey() const { return "test-peer"; }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) { return true; }
    virtual BgpServer *server() { return NULL; }
    virtual IPeerClose *peer_close() { return NULL; }
    virtual IPeerDebugStats *peer_stats() { return NULL; }
    virtual bool IsReady() const { return true; }
    virtual bool IsXmppPeer() const { return false; }
    virtual void Close() { }
    BgpProto::BgpPeerType PeerType() const { return BgpProto::IBGP; }
    virtual uint32_t bgp_identifier() const { return 0; }
    virtual const std::string GetStateName() const { return "UNKNOWN"; }
    virtual void UpdateRefCount(int count) const { }
    virtual tbb::atomic<int> GetRefCount() const {
        tbb::atomic<int> count;
        count = 0;
        return count;
    }
private:
};

class InetVpnTableTest : public ::testing::Test {
protected:
    InetVpnTableTest()
            : server_(&evm_), rib_(NULL) {
    }

    virtual void SetUp() {
        master_cfg_.reset(new BgpInstanceConfig(BgpConfigManager::kMasterInstance));
        server_.routing_instance_mgr()->CreateRoutingInstance(
                master_cfg_.get());
        RoutingInstance *rti =
                server_.routing_instance_mgr()->GetRoutingInstance(
                    BgpConfigManager::kMasterInstance);
        ASSERT_TRUE(rti != NULL);

        rib_ = static_cast<InetVpnTable *>(rti->GetTable(Address::INETVPN));
        ASSERT_TRUE(rib_ != NULL);

        tid_ = rib_->Register(
            boost::bind(&InetVpnTableTest::VpnTableListener, this, _1, _2));
    }

    virtual void TearDown() {
        rib_->Unregister(tid_);
        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        task_util::WaitForIdle();
    }

    void VpnTableListener(DBTablePartBase *root, DBEntryBase *entry) {
        bool del_notify = entry->IsDeleted();
        if (del_notify)
            del_notification_++;
        else
            adc_notification_++;
    }

    EventManager evm_;
    BgpServer server_;
    InetVpnTable *rib_;
    DBTableBase::ListenerId tid_;
    std::auto_ptr<BgpInstanceConfig> master_cfg_;

    tbb::atomic<long> adc_notification_;
    tbb::atomic<long> del_notification_;
};

TEST_F(InetVpnTableTest, AllocEntryStr) {
    std::string prefix_str("123:456:192.168.24.0/24");
    std::auto_ptr<DBEntry> route = rib_->AllocEntryStr(prefix_str);
    EXPECT_EQ(prefix_str, route->ToString());
}

TEST_F(InetVpnTableTest, Basic) {
    adc_notification_ = 0;
    del_notification_ = 0;
    // Create a route prefix
    InetVpnPrefix prefix(InetVpnPrefix::FromString("123:456:192.168.24.0/24"));

    // Create a set of route attributes
    BgpAttrSpec attrs;

    EXPECT_EQ(rib_, server_.database()->FindTable("bgp.l3vpn.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new InetVpnTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_->Enqueue(&addReq);

    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification_, 1);

    InetVpnTable::RequestKey key(prefix, NULL);
    Route *rt_entry = static_cast<Route *>(rib_->Find(&key));
    EXPECT_TRUE(rt_entry != NULL);


    // Enqueue the delete
    DBRequest delReq;
    delReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);

    task_util::WaitForIdle();

    EXPECT_EQ(del_notification_, 1);

    rt_entry = static_cast<Route *>(rib_->Find(&key));
    EXPECT_TRUE(rt_entry == NULL);
}


TEST_F(InetVpnTableTest, DupDelete) {
    adc_notification_ = 0;
    del_notification_ = 0;
    // Create a route prefix
    InetVpnPrefix prefix(InetVpnPrefix::FromString("123:456:192.168.24.0/24"));

    // Create a set of route attributes
    BgpAttrSpec attrs;

    EXPECT_EQ(rib_, server_.database()->FindTable("bgp.l3vpn.0"));

    // Enqueue the update
    DBRequest addReq;
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    BgpAttrPtr attr = server_.attr_db()->Locate(attrs);
    addReq.data.reset(new InetVpnTable::RequestData(attr, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_->Enqueue(&addReq);
    task_util::WaitForIdle();

    EXPECT_EQ(adc_notification_, 1);

    InetVpnTable::RequestKey key(prefix, NULL);
    Route *rt_entry = static_cast<Route *>(rib_->Find(&key));
    EXPECT_TRUE(rt_entry != NULL);


    rt_entry->SetState(rib_, tid_, NULL);

    // Enqueue the delete
    DBRequest delReq;
    delReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);
    task_util::WaitForIdle();
    EXPECT_EQ(del_notification_, 1);

    // Enqueue a duplicate the delete
    delReq.key.reset(new InetVpnTable::RequestKey(prefix, NULL));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);
    task_util::WaitForIdle();
    EXPECT_EQ(del_notification_, 1);

    rt_entry = static_cast<Route *>(rib_->Find(&key));
    EXPECT_TRUE(rt_entry != NULL);

    rt_entry->ClearState(rib_, tid_);
    task_util::WaitForIdle();

    rt_entry = static_cast<Route *>(rib_->Find(&key));
    EXPECT_TRUE(rt_entry == NULL);
}

TEST_F(InetVpnTableTest, TableNotification) {
    BgpPeerMock peer1;
    BgpPeerMock peer2;
    BgpPeerMock peer3;

    adc_notification_ = 0;
    del_notification_ = 0;
    // Create a route prefix
    InetVpnPrefix prefix(InetVpnPrefix::FromString("123:456:192.168.24.0/24"));

    // Create a set of route attributes
    BgpAttrSpec attrs;

    EXPECT_EQ(rib_, server_.database()->FindTable("bgp.l3vpn.0"));

    // Enqueue the update
    BgpAttrDB *db = server_.attr_db();
    DBRequest addReq;

    addReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer1));
    BgpAttr *attr1 = new BgpAttr(db, attrs);
    attr1->set_local_pref(10);

    addReq.data.reset(new InetVpnTable::RequestData(attr1, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(1, adc_notification_);

    InetVpnTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(static_cast<BgpRoute *>(rib_->Find(&key)) != NULL);
    BgpRoute *rt = static_cast<BgpRoute *>(rib_->Find(&key));
    TASK_UTIL_EXPECT_EQ(rt->FindPath(BgpPath::BGP_XMPP, &peer1, 0),
                                     rt->BestPath());

    //
    // Add another non-best path and make sure that notification is still
    // received
    //
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer2));
    BgpAttr *attr2 = new BgpAttr(db, attrs);
    attr2->set_local_pref(5);

    addReq.data.reset(new InetVpnTable::RequestData(attr2, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(2, adc_notification_);
    TASK_UTIL_EXPECT_EQ(rt->FindPath(BgpPath::BGP_XMPP, &peer1, 0),
                        rt->BestPath());

    //
    // Add new best path and make sure that notification is still received
    //
    addReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer3));
    BgpAttr *attr3 = new BgpAttr(db, attrs);
    attr3->set_local_pref(15);

    addReq.data.reset(new InetVpnTable::RequestData(attr3, 0, 20));
    addReq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rib_->Enqueue(&addReq);
    TASK_UTIL_EXPECT_EQ(3, adc_notification_);
    TASK_UTIL_EXPECT_EQ(rt->FindPath(BgpPath::BGP_XMPP, &peer3, 0),
                        rt->BestPath());

    //
    // Delete all the paths
    //
    DBRequest delReq;

    delReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer3));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(4, adc_notification_);
    TASK_UTIL_EXPECT_EQ(rt->FindPath(BgpPath::BGP_XMPP, &peer1, 0),
                                     rt->BestPath());

    delReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer2));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(5, adc_notification_);
    TASK_UTIL_EXPECT_EQ(rt->FindPath(BgpPath::BGP_XMPP, &peer1, 0),
                                     rt->BestPath());

    delReq.key.reset(new InetVpnTable::RequestKey(prefix, &peer1));
    delReq.oper = DBRequest::DB_ENTRY_DELETE;
    rib_->Enqueue(&delReq);
    TASK_UTIL_EXPECT_EQ(5, adc_notification_);
    TASK_UTIL_EXPECT_EQ(1, del_notification_);

    //
    // Make sure that the route itself is gone by now as all the paths have
    // been deleted
    //
    InetVpnTable::RequestKey key2(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(static_cast<BgpRoute *>(rib_->Find(&key2)) == NULL);
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
