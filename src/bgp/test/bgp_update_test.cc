/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/foreach.hpp>

#include <tbb/compat/condition_variable>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "control-node/control_node.h"

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
    BgpTestPeer() : index_(gbl_index++), count_(0), send_block_(false) {
        std::ostringstream repr;
        repr << "Peer" << index_;
        to_str_ = repr.str();
    }

    virtual ~BgpTestPeer() { }

    virtual const std::string &ToString() const { return to_str_; }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        count_++;
        send_block_ = block_set_.find(count_) != block_set_.end();
        if (send_block_) {
            cond_var_.Set();
        }
        return !send_block_;
    }

    void WriteActive(BgpUpdateSender *sender) {
        send_block_ = false;
        sender->PeerSendReady(this);
    }

    void WaitOnBlocked(BgpUpdateSender *sender) {
        // wait until the SendUpdate method returns false
        cond_var_.WaitAndClear();
        // wait until the BgpUpdateSender code updates its state
        ConcurrencyScope scope("bgp::PeerMembership");
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_FALSE(sender->partition(0)->PeerIsSendReady(this));
    }

    void set_block_at(int step) {
        block_set_.insert(step);
    }
    int update_count() const { return count_; }
    virtual bool send_ready() const { return !send_block_; }

private:
    int index_;
    std::set<int> block_set_;
    int count_;
    Condition cond_var_;
    bool send_block_;
    std::string to_str_;
};

class BgpMessageMock : public BgpMessage {
public:
    bool Start(const RibOut *ribout, bool cache_routes,
        const RibOutAttr *roattr, const BgpRoute *route) {
        return true;
    }
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr* attr) {
        return true;
    }
    virtual void Finish() {
    }
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp,
        const string **msg_str) {
        return NULL;
    }
};

class BgpMessageBuilderMock : public BgpMessageBuilder {
public:
    virtual Message *Create() const {
        return new BgpMessageMock;
    }
};

static RouteUpdate *BuildUpdate(BgpRoute *route, const RibOut *ribout,
        BgpAttrPtr attrp) {
    RouteUpdate *update = new RouteUpdate(route, RibOutUpdates::QUPDATE);
    UpdateInfoSList ulist;
    UpdateInfo *info = new UpdateInfo();
    info->roattr.set_attr(
        static_cast<const BgpTable *>(route->get_table()), attrp, 0, 0, false);
    info->target = ribout->PeerSet();
    ulist->push_front(*info);
    update->SetUpdateInfo(ulist);
    return update;
}

static RouteUpdate *BuildWithdraw(BgpRoute *route, const RibOut *ribout) {
    const DBState *state =
        route->GetState(ribout->table(), ribout->listener_id());
    const RouteState *rs = dynamic_cast<const RouteState *>(state);
    RouteUpdate *update = new RouteUpdate(route, RibOutUpdates::QUPDATE);
    UpdateInfoSList ulist;
    UpdateInfo *info = new UpdateInfo();
    info->target = ribout->PeerSet();
    ulist->push_front(*info);
    update->SetUpdateInfo(ulist);
    const_cast<RouteState *>(rs)->MoveHistory(update);
    delete rs;
    return update;
}

class BgpUpdateTest : public ::testing::Test {
protected:
    static const int kPeerCount = 2;
    static const int kAttrCount = 20;
    BgpUpdateTest()
        : server_(&evm_),
          sender_(server_.update_sender()),
          inetvpn_table_(
              static_cast<InetVpnTable *>(db_.CreateTable("bgp.l3vpn.0"))),
          ribout1_(inetvpn_table_->RibOutLocate(sender_, RibExportPolicy(1))) {
    }

    virtual void SetUp() {
        gbl_index = 0;
        for (int idx = 0; idx < kPeerCount; ++idx) {
            CreatePeer();
            RibOutRegister(ribout1_, &peers_[idx]);
        }

        CreateAttrs();
    }

    virtual void TearDown() {
        for (int idx = 0; idx < kPeerCount; ++idx) {
            RibOutDeactivate(ribout1_, &peers_[idx]);
            RibOutUnregister(ribout1_, &peers_[idx]);
        }
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
    }

    void RibOutDeactivate(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Deactivate(peer);
    }

    void RibOutUnregister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Unregister(peer);
    }

    void CreatePeer() {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);
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
            RouteUpdate *update = BuildUpdate(rt, ribout, attr_[i]);
            ribout->updates(0)->Enqueue(rt, update);
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
    DB db_;
    BgpServer server_;
    BgpUpdateSender *sender_;
    InetVpnTable *inetvpn_table_;
    RibOut *ribout1_;
    BgpAttrPtr a1_, a2_, a3_;
    std::vector<BgpAttrPtr> attr_;
    boost::ptr_vector<BgpTestPeer> peers_;
};

// Create a positive route update, observe that it gets sent.
// Then generate a corresponding delete.
TEST_F(BgpUpdateTest, Basic) {
    RibOutUpdates *updates = ribout1_->updates(0);
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, ribout1_, a1_);
    EnqueueOneUpdate(updates, &rt1, u1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_EQ(1, peers_[0].update_count());
    DBState *state = rt1.GetState(ribout1_->table(), ribout1_->listener_id());
    ASSERT_TRUE(state != NULL);
    RouteState *rs = static_cast<RouteState *>(state);
    const AdvertiseSList &adv_slist = rs->Advertised();
    TASK_UTIL_EXPECT_EQ(1, adv_slist->size());
    const AdvertiseInfo &ainfo = *adv_slist->begin();
    TASK_UTIL_EXPECT_TRUE(ribout1_->PeerSet() == ainfo.bitset);

    // delete
    u1 = BuildWithdraw(&rt1, ribout1_);
    EnqueueOneUpdate(updates, &rt1, u1);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_EQ(2, peers_[0].update_count());
    state = rt1.GetState(ribout1_->table(), ribout1_->listener_id());
    TASK_UTIL_EXPECT_TRUE(state == NULL);

    // Cleanup RouteState on all routes.
    DeleteRouteState(ribout1_, &rt1);
}

// 2. Verify that update with same attribute end up in the same packet.
TEST_F(BgpUpdateTest, UpdatePack) {
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, ribout1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, ribout1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, ribout1_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, ribout1_, a3_);

    TaskScheduler::GetInstance()->Stop();
    RibOutUpdates *updates = ribout1_->updates(0);
    EnqueueOneUpdate(updates, &rt1, u1);
    EnqueueOneUpdate(updates, &rt2, u2);
    EnqueueOneUpdate(updates, &rt3, u3);
    EnqueueOneUpdate(updates, &rt4, u4);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_EQ(3, peers_[0].update_count());

    // Cleanup RouteState on all routes.
    DeleteRouteState(ribout1_, &rt1);
    DeleteRouteState(ribout1_, &rt2);
    DeleteRouteState(ribout1_, &rt3);
    DeleteRouteState(ribout1_, &rt4);
}

// 3. Peer send blocks
TEST_F(BgpUpdateTest, SendBlock) {
    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, ribout1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, ribout1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, ribout1_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, ribout1_, a3_);

    BgpTestPeer *peer1 = &peers_[1];
    peer1->set_block_at(1);

    TaskScheduler::GetInstance()->Stop();
    RibOutUpdates *updates = ribout1_->updates(0);
    EnqueueOneUpdate(updates, &rt1, u1);
    EnqueueOneUpdate(updates, &rt2, u2);
    EnqueueOneUpdate(updates, &rt3, u3);
    EnqueueOneUpdate(updates, &rt4, u4);
    TaskScheduler::GetInstance()->Start();

    peer1->WaitOnBlocked(sender_);
    peer1->WriteActive(sender_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_EQ(3, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(3, peers_[1].update_count());

    UpdateQueue *queue = ribout1_->updates(0)->queue(RibOutUpdates::QUPDATE);
    RibPeerSet &bitset = queue->tail_marker()->members;
    TASK_UTIL_EXPECT_TRUE(bitset == ribout1_->PeerSet());

    // Cleanup RouteState on all routes.
    DeleteRouteState(ribout1_, &rt1);
    DeleteRouteState(ribout1_, &rt2);
    DeleteRouteState(ribout1_, &rt3);
    DeleteRouteState(ribout1_, &rt4);
}

TEST_F(BgpUpdateTest, MultipleMarkers) {
    for (int idx = 0; idx < 4; ++idx) {
        CreatePeer();
        RibOutRegister(ribout1_, &peers_[kPeerCount + idx]);
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
    EnqueueUpdates(ribout1_, &routes, 10);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peers_[0].update_count() == 10);

    peers_[1].WriteActive(sender_);
    peers_[2].WriteActive(sender_);

    peers_[3].WaitOnBlocked(sender_);
    peers_[3].WriteActive(sender_);

    peers_[4].WaitOnBlocked(sender_);
    peers_[4].WriteActive(sender_);

    peers_[2].WaitOnBlocked(sender_);
    peers_[3].WaitOnBlocked(sender_);
    peers_[3].WriteActive(sender_);
    peers_[2].WriteActive(sender_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(peers_[2].update_count() == 10);
    TASK_UTIL_EXPECT_TRUE(peers_[3].update_count() == 10);
    TASK_UTIL_EXPECT_TRUE(peers_[4].update_count() == 10);

    task_util::WaitForIdle();
    BOOST_FOREACH(BgpRoute *route, routes) {
        DeleteRouteState(ribout1_, route);
    }
    STLDeleteValues(&routes);
    for (int idx = 0; idx < 4; ++idx) {
        RibOutDeactivate(ribout1_, &peers_[kPeerCount + idx]);
        RibOutUnregister(ribout1_, &peers_[kPeerCount + idx]);
    }
}

class BgpUpdate2RibTest : public BgpUpdateTest {
protected:
    typedef BgpUpdateTest Base;
    static const int kPeerCount2 = 2;

    BgpUpdate2RibTest()
      : ribout2_(inetvpn_table_->RibOutLocate(sender_, RibExportPolicy(2))) {
    }

    virtual void SetUp() {
        Base::SetUp();
        for (int idx = 0; idx < kPeerCount2; ++idx) {
            CreatePeer();
            RibOutRegister(ribout2_, &peers_[kPeerCount + idx]);
        }
    }

    virtual void TearDown() {
        for (int idx = 0; idx < kPeerCount2; ++idx) {
            RibOutDeactivate(ribout2_, &peers_[kPeerCount + idx]);
            RibOutUnregister(ribout2_, &peers_[kPeerCount + idx]);
        }
        Base::TearDown();
    }

    RibOut *ribout2_;
};

// 4. Multiple ribs (in sync)
TEST_F(BgpUpdate2RibTest, Basic) {
    RibOutRegister(ribout2_, &peers_[0]);

    InetVpnPrefix prefix(InetVpnPrefix::FromString("0:0:192.168.24.0/24"));
    InetVpnRoute rt1(prefix), rt2(prefix), rt3(prefix), rt4(prefix);
    RouteUpdate *u1 = BuildUpdate(&rt1, ribout1_, a1_);
    RouteUpdate *u2 = BuildUpdate(&rt2, ribout1_, a2_);
    RouteUpdate *u3 = BuildUpdate(&rt3, ribout2_, a1_);
    RouteUpdate *u4 = BuildUpdate(&rt4, ribout2_, a3_);

    TaskScheduler::GetInstance()->Stop();
    {
        RibOutUpdates *updates = ribout1_->updates(0);
        EnqueueOneUpdate(updates, &rt1, u1);
        EnqueueOneUpdate(updates, &rt2, u2);
    }
    {
        RibOutUpdates *updates = ribout2_->updates(0);
        EnqueueOneUpdate(updates, &rt3, u3);
        EnqueueOneUpdate(updates, &rt4, u4);
    }
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_TRUE(ribout2_->updates(0)->Empty());
    TASK_UTIL_EXPECT_EQ(4, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(2, peers_[1].update_count());
    TASK_UTIL_EXPECT_EQ(2, peers_[2].update_count());

    // Cleanup RouteState on all routes.
    DeleteRouteState(ribout1_, &rt1);
    DeleteRouteState(ribout1_, &rt2);
    DeleteRouteState(ribout2_, &rt3);
    DeleteRouteState(ribout2_, &rt4);

    RibOutDeactivate(ribout2_, &peers_[0]);
    RibOutUnregister(ribout2_, &peers_[0]);
}

// 5. Multiple ribs (send block and catch up)
TEST_F(BgpUpdate2RibTest, WithBlock) {
    for (int idx = 0; idx < 4; ++idx) {
        CreatePeer();
        RibOutRegister(ribout1_, &peers_[kPeerCount + kPeerCount2 + idx]);
        RibOutRegister(ribout2_, &peers_[kPeerCount + kPeerCount2 + idx]);
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
    EnqueueUpdates(ribout1_, &routes, 10);
    EnqueueUpdates(ribout2_, &routes, 10);
    TaskScheduler::GetInstance()->Start();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(10, peers_[0].update_count());
    TASK_UTIL_EXPECT_EQ(10, peers_[2].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[4].update_count());

    peers_[1].WaitOnBlocked(sender_);
    peers_[1].WriteActive(sender_);

    peers_[3].WaitOnBlocked(sender_);
    peers_[3].WriteActive(sender_);

    peers_[5].WaitOnBlocked(sender_);
    peers_[6].WaitOnBlocked(sender_);
    peers_[7].WaitOnBlocked(sender_);

    peers_[5].WriteActive(sender_);
    peers_[6].WriteActive(sender_);
    peers_[7].WriteActive(sender_);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(ribout1_->updates(0)->Empty());
    TASK_UTIL_EXPECT_TRUE(ribout2_->updates(0)->Empty());

    TASK_UTIL_EXPECT_EQ(10, peers_[1].update_count());
    TASK_UTIL_EXPECT_EQ(10, peers_[3].update_count());

    TASK_UTIL_EXPECT_EQ(20, peers_[5].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[6].update_count());
    TASK_UTIL_EXPECT_EQ(20, peers_[7].update_count());

    BOOST_FOREACH(BgpRoute *route, routes) {
        DeleteRouteState(ribout1_, route);
        DeleteRouteState(ribout2_, route);
    }
    STLDeleteValues(&routes);
    for (int idx = 0; idx < 4; ++idx) {
        RibOutDeactivate(ribout1_, &peers_[kPeerCount + kPeerCount2 + idx]);
        RibOutUnregister(ribout1_, &peers_[kPeerCount + kPeerCount2 + idx]);
        RibOutDeactivate(ribout2_, &peers_[kPeerCount + kPeerCount2 + idx]);
        RibOutUnregister(ribout2_, &peers_[kPeerCount + kPeerCount2 + idx]);
    }
}

static void SetUp() {
    bgp_log_test::init();
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpMessageBuilder>(
        boost::factory<BgpMessageBuilderMock *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    BgpServer::Terminate();
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
