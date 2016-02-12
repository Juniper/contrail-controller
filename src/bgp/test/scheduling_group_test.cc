/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/scheduling_group.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "control-node/control_node.h"
#include "db/db.h"

using namespace std;

static int gbl_index;

class BgpTestPeer : public IPeerUpdate {
public:
    BgpTestPeer() : index_(gbl_index++) {
    }
    virtual ~BgpTestPeer() {
    }
    virtual std::string ToString() const {
        std::ostringstream repr;
        repr << "Peer" << index_;
        return repr.str();
    }
    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize)  {
        return true;
    }
private:
    int index_;
};

class SchedulingGroupManagerTest : public ::testing::Test {
protected:
    SchedulingGroupManagerTest()
        : inetvpn_table_(static_cast<InetVpnTable *>(db_.CreateTable("bgp.l3vpn.0"))) {
    }

    virtual void SetUp() {
        gbl_index = 0;
        TaskScheduler::GetInstance()->Stop();
    }

    virtual void TearDown() {
        TaskScheduler::GetInstance()->Start();
    }

    void Join(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        sgman_.Join(ribout, peer);
    }

    void Leave(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        sgman_.Leave(ribout, peer);
    }

    void Register(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
    }

    void GetRibOutList(SchedulingGroup *sg,
            SchedulingGroup::RibOutList *rlist) {
        ConcurrencyScope scope("bgp::PeerMembership");
        sg->GetRibOutList(rlist);
    }

    void GetPeerList(SchedulingGroup *sg, SchedulingGroup::PeerList *plist) {
        ConcurrencyScope scope("bgp::PeerMembership");
        sg->GetPeerList(plist);
    }

    void WorkPeerEnqueue(SchedulingGroup *sg, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::SendReadyTask");
        sg->WorkPeerEnqueue(peer);
    }

    void WorkRibOutEnqueue(SchedulingGroup *sg, RibOut *ribout) {
        ConcurrencyScope scope("bgp::SendTask");
        sg->WorkRibOutEnqueue(ribout, 0);
    }

    size_t GetWorkQueueSize(SchedulingGroup *sg) {
        ConcurrencyScope scope("bgp::PeerMembership");
        return sg->work_queue_.size();
    }

    void SetQueueActive(SchedulingGroup *sg, RibOut *ribout, int queue_id,
        IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::SendTask");
        sg->SetQueueActive(ribout, queue_id, peer);
    }

    bool IsQueueActive(SchedulingGroup *sg, RibOut *ribout, int queue_id,
        IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::SendTask");
       return sg->IsQueueActive(ribout, queue_id, peer);
    }

    size_t GetWorkQueueInvalidSize(SchedulingGroup *sg) {
        ConcurrencyScope scope("bgp::PeerMembership");
        size_t count = 0;
        for (SchedulingGroup::WorkQueue::iterator it = sg->work_queue_.begin();
            it != sg->work_queue_.end(); ++it) {
            SchedulingGroup::WorkBase *wentry = it.operator->();
            if (!wentry->valid)
                count++;
        }
        return count;
    }

    void VerifyRibOutInvalid(SchedulingGroup *sg, RibOut *ribout) {
        ConcurrencyScope scope("bgp::PeerMembership");
        for (SchedulingGroup::WorkQueue::iterator it = sg->work_queue_.begin();
            it != sg->work_queue_.end(); ++it) {
            SchedulingGroup::WorkBase *wentry = it.operator->();
            if (wentry->type != SchedulingGroup::WorkBase::WRibOut)
                continue;
            SchedulingGroup::WorkRibOut *wribout =
                static_cast<SchedulingGroup::WorkRibOut *>(wentry);
            if (wribout->ribout != ribout)
                continue;
            EXPECT_FALSE(wribout->valid);
        }
    }

    void VerifyPeerInvalid(SchedulingGroup *sg, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        for (SchedulingGroup::WorkQueue::iterator it = sg->work_queue_.begin();
            it != sg->work_queue_.end(); ++it) {
            SchedulingGroup::WorkBase *wentry = it.operator->();
            if (wentry->type != SchedulingGroup::WorkBase::WPeer)
                continue;
            SchedulingGroup::WorkPeer *wpeer =
                static_cast<SchedulingGroup::WorkPeer *>(wentry);
            if (wpeer->peer != peer)
                continue;
            EXPECT_FALSE(wpeer->valid);
        }
    }

    SchedulingGroupManager sgman_;
    DB db_;
    InetVpnTable *inetvpn_table_;
};

TEST_F(SchedulingGroupManagerTest, OneTableTwoPeers) {
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p2(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(rp1->GetSchedulingGroup() != NULL);
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.RibOutGroup(rp1.get()));
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.PeerGroup(p1.get()));

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.PeerGroup(p1.get()));
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.PeerGroup(p2.get()));

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(rp1->GetSchedulingGroup() != NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(0, sgman_.size());
    EXPECT_TRUE(rp1->GetSchedulingGroup() == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p2.get()) == NULL);
}

TEST_F(SchedulingGroupManagerTest, TwoTablesOnePeer) {
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp2(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(rp1->GetSchedulingGroup() != NULL);
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.RibOutGroup(rp1.get()));
    EXPECT_EQ(rp1->GetSchedulingGroup(), sgman_.PeerGroup(p1.get()));

    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(rp2->GetSchedulingGroup() != NULL);
    EXPECT_EQ(rp2->GetSchedulingGroup(), sgman_.RibOutGroup(rp2.get()));
    EXPECT_EQ(rp2->GetSchedulingGroup(), sgman_.PeerGroup(p1.get()));

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(rp1->GetSchedulingGroup() == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) != NULL);

    Leave(rp2.get(), p1.get());
    EXPECT_EQ(0, sgman_.size());
    EXPECT_TRUE(rp2->GetSchedulingGroup() == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);
}

// Join/Leave order is such that no merge/split is required.  The first peer
// joins both tables, then the second peer joins both tables. Leave order is
// the same as join order.
TEST_F(SchedulingGroupManagerTest, TwoTablesTwoPeers1a) {
    SchedulingGroup *sg;
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp2(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p2(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_TRUE(sg != NULL);
    EXPECT_EQ(1, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(2, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(3, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(4, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(3, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Leave(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(2, sg->member_count());
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(1, sg->member_count());
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);
    EXPECT_TRUE(sgman_.RibOutGroup(rp1.get()) == NULL);

    Leave(rp2.get(), p2.get());
    EXPECT_EQ(0, sgman_.size());
    EXPECT_TRUE(sgman_.RibOutGroup(rp2.get()) == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p2.get()) == NULL);
}

// Join/Leave order is such that no merge/split is required. Both peers join
// one table, then they joins the other table. Leave order is the same as join
// order.
TEST_F(SchedulingGroupManagerTest, TwoTablesTwoPeers1b) {
    SchedulingGroup *sg;
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp2(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p2(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_TRUE(sg != NULL);
    EXPECT_EQ(1, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(2, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(3, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(4, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(3, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(2, sg->member_count());
    EXPECT_TRUE(sgman_.RibOutGroup(rp1.get()) == NULL);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(1, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);

    Leave(rp2.get(), p2.get());
    EXPECT_EQ(0, sgman_.size());
    EXPECT_TRUE(sgman_.RibOutGroup(rp2.get()) == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p2.get()) == NULL);
}

// Join/Leave order is such that a merge/split is required. The 2 peers join
// different tables. Then they join the other table.  Leave order is reverse
// of the join order.
TEST_F(SchedulingGroupManagerTest, TwoTablesTwoPeers2) {
    SchedulingGroup *sg, *sg_a, *sg_b;
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp2(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p2(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    sg_a = rp1->GetSchedulingGroup();
    EXPECT_TRUE(sg_a != NULL);
    EXPECT_EQ(1, sg_a->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg_a);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg_a);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(2, sgman_.size());
    sg_b = rp2->GetSchedulingGroup();
    EXPECT_TRUE(sg_b != NULL);
    EXPECT_EQ(1, sg_b->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg_b);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg_b);

    // Merge happens here.
    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_EQ(3, sg->member_count());
    EXPECT_TRUE(sg_a == sg || sg_b == sg);
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_EQ(4, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_EQ(3, sg->member_count());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    // Split happens here.
    Leave(rp2.get(), p1.get());
    EXPECT_EQ(2, sgman_.size());
    sg_a = rp1->GetSchedulingGroup();
    sg_b = rp2->GetSchedulingGroup();
    EXPECT_TRUE(sg_a == sg || sg_b == sg);
    EXPECT_EQ(1, sg_a->member_count());
    EXPECT_EQ(1, sg_b->member_count());
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg_a);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg_b);

    Leave(rp2.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(sgman_.RibOutGroup(rp2.get()) == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p2.get()) == NULL);

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(0, sgman_.size());
    EXPECT_TRUE(sgman_.RibOutGroup(rp1.get()) == NULL);
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);
}

TEST_F(SchedulingGroupManagerTest, Merge) {
    auto_ptr<RibOut> rp1(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp2(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<RibOut> rp3(new RibOut(inetvpn_table_, &sgman_,
                                    RibExportPolicy()));
    auto_ptr<BgpTestPeer> p1(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p2(new BgpTestPeer());
    auto_ptr<BgpTestPeer> p3(new BgpTestPeer());

    Join(rp1.get(), p1.get());
    Join(rp2.get(), p2.get());
    Join(rp3.get(), p3.get());
    EXPECT_EQ(3, sgman_.size());
    sgman_.CheckInvariants();

    Join(rp1.get(), p3.get());
    EXPECT_EQ(2, sgman_.size());
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sgman_.PeerGroup(p3.get()));
    sgman_.CheckInvariants();

    Join(rp2.get(), p3.get());
    EXPECT_EQ(1, sgman_.size());
    sgman_.CheckInvariants();

    Leave(rp1.get(), p3.get());
    EXPECT_EQ(2, sgman_.size());
    sgman_.CheckInvariants();

    SchedulingGroup *sg1 = sgman_.PeerGroup(p1.get());
    SchedulingGroup *sg2 = sgman_.PeerGroup(p2.get());
    EXPECT_EQ(sg2, sgman_.PeerGroup(p3.get()));

    SchedulingGroup::PeerList plist;
    GetPeerList(sg1, &plist);
    EXPECT_EQ(1, plist.size());
    GetPeerList(sg2, &plist);
    EXPECT_EQ(2, plist.size());

    SchedulingGroup::RibOutList rlist;
    GetRibOutList(sg1, &rlist);
    EXPECT_EQ(1, rlist.size());
    EXPECT_EQ(rp1.get(), rlist.front());

    GetRibOutList(sg2, &rlist);
    EXPECT_EQ(2, rlist.size());
    EXPECT_TRUE(rlist.front() == rp2.get() || rlist.front() == rp3.get());
    EXPECT_TRUE(rlist.back() != rlist.front());
    EXPECT_TRUE(rlist.back() != rp1.get());


    Leave(rp2.get(), p2.get());
    EXPECT_EQ(2, sgman_.size());
    sg1 = sgman_.PeerGroup(p1.get());
    EXPECT_TRUE(sgman_.PeerGroup(p2.get()) == NULL);
    sg2 = sgman_.PeerGroup(p3.get());

    GetRibOutList(sg1, &rlist);
    EXPECT_EQ(1, rlist.size());
    EXPECT_EQ(rp1.get(), rlist.front());

    GetRibOutList(sg2, &rlist);
    EXPECT_EQ(2, rlist.size());

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());

    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);

    Leave(rp3.get(), p3.get());
    EXPECT_EQ(1, sgman_.size());

    Leave(rp2.get(), p3.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_F(SchedulingGroupManagerTest, TwoTablesMultiplePeers) {
    RibOut tbl1(inetvpn_table_, &sgman_, RibExportPolicy());
    RibOut tbl2(inetvpn_table_, &sgman_, RibExportPolicy());
    vector<BgpTestPeer *> peers;
    for (int i = 0; i < 2; i++) {
        BgpTestPeer *peer = new BgpTestPeer();
        peers.push_back(peer);
        Register(&tbl1, peer);
    }
    EXPECT_TRUE(tbl1.GetSchedulingGroup() != NULL);
    for (int i = 0; i < 2; i++) {
        BgpTestPeer *peer = new BgpTestPeer();
        peers.push_back(peer);
        Register(&tbl2, peer);
    }
    EXPECT_TRUE(tbl2.GetSchedulingGroup() != NULL);
    EXPECT_NE(tbl1.GetSchedulingGroup(), tbl2.GetSchedulingGroup());
    Register(&tbl2, peers[0]);
    EXPECT_EQ(tbl1.GetSchedulingGroup(), tbl2.GetSchedulingGroup());

    BgpTestPeer *peer = new BgpTestPeer();
    peers.push_back(peer);
    Register(&tbl1, peer);
    Register(&tbl2, peer);

    STLDeleteValues(&peers);
}

//
// Exercise the Leave code with a large number of RibOuts.
// Test is constructed such that there's only a single SchedulingGroup.
// Intent is to test the performance of SchedulingGroup::GetPeerRibList.
//
TEST_F(SchedulingGroupManagerTest, LeaveScaling1) {
    // Create 2 peers and join them to a common ribout.
    vector<BgpTestPeer *> peers;
    BgpTestPeer *test_peer(new BgpTestPeer());
    BgpTestPeer *other_peer(new BgpTestPeer());
    peers.push_back(test_peer);
    peers.push_back(other_peer);
    RibOut ribout_common(inetvpn_table_, &sgman_, RibExportPolicy());
    Join(&ribout_common, test_peer);
    Join(&ribout_common, other_peer);
    EXPECT_EQ(1, sgman_.size());

    // Create large number of additional RibOuts.
    vector<RibOut *> ribouts;
    for (size_t idx = 0; idx < SchedulingGroup::kSplitThreshold / 2; ++idx) {
        RibOut *ribout(new RibOut(inetvpn_table_, &sgman_,
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, idx, 0)));
        ribouts.push_back(ribout);
    }

    // Join the test peer to all the additional RibOuts.
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Join(ribout, test_peer);
    }
    EXPECT_EQ(1, sgman_.size());

    // Leave the test peer from all the additional RibOuts.
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Leave(ribout, test_peer);
    }
    EXPECT_EQ(1, sgman_.size());

    // Cleanup.
    Leave(&ribout_common, test_peer);
    Leave(&ribout_common, other_peer);
    EXPECT_EQ(0, sgman_.size());
    STLDeleteValues(&ribouts);
    STLDeleteValues(&peers);
}

//
// Exercise the Leave code with a large number of RibOuts.
// Test is constructed such that there's only a single SchedulingGroup.
// Intent is to test the performance of the logic to determine if the
// SchedulingGroup can be split - focus is on the negative case.
// The common RibOut which prevents the split has the lowest index.
//
TEST_F(SchedulingGroupManagerTest, LeaveScaling2) {
    // Create 2 peers and join them to a common ribout.
    vector<BgpTestPeer *> peers;
    BgpTestPeer *test_peer1(new BgpTestPeer());
    BgpTestPeer *test_peer2(new BgpTestPeer());
    peers.push_back(test_peer1);
    peers.push_back(test_peer2);
    RibOut ribout_common(inetvpn_table_, &sgman_, RibExportPolicy());
    Join(&ribout_common, test_peer1);
    Join(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());

    // Create large number of additional ribouts.
    vector<RibOut *> ribouts;
    for (size_t idx = 0; idx < SchedulingGroup::kSplitThreshold / 2; ++idx) {
        RibOut *ribout(new RibOut(inetvpn_table_, &sgman_,
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, idx, 0)));
        ribouts.push_back(ribout);
    }

    // Join the test peers to odd/even additional ribouts.
    size_t idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Join(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(1, sgman_.size());

    // Leave the test peer from odd/even additional ribouts.
    idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Leave(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(1, sgman_.size());

    // Cleanup.
    Leave(&ribout_common, test_peer1);
    Leave(&ribout_common, test_peer2);
    EXPECT_EQ(0, sgman_.size());
    STLDeleteValues(&ribouts);
    STLDeleteValues(&peers);
}

//
// Exercise the Leave code with a large number of RibOuts.
// Test is constructed such that there's only a single SchedulingGroup.
// Intent is to test the performance of the logic to determine if the
// SchedulingGroup can be split - focus is on the negative case.
// The common RibOut which prevents the split has the highest index.
//
TEST_F(SchedulingGroupManagerTest, LeaveScaling3) {
    // Create 2 peers.
    vector<BgpTestPeer *> peers;
    BgpTestPeer *test_peer1(new BgpTestPeer());
    BgpTestPeer *test_peer2(new BgpTestPeer());
    peers.push_back(test_peer1);
    peers.push_back(test_peer2);

    // Create large number of additional RibOuts.
    vector<RibOut *> ribouts;
    for (size_t idx = 0; idx < SchedulingGroup::kSplitThreshold / 2; ++idx) {
        RibOut *ribout(new RibOut(inetvpn_table_, &sgman_,
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, idx, 0)));
        ribouts.push_back(ribout);
    }

    // Join the test peers to odd/even additional RibOuts.
    size_t idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Join(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(2, sgman_.size());

    // Join the test peers to a common ribout so that the groups get merged.
    RibOut ribout_common(inetvpn_table_, &sgman_, RibExportPolicy());
    Join(&ribout_common, test_peer1);
    Join(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());

    // Leave the test peers from odd/even additional RibOuts.
    idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Leave(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(1, sgman_.size());

    // Cleanup.
    Leave(&ribout_common, test_peer1);
    Leave(&ribout_common, test_peer2);
    EXPECT_EQ(0, sgman_.size());
    STLDeleteValues(&ribouts);
    STLDeleteValues(&peers);
}

//
// Exercise the split disable logic.
//
TEST_F(SchedulingGroupManagerTest, SplitDisabled1) {
    // Create 2 peers and join them to a common ribout.
    vector<BgpTestPeer *> peers;
    BgpTestPeer *test_peer1(new BgpTestPeer());
    BgpTestPeer *test_peer2(new BgpTestPeer());
    peers.push_back(test_peer1);
    peers.push_back(test_peer2);
    RibOut ribout_common(inetvpn_table_, &sgman_, RibExportPolicy());
    Join(&ribout_common, test_peer1);
    Join(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());

    // Create large number of additional ribouts.
    vector<RibOut *> ribouts;
    for (size_t idx = 0; idx < SchedulingGroup::kSplitThreshold; ++idx) {
        RibOut *ribout(new RibOut(inetvpn_table_, &sgman_,
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, idx, 0)));
        ribouts.push_back(ribout);
    }

    // Join the test peers to odd/even additional ribouts.
    size_t idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Join(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(1, sgman_.size());
    SchedulingGroup *sg1 = ribouts[0]->GetSchedulingGroup();
    EXPECT_TRUE(sg1->split_disabled());
    EXPECT_EQ(SchedulingGroup::kSplitThreshold + 2, sg1->member_count());

    // Leave test peers from common ribout and verify that group is not split.
    Leave(&ribout_common, test_peer1);
    Leave(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());
    SchedulingGroup *sg2 = ribouts[0]->GetSchedulingGroup();
    EXPECT_TRUE(sg2->split_disabled());
    EXPECT_EQ(SchedulingGroup::kSplitThreshold + 0, sg2->member_count());
    EXPECT_EQ(sg1, sg2);

    // Leave the test peer from odd/even additional ribouts.
    idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Leave(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(0, sgman_.size());

    STLDeleteValues(&ribouts);
    STLDeleteValues(&peers);
}

//
// Exercise the split disable logic after a merge.
//
TEST_F(SchedulingGroupManagerTest, SplitDisabled2) {
    // Create 2 peers.
    vector<BgpTestPeer *> peers;
    BgpTestPeer *test_peer1(new BgpTestPeer());
    BgpTestPeer *test_peer2(new BgpTestPeer());
    peers.push_back(test_peer1);
    peers.push_back(test_peer2);

    // Create large number of additional RibOuts.
    vector<RibOut *> ribouts;
    for (size_t idx = 0; idx < SchedulingGroup::kSplitThreshold; ++idx) {
        RibOut *ribout(new RibOut(inetvpn_table_, &sgman_,
            RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, idx, 0)));
        ribouts.push_back(ribout);
    }

    // Join the test peers to odd/even additional RibOuts.
    size_t idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Join(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(2, sgman_.size());
    EXPECT_EQ(SchedulingGroup::kSplitThreshold / 2,
        ribouts[0]->GetSchedulingGroup()->member_count());

    // Join the test peers to a common ribout so that the groups get merged.
    RibOut ribout_common(inetvpn_table_, &sgman_, RibExportPolicy());
    Join(&ribout_common, test_peer1);
    Join(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());
    SchedulingGroup *sg1 = ribouts[0]->GetSchedulingGroup();
    EXPECT_TRUE(sg1->split_disabled());
    EXPECT_EQ(SchedulingGroup::kSplitThreshold + 2, sg1->member_count());

    // Leave test peers from common ribout and verify that group is not split.
    Leave(&ribout_common, test_peer1);
    Leave(&ribout_common, test_peer2);
    EXPECT_EQ(1, sgman_.size());
    SchedulingGroup *sg2 = ribouts[0]->GetSchedulingGroup();
    EXPECT_TRUE(sg2->split_disabled());
    EXPECT_EQ(SchedulingGroup::kSplitThreshold + 0, sg1->member_count());
    EXPECT_EQ(sg1, sg2);

    // Leave the test peers from odd/even additional RibOuts.
    idx = 0;
    BOOST_FOREACH(RibOut *ribout, ribouts) {
        Leave(ribout, peers[idx % 2]);
        idx++;
    }
    EXPECT_EQ(0, sgman_.size());

    // Cleanup.
    STLDeleteValues(&ribouts);
    STLDeleteValues(&peers);
}

TEST_F(SchedulingGroupManagerTest, WorkRibOutInvalidate) {
    // Create 1 test peer and 2 ribouts.
    boost::scoped_ptr<BgpTestPeer> test_peer(new BgpTestPeer());
    boost::scoped_ptr<RibOut> ribout1(new RibOut(inetvpn_table_, &sgman_,
        RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0)));
    boost::scoped_ptr<RibOut> ribout2(new RibOut(inetvpn_table_, &sgman_,
        RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, 2, 0)));

    // Join the test peer to ribout1 and ribout2.
    Join(ribout1.get(), test_peer.get());
    Join(ribout2.get(), test_peer.get());
    EXPECT_EQ(1, sgman_.size());

    // Add multiple WorkRibOut entries for both ribouts to the group.
    SchedulingGroup *sg = ribout1->GetSchedulingGroup();
    WorkRibOutEnqueue(sg, ribout1.get());
    WorkRibOutEnqueue(sg, ribout2.get());
    WorkRibOutEnqueue(sg, ribout1.get());
    WorkRibOutEnqueue(sg, ribout2.get());
    EXPECT_EQ(4, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    // Leave the test peer from ribout2.
    // The WorkRibOut items for ribout1 will get invalidated at this point.
    Leave(ribout2.get(), test_peer.get());
    EXPECT_EQ(4, GetWorkQueueSize(sg));
    EXPECT_EQ(2, GetWorkQueueInvalidSize(sg));
    VerifyRibOutInvalid(sg, ribout2.get());

    // Leave the test peer from ribout1.
    Leave(ribout1.get(), test_peer.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_F(SchedulingGroupManagerTest, WorkPeerInvalidate) {
    // Create 2 test peers and 1 ribout.
    boost::scoped_ptr<BgpTestPeer> test_peer1(new BgpTestPeer());
    boost::scoped_ptr<BgpTestPeer> test_peer2(new BgpTestPeer());
    boost::scoped_ptr<RibOut> ribout(new RibOut(inetvpn_table_, &sgman_,
        RibExportPolicy(BgpProto::IBGP, RibExportPolicy::BGP, 0, 0)));

    // Join test peers 1 and 2 to ribout.
    Join(ribout.get(), test_peer1.get());
    Join(ribout.get(), test_peer2.get());
    EXPECT_EQ(1, sgman_.size());

    // Add multiple WorkPeer entries for both test peers to group.
    SchedulingGroup *sg = ribout->GetSchedulingGroup();
    WorkPeerEnqueue(sg, test_peer1.get());
    WorkPeerEnqueue(sg, test_peer2.get());
    WorkPeerEnqueue(sg, test_peer1.get());
    WorkPeerEnqueue(sg, test_peer2.get());
    EXPECT_EQ(4, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    // Leave test peer 1 from ribout.
    // The WorkPeer items for test peer1 will get invalidated at this point.
    Leave(ribout.get(), test_peer1.get());
    EXPECT_EQ(4, GetWorkQueueSize(sg));
    EXPECT_EQ(2, GetWorkQueueInvalidSize(sg));
    VerifyPeerInvalid(sg, test_peer1.get());

    // Leave test peer 1 from ribout.
    Leave(ribout.get(), test_peer2.get());
    EXPECT_EQ(0, sgman_.size());
}

// Parameterize number of entries in the work queue and the order in which
// the entries are added.

typedef std::tr1::tuple<int, bool> WorkQueueTestParams;

class SchedulingGroupWorkQueueTest :
    public SchedulingGroupManagerTest,
    public ::testing::WithParamInterface<WorkQueueTestParams> {

protected:
    virtual void SetUp() {
        SchedulingGroupManagerTest::SetUp();
        rp1_.reset(new RibOut(inetvpn_table_, &sgman_, RibExportPolicy()));
        rp2_.reset(new RibOut(inetvpn_table_, &sgman_, RibExportPolicy()));
        p1_.reset(new BgpTestPeer());
        p2_.reset(new BgpTestPeer());
        num_items_ = std::tr1::get<0>(GetParam());
        condition_ = std::tr1::get<1>(GetParam());
    }
    virtual void TearDown() {
        SchedulingGroupManagerTest::TearDown();
    }

    auto_ptr<RibOut> rp1_, rp2_;
    auto_ptr<BgpTestPeer> p1_, p2_;
    int num_items_;
    bool condition_;
};

TEST_P(SchedulingGroupWorkQueueTest, SplitWorkRibOut1) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp1_.get(), p2_.get());
    Join(rp2_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        if (condition_) {
            WorkRibOutEnqueue(sg, rp1_.get());
            WorkRibOutEnqueue(sg, rp2_.get());
        } else {
            WorkRibOutEnqueue(sg, rp2_.get());
            WorkRibOutEnqueue(sg, rp1_.get());
        }
    }

    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, SplitWorkRibOut2) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp1_.get(), p2_.get());
    Join(rp2_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        if (condition_) {
            WorkRibOutEnqueue(sg, rp1_.get());
        } else {
            WorkRibOutEnqueue(sg, rp2_.get());
        }
    }

    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(condition_ ? num_items_ : 0, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(condition_ ? 0 : num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, SplitWorkPeer1) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp1_.get(), p2_.get());
    Join(rp2_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        if (condition_) {
            WorkPeerEnqueue(sg, p1_.get());
            WorkPeerEnqueue(sg, p2_.get());
        } else {
            WorkPeerEnqueue(sg, p2_.get());
            WorkPeerEnqueue(sg, p1_.get());
        }
    }

    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, SplitWorkPeer2) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp1_.get(), p2_.get());
    Join(rp2_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        if (condition_) {
            WorkPeerEnqueue(sg, p1_.get());
        } else {
            WorkPeerEnqueue(sg, p2_.get());
        }
    }

    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(condition_ ? num_items_ : 0, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(condition_ ? 0 : num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, MergeWorkRibOut1) {
    SchedulingGroup *sg;

    if (condition_) {
        Join(rp1_.get(), p1_.get());
        Join(rp2_.get(), p2_.get());
    } else {
        Join(rp2_.get(), p2_.get());
        Join(rp1_.get(), p1_.get());
    }
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkRibOutEnqueue(sg, rp1_.get());
    }

    sg = rp2_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkRibOutEnqueue(sg, rp2_.get());
    }

    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(2 * num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, MergeWorkRibOut2) {
    SchedulingGroup *sg;

    if (condition_) {
        Join(rp1_.get(), p1_.get());
        Join(rp2_.get(), p2_.get());
    } else {
        Join(rp2_.get(), p2_.get());
        Join(rp1_.get(), p1_.get());
    }
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkRibOutEnqueue(sg, rp1_.get());
    }

    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, MergeWorkPeer1) {
    SchedulingGroup *sg;

    if (condition_) {
        Join(rp1_.get(), p1_.get());
        Join(rp2_.get(), p2_.get());
    } else {
        Join(rp2_.get(), p2_.get());
        Join(rp1_.get(), p1_.get());
    }
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkPeerEnqueue(sg, p1_.get());
    }

    sg = rp2_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkPeerEnqueue(sg, p2_.get());
    }

    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(2 * num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupWorkQueueTest, MergeWorkPeer2) {
    SchedulingGroup *sg;

    if (condition_) {
        Join(rp1_.get(), p1_.get());
        Join(rp2_.get(), p2_.get());
    } else {
        Join(rp2_.get(), p2_.get());
        Join(rp1_.get(), p1_.get());
    }
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    for (int idx = 0; idx < num_items_; ++idx) {
        WorkPeerEnqueue(sg, p1_.get());
    }

    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(num_items_, GetWorkQueueSize(sg));
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

INSTANTIATE_TEST_CASE_P(One, SchedulingGroupWorkQueueTest,
    ::testing::Combine(::testing::Range(1, 9), ::testing::Bool()));

// The 4 booleans represent the following:
//
// (rp1_ + p1, QBULK)   is active/inactive
// (rp1_ + p1, QUPDATE) is active/inactive
// (rp2_ + p2, QBULK)   is active/inactive
// (rp2_ + p2, QUPDATE) is active/inactive

typedef std::tr1::tuple<bool, bool, bool, bool> QueueActiveTestParams;

class SchedulingGroupQueueActiveTest :
    public SchedulingGroupManagerTest,
    public ::testing::WithParamInterface<QueueActiveTestParams> {
protected:
    virtual void SetUp() {
        SchedulingGroupManagerTest::SetUp();
        rp1_.reset(new RibOut(inetvpn_table_, &sgman_, RibExportPolicy()));
        rp2_.reset(new RibOut(inetvpn_table_, &sgman_, RibExportPolicy()));
        p1_.reset(new BgpTestPeer());
        p2_.reset(new BgpTestPeer());
        rp1_p1_bulk_active_ = std::tr1::get<0>(GetParam());
        rp1_p1_update_active_ = std::tr1::get<1>(GetParam());
        rp2_p2_bulk_active_ = std::tr1::get<2>(GetParam());
        rp2_p2_update_active_ = std::tr1::get<3>(GetParam());
    }
    virtual void TearDown() {
        SchedulingGroupManagerTest::TearDown();
    }

    auto_ptr<RibOut> rp1_, rp2_;
    auto_ptr<BgpTestPeer> p1_, p2_;
    bool rp1_p1_bulk_active_, rp1_p1_update_active_;
    bool rp2_p2_bulk_active_, rp2_p2_update_active_;
};

TEST_P(SchedulingGroupQueueActiveTest, Split) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp2_.get(), p2_.get());
    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    if (rp1_p1_bulk_active_)
        SetQueueActive(sg, rp1_.get(), RibOutUpdates::QBULK, p1_.get());
    if (rp1_p1_update_active_)
        SetQueueActive(sg, rp1_.get(), RibOutUpdates::QUPDATE, p1_.get());

    sg = rp2_->GetSchedulingGroup();
    if (rp2_p2_bulk_active_)
        SetQueueActive(sg, rp2_.get(), RibOutUpdates::QBULK, p2_.get());
    if (rp2_p2_update_active_)
        SetQueueActive(sg, rp2_.get(), RibOutUpdates::QUPDATE, p2_.get());

    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    EXPECT_EQ(rp1_p1_bulk_active_,
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QBULK, p1_.get()));
    EXPECT_EQ(rp1_p1_update_active_,
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QUPDATE, p1_.get()));

    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    EXPECT_EQ(rp2_p2_bulk_active_,
        IsQueueActive(sg, rp2_.get(), RibOutUpdates::QBULK, p2_.get()));
    EXPECT_EQ(rp2_p2_update_active_,
        IsQueueActive(sg, rp2_.get(), RibOutUpdates::QUPDATE, p2_.get()));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

TEST_P(SchedulingGroupQueueActiveTest, Merge) {
    SchedulingGroup *sg;

    Join(rp1_.get(), p1_.get());
    Join(rp2_.get(), p2_.get());
    EXPECT_EQ(2, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    if (rp1_p1_bulk_active_)
        SetQueueActive(sg, rp1_.get(), RibOutUpdates::QBULK, p1_.get());
    if (rp1_p1_update_active_)
        SetQueueActive(sg, rp1_.get(), RibOutUpdates::QUPDATE, p1_.get());

    sg = rp2_->GetSchedulingGroup();
    if (rp2_p2_bulk_active_)
        SetQueueActive(sg, rp2_.get(), RibOutUpdates::QBULK, p2_.get());
    if (rp2_p2_update_active_)
        SetQueueActive(sg, rp2_.get(), RibOutUpdates::QUPDATE, p2_.get());

    Join(rp1_.get(), p2_.get());
    EXPECT_EQ(1, sgman_.size());

    sg = rp1_->GetSchedulingGroup();
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    EXPECT_EQ(rp1_p1_bulk_active_,
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QBULK, p1_.get()));
    EXPECT_EQ(rp1_p1_update_active_,
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QUPDATE, p1_.get()));

    sg = rp2_->GetSchedulingGroup();
    EXPECT_EQ(0, GetWorkQueueInvalidSize(sg));
    EXPECT_EQ(rp2_p2_bulk_active_,
        IsQueueActive(sg, rp2_.get(), RibOutUpdates::QBULK, p2_.get()));
    EXPECT_EQ(rp2_p2_update_active_,
        IsQueueActive(sg, rp2_.get(), RibOutUpdates::QUPDATE, p2_.get()));

    EXPECT_FALSE(
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QBULK, p2_.get()));
    EXPECT_FALSE(
        IsQueueActive(sg, rp1_.get(), RibOutUpdates::QUPDATE, p2_.get()));

    Leave(rp1_.get(), p1_.get());
    Leave(rp2_.get(), p2_.get());
    Leave(rp1_.get(), p2_.get());
    EXPECT_EQ(0, sgman_.size());
}

INSTANTIATE_TEST_CASE_P(One, SchedulingGroupQueueActiveTest,
    ::testing::Combine(
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool(),
        ::testing::Bool()));

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
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
