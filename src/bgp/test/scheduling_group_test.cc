/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/scheduling_group.h"

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "db/db.h"
#include "testing/gunit.h"

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
    }

    virtual void TearDown() {
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
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Leave(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(sgman_.PeerGroup(p1.get()) == NULL);
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
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
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_TRUE(sgman_.RibOutGroup(rp1.get()) == NULL);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
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
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg_a);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg_a);

    Join(rp2.get(), p2.get());
    EXPECT_EQ(2, sgman_.size());
    sg_b = rp2->GetSchedulingGroup();
    EXPECT_TRUE(sg_b != NULL);
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg_b);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg_b);

    // Merge happens here.
    Join(rp2.get(), p1.get());
    EXPECT_EQ(1, sgman_.size());
    sg = rp1->GetSchedulingGroup();
    EXPECT_TRUE(sg_a == sg || sg_b == sg);
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.RibOutGroup(rp2.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Join(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    Leave(rp1.get(), p2.get());
    EXPECT_EQ(1, sgman_.size());
    EXPECT_EQ(sgman_.RibOutGroup(rp1.get()), sg);
    EXPECT_EQ(sgman_.PeerGroup(p2.get()), sg);

    // Split happens here.
    Leave(rp2.get(), p1.get());
    EXPECT_EQ(2, sgman_.size());
    sg_a = rp1->GetSchedulingGroup();
    sg_b = rp2->GetSchedulingGroup();
    EXPECT_TRUE(sg_a == sg || sg_b == sg);
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
