/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_update.h"

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/foreach.hpp>

#include <tbb/compat/condition_variable>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/message_builder.h"
#include "bgp/scheduling_group.h"
#include "bgp/inet/inet_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

class Condition {
public:
    Condition() : state_(false) { }
    void WaitAndClear() {
        tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
        while (!state_) {
            cond_var_.wait(lock);
        }
        state_ = false;
    }

    void Set() {
        {
            tbb::interface5::unique_lock<tbb::mutex> lock(mutex_);
            state_ = true;
        }
        cond_var_.notify_one();
    }

private:
    tbb::mutex mutex_;
    tbb::interface5::condition_variable cond_var_;
    bool state_;
};

static int gbl_index;

class BgpTestPeer : public IPeerUpdate {
public:
    BgpTestPeer() : index_(gbl_index++), count_(0) {
    }

    virtual ~BgpTestPeer() { }

    virtual std::string ToString() const {
        std::ostringstream repr;
        repr << "Peer" << index_;
        return repr.str();
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        count_++;
        bool send_block = block_set_.find(count_) != block_set_.end();
        if (send_block) {
            cond_var_.Set();
        }
        return !send_block;
    }

    void WriteActive(SchedulingGroupManager *mgr) {
        mgr->SendReady(this);
    }

    void WaitOnBlocked(SchedulingGroupManager *mgr) {
        // wait until the SendUpdate method returns false
        cond_var_.WaitAndClear();
        // wait until the SchedulingGroup code updates its state
        ConcurrencyScope scope("bgp::PeerMembership");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(!mgr->PeerGroup(this)->IsSendReady(this));
    }

    void set_block_at(int step) {
        block_set_.insert(step);
    }
    int update_count() const { return count_; }

private:
    int index_;
    std::set<int> block_set_;
    int count_;
    Condition cond_var_;
};

class MsgBuilderMock : public MessageBuilder {
public:
    class MessageMock : public Message {
    public:
        virtual bool AddRoute(const BgpRoute *route, const RibOutAttr* attr) {
            return true;
        }
        virtual void Finish() {
        }
        virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp) {
            return NULL;
        }
    };
    virtual Message *Create(const BgpTable *table,
                            const RibOutAttr *attr,
                            const BgpRoute *route) const {
        return new MessageMock();
    }
};

static RouteUpdate *BuildUpdate(BgpRoute *route, const RibOut &ribout,
        BgpAttrPtr attrp) {
    RouteUpdate *update = new RouteUpdate(route, RibOutUpdates::QUPDATE);
    UpdateInfoSList ulist;
    UpdateInfo *info = new UpdateInfo();
    info->roattr.set_attr(attrp);
    info->target = ribout.PeerSet();
    ulist->push_front(*info);
    update->SetUpdateInfo(ulist);
    return update;
}

static RouteUpdate *BuildWithdraw(BgpRoute *route, const RibOut &ribout) {
    DBState *state = route->GetState(ribout.table(), ribout.listener_id());
    RouteState *rs = dynamic_cast<RouteState *>(state);
    RouteUpdate *update = new RouteUpdate(route, RibOutUpdates::QUPDATE);
    UpdateInfoSList ulist;
    UpdateInfo *info = new UpdateInfo();
    info->target = ribout.PeerSet();
    ulist->push_front(*info);
    update->SetUpdateInfo(ulist);
    rs->MoveHistory(update);
    delete rs;
    return update;
}

class BgpUpdateTest : public ::testing::Test {
protected:
    static const int kPeerCount = 2;
    static const int kAttrCount = 20;
    BgpUpdateTest() 
        : server_(&evm_),
          inetvpn_table_(static_cast<InetVpnTable *>(db_.CreateTable("bgp.l3vpn.0"))),
          tbl1_(inetvpn_table_, &mgr_, RibExportPolicy()) {
        tbl1_.updates()->SetMessageBuilder(&builder_);
    }

    virtual void SetUp() {
        gbl_index = 0;
        for (int i = 0; i < kPeerCount; i++) {
            CreatePeer();
        }

        CreateAttrs();
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
    }

    void CreatePeer() {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);
        RibOutRegister(&tbl1_, peer);
        ASSERT_TRUE(tbl1_.GetSchedulingGroup() != NULL);        
    }

    void CreateAttrs() {
        BgpAttr *attr;

        attr = new BgpAttr(server_.attr_db());
        attr->set_med(101);
        a1_ = server_.attr_db()->Locate(attr);

        attr = new BgpAttr(server_.attr_db());
        attr->set_med(102);
        a2_ = server_.attr_db()->Locate(attr);

        attr = new BgpAttr(server_.attr_db());
        attr->set_med(103);
        a3_ = server_.attr_db()->Locate(attr);

        for (int i = 0; i < kAttrCount; i++) {
            attr = new BgpAttr(server_.attr_db());
            attr->set_med(1000 + i);
            attr_.push_back(server_.attr_db()->Locate(attr));
        }
    }

    void EnqueueUpdates(RibOut *ribout, vector<InetVpnRoute *> *routes,
            int count) {
        ConcurrencyScope scope("db::DBTable");
        assert(count <= kAttrCount);
        InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
        for (int i = 0; i < count; i++) {
            InetVpnRoute *rt = new InetVpnRoute(prefix);
            routes->push_back(rt);
            RouteUpdate *update = BuildUpdate(rt, *ribout, attr_[i]);
            ribout->updates()->Enqueue(rt, update);
        }
    }

    void EnqueueOneUpdate(RibOutUpdates *updates, BgpRoute *route,
            RouteUpdate *rt_update) {
        ConcurrencyScope scope("db::DBTable");
        updates->Enqueue(route, rt_update);
    }

    void DeleteRouteState(RibOut *ribout, BgpRoute *route) {
        ConcurrencyScope scope("db::DBTable");
        DBState *state = route->GetState(inetvpn_table_, ribout->listener_id());
        if (!state)
            return;
        RouteState *rs = dynamic_cast<RouteState *>(state);
        assert(rs);
        route->ClearState(inetvpn_table_, ribout->listener_id());
        delete rs;
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    InetVpnTable *inetvpn_table_;
    SchedulingGroupManager mgr_;
    MsgBuilderMock builder_;
    RibOut tbl1_;
    BgpAttrPtr a1_, a2_, a3_;
    std::vector<BgpAttrPtr> attr_;
    boost::ptr_vector<BgpTestPeer> peers_;
};

// Create a positive route update, observe that it gets sent.
// Then generate a corresponding delete.
TEST_F(BgpUpdateTest, Basic) {
    RibOutUpdates *updates = tbl1_.updates();
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, tbl1_, a1_);
    EnqueueOneUpdate(updates, &rt1, u1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_EQ(1, peers_[0].update_count());
    DBState *state = rt1.GetState(tbl1_.table(), tbl1_.listener_id());
    ASSERT_TRUE(state != NULL);
    RouteState *rs = static_cast<RouteState *>(state);
    const AdvertiseSList &adv_slist = rs->Advertised();
    TASK_UTIL_EXPECT_EQ(1, adv_slist->size());
    const AdvertiseInfo &ainfo = *adv_slist->begin();
    TASK_UTIL_EXPECT_TRUE(tbl1_.PeerSet() == ainfo.bitset);
    
    // delete
    u1 = BuildWithdraw(&rt1, tbl1_);
    EnqueueOneUpdate(updates, &rt1, u1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_EQ(2, peers_[0].update_count());
    state = rt1.GetState(tbl1_.table(), tbl1_.listener_id());
    TASK_UTIL_EXPECT_TRUE(state == NULL);

    // Cleanup RouteState on all routes.
    DeleteRouteState(&tbl1_, &rt1);
}

// 2. Verify that update with same attribute end up in the same packet.
TEST_F(BgpUpdateTest, UpdatePack) {
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, tbl1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, tbl1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, tbl1_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, tbl1_, a3_);
    
    TaskScheduler::GetInstance()->Stop();
    RibOutUpdates *updates = tbl1_.updates();
    EnqueueOneUpdate(updates, &rt1, u1);
    EnqueueOneUpdate(updates, &rt2, u2);
    EnqueueOneUpdate(updates, &rt3, u3);
    EnqueueOneUpdate(updates, &rt4, u4);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_EQ(3, peers_[0].update_count());

    // Cleanup RouteState on all routes.
    DeleteRouteState(&tbl1_, &rt1);
    DeleteRouteState(&tbl1_, &rt2);
    DeleteRouteState(&tbl1_, &rt3);
    DeleteRouteState(&tbl1_, &rt4);
}

// 3. Peer send blocks
TEST_F(BgpUpdateTest, SendBlock) {
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, tbl1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, tbl1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, tbl1_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, tbl1_, a3_);

    BgpTestPeer *peer1 = &peers_[1];
    peer1->set_block_at(1);

    TaskScheduler::GetInstance()->Stop();
    RibOutUpdates *updates = tbl1_.updates();
    EnqueueOneUpdate(updates, &rt1, u1);
    EnqueueOneUpdate(updates, &rt2, u2);
    EnqueueOneUpdate(updates, &rt3, u3);
    EnqueueOneUpdate(updates, &rt4, u4);
    TaskScheduler::GetInstance()->Start();

    peer1->WaitOnBlocked(&mgr_);
    peer1->WriteActive(&mgr_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_EQ(3, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(3, peers_[1].update_count());

    UpdateQueue *queue = tbl1_.updates()->queue(RibOutUpdates::QUPDATE);
    RibPeerSet &bitset = queue->tail_marker()->members;
    TASK_UTIL_EXPECT_TRUE(bitset == tbl1_.PeerSet());

    // Cleanup RouteState on all routes.
    DeleteRouteState(&tbl1_, &rt1);
    DeleteRouteState(&tbl1_, &rt2);
    DeleteRouteState(&tbl1_, &rt3);
    DeleteRouteState(&tbl1_, &rt4);
}

TEST_F(BgpUpdateTest, MultipleMarkers) {
    for (int i = 0; i < 4; i++) {
        CreatePeer();
    }

    // Configure block policies.
    peers_[1].set_block_at(1);
    peers_[2].set_block_at(1);
    peers_[2].set_block_at(5);

    peers_[3].set_block_at(3);
    peers_[3].set_block_at(6);
    peers_[4].set_block_at(7);

    // Enqueue updates
    vector<InetVpnRoute *> routes;
    TaskScheduler::GetInstance()->Stop();
    EnqueueUpdates(&tbl1_, &routes, 10);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peers_[0].update_count() == 10);
    
    // peers_[1].WaitOnBlocked(&mgr_);
    peers_[1].WriteActive(&mgr_);
    peers_[2].WriteActive(&mgr_);

    peers_[3].WaitOnBlocked(&mgr_);
    peers_[3].WriteActive(&mgr_);

    peers_[4].WaitOnBlocked(&mgr_);
    peers_[4].WriteActive(&mgr_);

    peers_[2].WaitOnBlocked(&mgr_);
    peers_[3].WaitOnBlocked(&mgr_);
    peers_[3].WriteActive(&mgr_);
    peers_[2].WriteActive(&mgr_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peers_[2].update_count() == 10);
    TASK_UTIL_EXPECT_TRUE(peers_[3].update_count() == 10);
    TASK_UTIL_EXPECT_TRUE(peers_[4].update_count() == 10);
    
    task_util::WaitForIdle();
    BOOST_FOREACH(BgpRoute *route, routes) {
        DeleteRouteState(&tbl1_, route);
    }
    STLDeleteValues(&routes);
}

class BgpUpdate2RibTest : public BgpUpdateTest {
protected:
    typedef BgpUpdateTest Base;

    BgpUpdate2RibTest()
        : tbl2_(inetvpn_table_, &mgr_, RibExportPolicy()) {
        tbl2_.updates()->SetMessageBuilder(&builder_);
    }

    virtual void SetUp() {
        Base::SetUp();

        for (int i = 0; i < 2; i++) {
            BgpTestPeer *peer = new BgpTestPeer();
            peers_.push_back(peer);        
            RibOutRegister(&tbl2_, peer);
        }
    }

    RibOut tbl2_;    
};

// 4. Multiple ribs (in sync)
TEST_F(BgpUpdate2RibTest, Basic) {
    RibOutRegister(&tbl2_, &peers_[0]);
    ASSERT_EQ(tbl1_.GetSchedulingGroup(), tbl2_.GetSchedulingGroup());
    
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, tbl1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, tbl1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, tbl2_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, tbl2_, a3_);

    TaskScheduler::GetInstance()->Stop();
    {
        RibOutUpdates *updates = tbl1_.updates();
        EnqueueOneUpdate(updates, &rt1, u1);
        EnqueueOneUpdate(updates, &rt2, u2);
    }
    {
        RibOutUpdates *updates = tbl2_.updates();
        EnqueueOneUpdate(updates, &rt3, u3);
        EnqueueOneUpdate(updates, &rt4, u4);
    }
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_TRUE(tbl2_.updates()->Empty());
    TASK_UTIL_EXPECT_EQ(4, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(2, peers_[1].update_count());
    TASK_UTIL_EXPECT_EQ(2, peers_[2].update_count());

    // Cleanup RouteState on all routes.
    DeleteRouteState(&tbl1_, &rt1);
    DeleteRouteState(&tbl1_, &rt2);
    DeleteRouteState(&tbl2_, &rt3);
    DeleteRouteState(&tbl2_, &rt4);
}

// 5. Multiple ribs (send block and catch up)
TEST_F(BgpUpdate2RibTest, WithBlock) {
    for (int i = 0; i < 4; i++) {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);    
        RibOutRegister(&tbl1_, peer);
        RibOutRegister(&tbl2_, peer);
    }

    // set blocking points
    peers_[1].set_block_at(4);
    peers_[3].set_block_at(9);

    peers_[5].set_block_at(2);
    peers_[6].set_block_at(3);
    peers_[7].set_block_at(4);

    // Enqueue the updates
    vector<InetVpnRoute *> routes;
    TaskScheduler::GetInstance()->Stop();
    EnqueueUpdates(&tbl1_, &routes, 10);
    EnqueueUpdates(&tbl2_, &routes, 10);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(10, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(10, peers_[2].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[4].update_count());

    peers_[1].WaitOnBlocked(&mgr_);
    peers_[1].WriteActive(&mgr_);

    peers_[3].WaitOnBlocked(&mgr_);
    peers_[3].WriteActive(&mgr_);

    peers_[5].WaitOnBlocked(&mgr_);
    peers_[6].WaitOnBlocked(&mgr_);
    peers_[7].WaitOnBlocked(&mgr_);

    peers_[5].WriteActive(&mgr_);
    peers_[6].WriteActive(&mgr_);
    peers_[7].WriteActive(&mgr_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(tbl1_.updates()->Empty());
    TASK_UTIL_EXPECT_TRUE(tbl2_.updates()->Empty());

    TASK_UTIL_EXPECT_EQ(10, peers_[1].update_count());
    TASK_UTIL_EXPECT_EQ(10, peers_[3].update_count());

    TASK_UTIL_EXPECT_EQ(20, peers_[5].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[6].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[7].update_count());

    BOOST_FOREACH(BgpRoute *route, routes) {
        DeleteRouteState(&tbl1_, route);
        DeleteRouteState(&tbl2_, route);
    }
    STLDeleteValues(&routes);
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
