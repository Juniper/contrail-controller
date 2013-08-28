/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/scheduling_group.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/inet/inet_table.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;
using namespace ::testing;

static inline void SchedulerStop() {
    TaskScheduler::GetInstance()->Stop();
}

static inline void SchedulerStart() {
    TaskScheduler::GetInstance()->Start();
}

static void SchedulerTerminate() {
    TaskScheduler::GetInstance()->Terminate();
}

static int gbl_peer_index;
static int gbl_ribout_index;

class RibOutUpdatesMock : public RibOutUpdates {
public:
    explicit RibOutUpdatesMock(RibOut *ribout) : RibOutUpdates(ribout) { }

    MOCK_METHOD3(TailDequeue, bool(int,
                                   const RibPeerSet &, RibPeerSet *));
    MOCK_METHOD4(PeerDequeue, bool(int, IPeerUpdate *,
                                   const RibPeerSet &, RibPeerSet *));
};

static const int kPeerCount = 4;

class BgpTestPeer : public IPeerUpdate {
public:
    BgpTestPeer() : index_(gbl_peer_index++) { }
    virtual ~BgpTestPeer() { }

    virtual std::string ToString() const {
        std::ostringstream repr;
        repr << "Peer" << index_;
        return repr.str();
    }

    virtual bool SendUpdate(const uint8_t *msg, size_t msgsize) {
        return true;
    }

private:
    int index_;
};

class SGTest : public ::testing::Test {
protected:

    SGTest()
        : server_(&evm_),
        table_(&db_, "inet.0") {
        table_.Init();
    }

    virtual void SetUp() {
        SchedulerStop();

        gbl_ribout_index = 0;
        CreateRibOut();

        gbl_peer_index = 0;
        for (int idx = 0; idx < kPeerCount; idx++) {
            CreatePeer();
        }

        ASSERT_EQ(1, mgr_.size());
        sg_ = mgr_.RibOutGroup(ribouts_[0]);
        ASSERT_TRUE(sg_ != NULL);
        ASSERT_EQ(kPeerCount, PeerStateCount());
        ASSERT_EQ(1, RibStateCount());
        sg_->CheckInvariants();

        SchedulerStart();

        EXPECT_CALL(*updates_[0], TailDequeue(_, _, _)).Times(0);
        EXPECT_CALL(*updates_[0], PeerDequeue(_, _, _, _)).Times(0);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        CheckInvariants();
        STLDeleteValues(&ribouts_);
        STLDeleteValues(&peers_);

        server_.Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
    }

    void RibOutActive(RibOut *ribout, int qid) {
        ConcurrencyScope scope("db::DBTable");
        sg_->RibOutActive(ribout, qid);
    }

    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueJoin(RibOutUpdates::QBULK, bit);
    }

    void CreateRibOut() {
        RibExportPolicy policy;
        policy.affinity = gbl_ribout_index;
        RibOut *ribout = new RibOut(&table_, &mgr_, policy);
        RibOutUpdatesMock *updates =
            dynamic_cast<RibOutUpdatesMock *>(ribout->updates());
        assert(updates != NULL);
        ribouts_.push_back(ribout);
        updates_.push_back(updates);
    }

    void CreatePeer() {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);
        for (int idx = 0; idx < (int) ribouts_.size(); idx++) {
            RibOutRegister(ribouts_[idx], peer);
        }
    }

    void BuildOddEvenPeerSet(RibPeerSet &peerset, int ro_idx,
            int start_idx, int end_idx, bool even, bool odd) {
        peerset.clear();
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if ((idx % 2 == 0) == even || (idx % 2 == 1) == odd)
                peerset.set(ribouts_[ro_idx]->GetPeerIndex(peers_[idx]));
        }
    }

    void BuildPeerSet(RibPeerSet &peerset, int ro_idx, int idx) {
        BuildOddEvenPeerSet(peerset, ro_idx, idx, idx, true, true);
    }

    void BuildPeerSet(RibPeerSet &peerset, int ro_idx,
            int start_idx, int end_idx) {
        BuildOddEvenPeerSet(peerset, ro_idx, start_idx, end_idx, true, true);
    }

    void BuildEvenPeerSet(RibPeerSet &peerset, int ro_idx,
            int start_idx, int end_idx) {
        BuildOddEvenPeerSet(peerset, ro_idx, start_idx, end_idx, true, false);
    }

    void BuildOddPeerSet(RibPeerSet &peerset, int ro_idx,
            int start_idx, int end_idx) {
        BuildOddEvenPeerSet(peerset, ro_idx, start_idx, end_idx, false, true);
    }

    void SetOddEvenPeerUnblockNow(int start_idx, int end_idx,
            bool even, bool odd) {
        ConcurrencyScope scope("bgp::SendReadyTask");
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if ((idx % 2 == 0) == even || (idx % 2 == 1) == odd)
                sg_->SendReady(peers_[idx]);
        }
    }

    void SetPeerUnblockNow(int idx) {
        SetOddEvenPeerUnblockNow(idx, idx, true, true);
    }

    void SetPeerUnblockNow(int start_idx, int end_idx) {
        SetOddEvenPeerUnblockNow(start_idx, end_idx, true, true);
    }

    void SetEvenPeerUnblockNow(int start_idx, int end_idx) {
        SetOddEvenPeerUnblockNow(start_idx, end_idx, true, false);
    }

    void SetOddPeerUnblockNow(int start_idx, int end_idx) {
        SetOddEvenPeerUnblockNow(start_idx, end_idx, false, true);
    }

    void VerifyOddEvenPeerBlock(int start_idx, int end_idx,
            bool even, bool odd, bool blocked) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if ((idx % 2 == 0) == even || (idx % 2 == 1) == odd)
                EXPECT_EQ(blocked, !sg_->IsSendReady(peers_[idx]));
        }
    }

    void VerifyPeerBlock(int idx, bool blocked) {
        VerifyOddEvenPeerBlock(idx, idx, true, true, blocked);
    }

    void VerifyPeerBlock(int start_idx, int end_idx, bool blocked) {
        VerifyOddEvenPeerBlock(start_idx, end_idx, true, true, blocked);
    }

    void VerifyEvenPeerBlock(int start_idx, int end_idx, bool blocked) {
        VerifyOddEvenPeerBlock(start_idx, end_idx, true, false, blocked);
    }

    void VerifyOddPeerBlock(int start_idx, int end_idx, bool blocked) {
        VerifyOddEvenPeerBlock(start_idx, end_idx, false, true, blocked);
    }

    void VerifyOddEvenPeerInSync(int start_idx, int end_idx,
            bool even, bool odd, bool in_sync) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if ((idx % 2 == 0) == even || (idx % 2 == 1) == odd)
                EXPECT_EQ(in_sync, sg_->PeerInSync(peers_[idx]));
        }
    }

    void VerifyPeerInSync(int idx, bool in_sync) {
        VerifyOddEvenPeerInSync(idx, idx, true, true, in_sync);
    }

    void VerifyEvenPeerInSync(int start_idx, int end_idx, bool in_sync) {
        VerifyOddEvenPeerInSync(start_idx, end_idx, true, false, in_sync);
    }

    void VerifyOddPeerInSync(int start_idx, int end_idx, bool in_sync) {
        VerifyOddEvenPeerInSync(start_idx, end_idx, false, true, in_sync);
    }

    int PeerStateCount() { return sg_->peer_state_imap_.count(); }
    int RibStateCount() { return sg_->rib_state_imap_.count(); }

    void CheckInvariants() {
        mgr_.CheckInvariants();
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    InetTable table_;
    SchedulingGroupManager mgr_;
    SchedulingGroup *sg_;
    std::vector<BgpTestPeer *> peers_;
    std::vector<RibOut *> ribouts_;
    std::vector<RibOutUpdatesMock *> updates_;
};

TEST_F(SGTest, Noop) {
}

//
// One call to RibOutActive causes one call to TailDequeue.
//
TEST_F(SGTest, TailDequeueBasic1a) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect 1 call to TailDequeue.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
}

//
// N calls to RibOutActive cause N calls to TailDequeue.
//
TEST_F(SGTest, TailDequeueBasic1b) {
    const int kTailCount = 5;
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect kTailCount calls to TailDequeue.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(kTailCount)
        .WillRepeatedly(Return(true));

    for (int idx = 0; idx < kTailCount; idx++) {
        RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    }
}

//
// Calling RibOutActive for each qid causes TailDequeue for that qid.
//
TEST_F(SGTest, TailDequeueBasic2a) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect 1 call to TailDequeue for each qid.
    InSequence seq;
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        RibOutActive(ribouts_[0], qid);
    }
}

//
// Multiple calls to RibOutActive for each qid.
//
TEST_F(SGTest, TailDequeueBasic2b) {
    const int kTailCount = 5;
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect kTailCount calls to TailDequeue for each qid.
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(kTailCount)
            .WillRepeatedly(Return(true));
    }

    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int idx = 0; idx < kTailCount; idx++) {
            RibOutActive(ribouts_[0], qid);
        }
    }
}

//
// Setting the blocked mask to include all peers causes them to get blocked.
//
TEST_F(SGTest, TailDequeueAllBlock1) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect 1 call to TailDequeue. Populate the blocked parameter with the
    // mask containing all peers.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// Setting the blocked mask to include all peers during TailDequeue for one
// qid causes TailDequeue for the next qid to be invoked with an empty msync
// mask.
//
TEST_F(SGTest, TailDequeueAllBlock2) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect a call to TailDequeue for QUPDATE. Populate blocked parameter
    // with the mask containing all peers.
    InSequence seq;
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect a call to TailDequeue for QBULK with an empty msync mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);
}

//
// Setting the blocked mask to include all peers during the first TailDequeue
// for a qid causes subsequent TailDequeues for the same qid to be invoked with
// an empty msync mask.
//
TEST_F(SGTest, TailDequeueAllBlock3) {
    const int kTailCount = 5;
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect a call to TailDequeue for QUPDATE. Populate blocked parameter
    // with the mask containing all peers.
    InSequence seq;
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect subsequent calls to TailDequeue for QUPDATE with an empty msync
    // mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(kTailCount-1)
        .WillRepeatedly(Return(false));

    for (int idx = 0; idx < kTailCount; idx++) {
        RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    }
}

//
// TailDequeue sets the blocked mask to contain 1 peer.  When it's invoked
// repeatedly, the msync mask for the next call reflects the blocked peers
// till the mysnc mask is empty.
//
TEST_F(SGTest, TailDequeueProgressiveBlock1) {

    // Expect successive calls to TailDequeue with a msync mask that keeps
    // shrinking one bit at a time.
    InSequence seq;
    for (int vSyncPeerCount = kPeerCount; vSyncPeerCount >= 1;
            vSyncPeerCount--) {
        RibPeerSet sync_peerset, blocked_peerset;
        BuildPeerSet(sync_peerset, 0, 0, vSyncPeerCount-1);
        BuildPeerSet(blocked_peerset, 0, vSyncPeerCount-1);
        bool retval = (vSyncPeerCount == 1) ? false : true;

        EXPECT_CALL(*updates_[0],
            TailDequeue(RibOutUpdates::QUPDATE, sync_peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(blocked_peerset), Return(retval)));
    }

    // Expect the last call to TailDequeue with an empty msync mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    for (int idx = 0; idx <= kPeerCount; idx++) {
        RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    }

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// TailDequeue for one qid blocks all odd numbered peers. Tail dequeue for the
// next qid is invoked with the even numbered peers in the msync bits.  If the
// second TailDequeue blocks the even numbered peers, all peers are now blocked
// at this point.
//
TEST_F(SGTest, TailDequeueProgressiveBlock2) {
    InSequence seq;
    RibPeerSet peerset, even_peerset, odd_peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildEvenPeerSet(even_peerset, 0, 0, kPeerCount-1);
    BuildOddPeerSet(odd_peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue with sync mask containing all peers. Block
    // the odd peers as part of the action.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(odd_peerset), Return(true)));

    // Expect call to TailDequeue with sync mask containing even peers. Block
    // the even peers as part of the action.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, even_peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(even_peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// PeerDequeue is called when peers get unblocked.  Should not get called
// for qid that is not active.
// Since all peers get in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue.
// All blocked peers are unblocked in this test.
//
TEST_F(SGTest, PeerDequeueBasic1a) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue and get all peers into blocked state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for each peer. Return true to get
    // the peers in sync.
    for (int idx = 0; idx < kPeerCount; idx++) {
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QUPDATE, peers_[idx], peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    // Expect 1 call to TailDequeue as a consequence of the peers getting
    // in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Unblock all peers.
    SchedulerStop();
    SetPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called when peers get unblocked.  Should not get called
// for qid that is not active.
// Since some peers get in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue.
// Subset of blocked peers are unblocked in this test.
//
TEST_F(SGTest, PeerDequeueBasic1b) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    RibPeerSet odd_peerset;
    BuildOddPeerSet(odd_peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue and get all peers into blocked state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for odd peers. Return true to get
    // the odd peers in sync.
    for (int idx = 0; idx < kPeerCount; idx++) {
        if (idx % 2 == 0)
            continue;
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QUPDATE, peers_[idx], odd_peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    // Expect 1 call to TailDequeue as a result of the odd peers getting
    // in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, odd_peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Unblock odd peers.
    SchedulerStop();
    SetOddPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called when peers get unblocked.  Should not get called
// for qid that is not active.
// Exactly one peer is unblocked in this test.
// Since the peer gets in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue.
//
TEST_F(SGTest, PeerDequeueBasic1c) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue and get all peers into blocked state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for peer 0. Return true to get the
    // peer in sync.
    RibPeerSet peer0;
    BuildPeerSet(peer0, 0, 0, 0);
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[0], peer0,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Expect 1 call to TailDequeue as a consequence of the peer getting
    // in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peer0,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Unblock peer 0.
    SchedulerStop();
    SetPeerUnblockNow(0);
    SchedulerStart();
}

//
// PeerDequeue is called when peers get unblocked.  Should not get called
// for qid that is not active.
// All blocked peers are unblocked in this test.
// If all the peers stay unsynced after the calls to PeerDequeue, there will
// be no automatic call to TailDequeue.
//
TEST_F(SGTest, PeerDequeueBasic1d) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue and get all peers into blocked state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for each peer. Block the peer and
    // return false to keep the peer not in sync.
    for (int idx = 0; idx < kPeerCount; idx++) {
        RibPeerSet peerbit;
        BuildPeerSet(peerbit, 0, idx);
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QUPDATE, peers_[idx], _,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<3>(peerbit), Return(false)));
    }

    // Expect no calls to TailDequeue since none of the peers are in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock all peers.
    SchedulerStop();
    SetPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called for all active qids when peers get unblocked.
// All blocked peers are unblocked in this test.
// Since all peers get in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue for each qid.
//
TEST_F(SGTest, PeerDequeueBasic2a) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QUPDATE and get all peers into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect call to TailDequeue for QBULK with an empty mysnc mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for each peer for each qid. Return true
    // to get the peers in sync.
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int idx = 0; idx < kPeerCount; idx++) {
            EXPECT_CALL(*updates_[0],
                PeerDequeue(qid, peers_[idx], peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }
    }

    // Expect 1 call to TailDequeue for each qid as a consequence of the peers
    // getting in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Unblock all peers.
    SchedulerStop();
    SetPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called for all active qids when peers get unblocked.
// Subset of blocked peers are unblocked in this test.
// Since some peers get in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue for each qid.
//
TEST_F(SGTest, PeerDequeueBasic2b) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QUPDATE and get all peers into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect call to TailDequeue for QBULK with an empty mysnc mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for even peers for each qid. Return true
    // to get the peers in sync.
    RibPeerSet even_peerset;
    BuildEvenPeerSet(even_peerset, 0, 0, kPeerCount-1);
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int idx = 0; idx < kPeerCount; idx++) {
            if (idx % 2 == 1)
                continue;
            EXPECT_CALL(*updates_[0],
                PeerDequeue(qid, peers_[idx], even_peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }
    }

    // Expect 1 call to TailDequeue for each qid as a consequence of the even
    // peers getting in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, even_peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, even_peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    // Unblock even peers.
    SchedulerStop();
    SetEvenPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called for all active qids when peers get unblocked.
// Subset of blocked peers are unblocked in this test.
// If all the peers stay unsynced after the calls to PeerDequeue, there will
// be no automatic call to TailDequeue.
//
TEST_F(SGTest, PeerDequeueBasic2d) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QUPDATE and get all peers into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect call to TailDequeue for QBULK with an empty mysnc mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for each peer for each qid. Return true
    // for QUPDATE and false for QBULK to make sure that the peers stay out of
    // sync.  Set the blocked mask to include self in case of QBULK.
    for (int idx = 0; idx < kPeerCount; idx++) {
        if (idx % 2 == 1)
            continue;
        RibPeerSet peerbit;
        BuildPeerSet(peerbit, 0, idx);
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QUPDATE, peers_[idx], _,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QBULK, peers_[idx], _,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<3>(peerbit), Return(false)));
    }

    // Expect no calls to TailDequeue since none of the peers are in sync.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock even peers.
    SchedulerStop();
    SetEvenPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called when peers get unblocked.
// Should not get called for qid that is not active.
// Should only get called for peers that were blocked in the first place.
// Subset of blocked peers are blocked/unblocked in this test.
//
TEST_F(SGTest, PeerDequeueBasic3a) {
    RibPeerSet peerset, odd_peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildOddPeerSet(odd_peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QUPDATE and get odd peers into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(odd_peerset), Return(true)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that odd peers are blocked.
    task_util::WaitForIdle();
    VerifyOddPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for odd peers. Return true to get the
    // peers in sync.
    for (int idx = 0; idx < kPeerCount; idx++) {
        if (idx % 2 == 0)
            continue;
        EXPECT_CALL(*updates_[0],
            PeerDequeue(RibOutUpdates::QUPDATE, peers_[idx], peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    // Expect no calls to TailDequeue since the RibOut never got out of sync.
    // The even peers were in sync with the tail marker all the time.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock odd peers.  Even peers were never blocked to begin with.
    SchedulerStop();
    SetOddPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// PeerDequeue is called for all active qids when peers get unblocked.
// Should only get called for peers that were blocked in the first place.
// Subset of blocked peers are blocked/unblocked in this test.
//
TEST_F(SGTest, PeerDequeueBasic3b) {
    RibPeerSet peerset, odd_peerset, even_peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildOddPeerSet(odd_peerset, 0, 0, kPeerCount-1);
    BuildEvenPeerSet(even_peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QUPDATE and get even peers into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(even_peerset), Return(true)));

    // Expect call to TailDequeue for QBULK with a mysnc mask of odd peers.
    // Keep the odd peers in sync by returning true.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, odd_peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

    // Verify that all even peers are blocked.
    task_util::WaitForIdle();
    VerifyEvenPeerBlock(0, kPeerCount-1, true);

    // Expect PeerDequeue to be called for even peers for each qid. Return true
    // to get the peers in sync.
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int idx = 0; idx < kPeerCount; idx++) {
            if (idx % 2 == 1)
                continue;
            EXPECT_CALL(*updates_[0],
                PeerDequeue(qid, peers_[idx], peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }
    }

    // Expect no calls to TailDequeue since the RibOut never got out of sync.
    // The odd peers were in sync with the tail marker all the time.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock all even peers.
    SchedulerStop();
    SetEvenPeerUnblockNow(0, kPeerCount-1);
    SchedulerStart();
}

//
// If the peer is already blocked by the time the scheduler gets around to
// processing the peer, PeerDequeue should not be called for that peer.
//
// We assume that PeerDequeue for peer 0 will be called before PeerDequeue
// for peer 1. The call for peer 0 blocks both peers, so PeerDequeue should
// not be called for peer 1.
//
TEST_F(SGTest, PeerDequeueAlreadyBlocked) {
    RibPeerSet peerset, peer01;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildPeerSet(peer01, 0, 0, 1);

    // Expect call to TailDequeue for QUPDATE and get peers 0,1 into blocked
    // state.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peer01), Return(true)));

    RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);

    // Verify that peer 0 and 1 are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, 1, true);
    VerifyPeerBlock(2, kPeerCount-1, false);

    // Expect PeerDequeue to be called for peer 0.  Block both peers and
    // return false.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(peer01), Return(false)));

    // Expect PeerDequeue to be not called for peer 1.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[1], _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock peer 0 and 1.
    SchedulerStop();
    SetPeerUnblockNow(0, 1);
    SchedulerStart();

    // Verify that peers 0 and 1 are blocked again.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, 1, true);
}

// Peer 0 is blocked and has qids 0 and 1 as active.
class SGPeerDequeueBlocks1 : public SGTest {
protected:

    virtual void SetUp() {
        SGTest::SetUp();

        RibPeerSet peerset, peer0, others;
        BuildPeerSet(peerset, 0, 0, kPeerCount-1);
        BuildPeerSet(peer0, 0, 0, 0);
        BuildPeerSet(others, 0, 1, kPeerCount-1);

        EXPECT_CALL(*updates_[0],
            TailDequeue(RibOutUpdates::QUPDATE, peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(peer0), Return(true)));
        EXPECT_CALL(*updates_[0],
            TailDequeue(RibOutUpdates::QBULK, others,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));

        RibOutActive(ribouts_[0], RibOutUpdates::QUPDATE);
        RibOutActive(ribouts_[0], RibOutUpdates::QBULK);

        // Verify that peer 0 is blocked.
        task_util::WaitForIdle();
        VerifyPeerBlock(0, true);
        VerifyPeerBlock(1, kPeerCount-1, false);
    }

    virtual void TearDown() {
        SGTest::TearDown();
    }
};

//
// PeerDequeue is called for the first active qid when peer gets unblocked.
// If that call blocks the peer again, PeerDequeue for other active qids is
// not called.
//
TEST_F(SGPeerDequeueBlocks1, BlockFirstQid) {
    RibPeerSet peerset, peer0;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildPeerSet(peer0, 0, 0, 0);

    // Expect PeerDequeue to be called for peer 0 for QUPDATE. Block the peer
    // and return false.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(peer0), Return(false)));

    // Expect PeerDequeue to be not called for QBULK.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QBULK, peers_[0], _,
                    Property(&RibPeerSet::empty, true)))
        .Times(0);

    // Unblock peer 0.
    SchedulerStop();
    SetPeerUnblockNow(0);
    SchedulerStart();

    // Verify that peer 0 is blocked again.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, 0, true);
}

//
// PeerDequeue is called for all active qids when peer gets unblocked.  If
// the call for the last qid blocks the peer again, it's not marked in sync.
//
TEST_F(SGPeerDequeueBlocks1, BlockLastQid1) {
    RibPeerSet peerset, peer0;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildPeerSet(peer0, 0, 0, 0);

    // Expect PeerDequeue to be called for peer 0 for QUPDATE and QBULK. Block
    // the peer and return false when processing QBULK.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QBULK, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(peer0), Return(false)));

    // Unblock peer 0.
    SchedulerStop();
    SetPeerUnblockNow(0);
    SchedulerStart();

    // Verify that peer 0 is blocked again and is not in sync.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, true);
    VerifyPeerInSync(0, false);
}

//
// PeerDequeue is called for all active qids when peer gets unblocked.  If
// the call for the last qid blocks the peer again, but still returns true
// because some other peers got merged with the tail marker, the peer does
// not get marked in sync.
//
TEST_F(SGPeerDequeueBlocks1, BlockLastQid2) {
    RibPeerSet peerset, peer0;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);
    BuildPeerSet(peer0, 0, 0, 0);

    // Expect PeerDequeue to be called for peer 0 for QUPDATE and QBULK. Block
    // the peer and return true when processing QBULK.
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QUPDATE, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(*updates_[0],
        PeerDequeue(RibOutUpdates::QBULK, peers_[0], peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(peer0), Return(true)));

    // Unblock peer 0.
    SchedulerStop();
    SetPeerUnblockNow(0);
    SchedulerStart();

    // Verify that peer 0 is blocked again and is not in sync.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, true);
    VerifyPeerInSync(0, false);
}

static const int kRiboutCount = 4;

class SGMultiRibOutTest : public SGTest {
protected:

    virtual void SetUp() {
        SchedulerStop();

        gbl_ribout_index = 0;
        for (int ro_idx = 0; ro_idx < kPeerCount; ro_idx++) {
            CreateRibOut();
        }

        gbl_peer_index = 0;
        for (int idx = 0; idx < kPeerCount; idx++) {
            CreatePeer();
        }

        ASSERT_EQ(1, mgr_.size());
        sg_ = mgr_.RibOutGroup(ribouts_[0]);
        ASSERT_TRUE(sg_ != NULL);
        ASSERT_EQ(kPeerCount, PeerStateCount());
        ASSERT_EQ(kRiboutCount, RibStateCount());
        sg_->CheckInvariants();

        SchedulerStart();
        Initialize();
    }

    virtual void TearDown() {
        SGTest::TearDown();
    }

    void Initialize() {
        task_util::WaitForIdle();
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            Mock::VerifyAndClearExpectations(updates_[ro_idx]);
            EXPECT_CALL(*updates_[ro_idx], TailDequeue(_, _, _)).Times(0);
            EXPECT_CALL(*updates_[ro_idx], PeerDequeue(_, _, _, _)).Times(0);
        }
    }
};

TEST_F(SGMultiRibOutTest, Noop1) {
}

//
// Calling RibOutActive once for each RibOutUpdates results in a TailDequeue
// for the RibOutUpdates.
//
TEST_F(SGMultiRibOutTest, TailDequeueBasic1a) {

    // Expect 1 call to TailDequeue for each RibOut.
    InSequence seq;
    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        RibPeerSet peerset;
        BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);

        EXPECT_CALL(*updates_[ro_idx],
            TailDequeue(RibOutUpdates::QUPDATE, peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        RibOutActive(ribouts_[ro_idx], RibOutUpdates::QUPDATE);
    }
}

//
// Calling RibOutActive once for even RibOutUpdates results in a TailDequeue
// for the even RibOutUpdates.
//
TEST_F(SGMultiRibOutTest, TailDequeueBasic1b) {

    // Expect 1 call to TailDequeue for even RibOuts.
    InSequence seq;
    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        if (ro_idx % 2 == 1)
            continue;
        RibPeerSet peerset;
        BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);

        EXPECT_CALL(*updates_[ro_idx],
            TailDequeue(RibOutUpdates::QUPDATE, peerset,
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(true));
    }

    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        if (ro_idx % 2 == 1)
            continue;
        RibOutActive(ribouts_[ro_idx], RibOutUpdates::QUPDATE);
    }
}

//
// Calling RibOutActive once for each RibOutUpdates for each qid results in a
// TailDequeue for the RibOutUpdates for those qids.
//
TEST_F(SGMultiRibOutTest, TailDequeueBasic2a) {

    // Expect 1 call to TailDequeue for each RibOut for each qid.
    InSequence seq;
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);

            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }
    }

    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibOutActive(ribouts_[ro_idx], qid);
        }
    }
}

//
// Calling RibOutActive once for odd RibOutUpdates for each qid results in a
// TailDequeue for the odd RibOutUpdates for those qids.
//
TEST_F(SGMultiRibOutTest, TailDequeueBasic2b) {

    // Expect 1 call to TailDequeue for odd RibOuts for each qid.
    InSequence seq;
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            if (ro_idx % 2 == 0)
                continue;
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);

            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }
    }

    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            if (ro_idx % 2 == 0)
                continue;
            RibOutActive(ribouts_[ro_idx], qid);
        }
    }
}

//
// TailDequeue sets the blocked mask to contain 1 peer when processing the
// last RibOutUpdates.  When doing this repeatedly, the msync mask for the
// next TailDequeue call to all the RibOutUpdates reflects the blocked peers
// till the mysnc mask becomes empty.
//
TEST_F(SGMultiRibOutTest, TailDequeueProgressiveBlock1) {

    // Expect successive calls to TailDequeue with a msync mask that keeps
    // shrinking one bit at a time.  We block one peer when processing the
    // last RibOut.
    InSequence seq;
    for (int vSyncPeerCount = kPeerCount; vSyncPeerCount >= 1;
            vSyncPeerCount--) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            bool retval = true;
            RibPeerSet sync_peerset, blocked_peerset;
            BuildPeerSet(sync_peerset, ro_idx, 0, vSyncPeerCount-1);

            if (ro_idx == kRiboutCount - 1) {
                BuildPeerSet(blocked_peerset, ro_idx, vSyncPeerCount-1);
                retval = (vSyncPeerCount == 1) ? false : true;
            }
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(RibOutUpdates::QUPDATE, sync_peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(DoAll(SetArgPointee<2>(blocked_peerset),
                                Return(retval)));
        }
    }

    for (int idx = 0; idx < kPeerCount; idx++) {
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibOutActive(ribouts_[ro_idx], RibOutUpdates::QUPDATE);
        }
    }

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// If the call to TailDequeue for the first RibOutUpdates blocks all peers,
// this is reflected in the calls to TailDequeue for the other RibOutUpdates.
//
TEST_F(SGMultiRibOutTest, TailDequeueAllBlock1a) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for the first RibOut and get all peers into
    // blocked state.
    InSequence seq;
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect calls to TailDequeue for the other RibOuts with an empty mysnc
    // mask.
    for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
        EXPECT_CALL(*updates_[ro_idx],
            TailDequeue(RibOutUpdates::QUPDATE,
                        Property(&RibPeerSet::empty, true),
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(false));
    }

    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        RibOutActive(ribouts_[ro_idx], RibOutUpdates::QUPDATE);
    }

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// If the call to TailDequeue for one qid in the first RibOutUpdates blocks all
// peers, this is reflected in the calls to TailDequeue for all RibOutUpdates
// for a different qid.
//
TEST_F(SGMultiRibOutTest, TailDequeueAllBlock1b) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QBULK for the first RibOut and get all
    // peers into blocked state.
    InSequence seq;
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect calls to TailDequeue for QUPDATE for all the RibOuts with an
    // empty mysnc mask.
    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        EXPECT_CALL(*updates_[ro_idx],
            TailDequeue(RibOutUpdates::QUPDATE,
                        Property(&RibPeerSet::empty, true),
                        Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(Return(false));
    }

    RibOutActive(ribouts_[0], RibOutUpdates::QBULK);
    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        RibOutActive(ribouts_[ro_idx], RibOutUpdates::QUPDATE);
    }

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// If the call to TailDequeue for one qid in the first RibOutUpdates blocks all
// peers, this is reflected in the calls to TailDequeue for all RibOutUpdates
// for all qids.
//
TEST_F(SGMultiRibOutTest, TailDequeueAllBlock1c) {
    RibPeerSet peerset;
    BuildPeerSet(peerset, 0, 0, kPeerCount-1);

    // Expect call to TailDequeue for QBULK for the first RibOut and get all
    // peers into blocked state.
    InSequence seq;
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QBULK, peerset,
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

    // Expect call to TailDequeue for QUPDATE for the first RibOuts with an
    // empty mysnc mask.
    EXPECT_CALL(*updates_[0],
        TailDequeue(RibOutUpdates::QUPDATE,
                    Property(&RibPeerSet::empty, true),
                    Property(&RibPeerSet::empty, true)))
        .Times(1)
        .WillOnce(Return(false));

    // Expect calls to TailDequeue for the other RibOuts for all qids with an
    // empty mysnc mask.
    for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                qid++) {
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid,
                            Property(&RibPeerSet::empty, true),
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(false));
        }
    }

    for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                qid++) {
            RibOutActive(ribouts_[ro_idx], qid);
        }
    }

    // Verify that all peers are blocked.
    task_util::WaitForIdle();
    VerifyPeerBlock(0, kPeerCount-1, true);
}

//
// All RibOuts are active for all peers for the qid in question.  If all the
// peers are unblocked PeerDequeue for each peer should process the qid in
// question for all RibOuts.
// Since all peers get in sync after the calls to PeerDequeue, there will
// be one automatic call to TailDequeue per RibOut for the qid in question.
//
TEST_F(SGMultiRibOutTest, PeerDequeueBasic1a) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        Initialize();

        InSequence seq;
        RibPeerSet peerset;
        BuildPeerSet(peerset, 0, 0, kPeerCount-1);

        // Expect call to TailDequeue for the first RibOut and get all peers
        // into blocked state.
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

        // Expect calls to TailDequeue for the other RibOuts with an empty
        // mysnc mask.
        for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, Property(&RibPeerSet::empty, true),
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(false));
        }

        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibOutActive(ribouts_[ro_idx], qid);
        }

        // Verify that all peers are blocked.
        task_util::WaitForIdle();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for each peer for each RibOut.
        // Return true to get the peers in sync.
        for (int idx = 0; idx < kPeerCount; idx++) {
            for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
                RibPeerSet peerset;
                BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
                EXPECT_CALL(*updates_[ro_idx],
                    PeerDequeue(qid, peers_[idx], peerset,
                                Property(&RibPeerSet::empty, true)))
                    .Times(1)
                    .WillOnce(Return(true));
            }
        }

        // Expect 1 call to TailDequeue for each RibOut as a consequence of
        // the peers getting in sync.
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();
    }
}

//
// Even RibOuts are active for all peers for the qid in question.  If all the
// peers are unblocked PeerDequeue for each peer should process the qid in
// question for even RibOuts.
// Since all peers get in sync after the calls to PeerDequeue, there will
// be 1 automatic call to TailDequeue per even RibOut for the qid in question.
//
TEST_F(SGMultiRibOutTest, PeerDequeueBasic1b) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        Initialize();

        InSequence seq;
        RibPeerSet peerset;
        BuildPeerSet(peerset, 0, 0, kPeerCount-1);

        // Expect call to TailDequeue for the first RibOut and get all peers
        // into blocked state.
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

        // Expect calls to TailDequeue for the even RibOuts with an empty
        // mysnc mask.
        for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
            if (ro_idx % 2 == 1)
                continue;
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, Property(&RibPeerSet::empty, true),
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(false));
        }

        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            if (ro_idx % 2 == 1)
                continue;
            RibOutActive(ribouts_[ro_idx], qid);
        }

        // Verify that all peers are blocked.
        task_util::WaitForIdle();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for each peer for even RibOuts.
        // Return true to get the peers in sync.
        for (int idx = 0; idx < kPeerCount; idx++) {
            for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
                if (ro_idx % 2 == 1)
                    continue;
                RibPeerSet peerset;
                BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
                EXPECT_CALL(*updates_[ro_idx],
                    PeerDequeue(qid, peers_[idx], peerset,
                                Property(&RibPeerSet::empty, true)))
                    .Times(1)
                    .WillOnce(Return(true));
            }
        }

        // Expect 1 call to TailDequeue for even RibOuts as a consequence of
        // the peers getting in sync.
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            if (ro_idx % 2 == 1)
                continue;
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();
    }
}

//
// All RibOuts are active for all peers for the qid in question.  If the call
// to PeerDequeue for the first RibOut blocks the peer, PeerDequeue should not
// be called for the rest of the RibOuts.
//
// If all the peers are unblocked again, PeerDequeue should get called for all
// the RibOuts and all the peers should become in sync.
//
// Assumes that PeerDequeue will be called in order of peer index.
// Assumes that RibOuts will be processed in order of ribout index.
//
TEST_F(SGMultiRibOutTest, PeerDequeueBlocks1a) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        Initialize();

        InSequence seq;
        RibPeerSet peerset;
        BuildPeerSet(peerset, 0, 0, kPeerCount-1);

        // Expect call to TailDequeue for the first RibOut and get all peers
        // into blocked state.
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

        // Expect calls to TailDequeue for the other RibOuts with an empty
        // mysnc mask.
        for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, Property(&RibPeerSet::empty, true),
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(false));
        }

        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibOutActive(ribouts_[ro_idx], qid);
        }

        // Verify that all peers are blocked.
        task_util::WaitForIdle();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for each peer for the first RibOut.
        // Block the peer in question and return false for each call.
        for (int idx = 0; idx < kPeerCount; idx++) {
            RibPeerSet peerbit, peerset;
            BuildPeerSet(peerbit, 0, idx);
            BuildPeerSet(peerset, 0, idx, kPeerCount-1);
            EXPECT_CALL(*updates_[0],
                PeerDequeue(qid, peers_[idx], peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(DoAll(SetArgPointee<3>(peerbit), Return(false)));
        }

        // Expect PeerDequeue to be not called for any peer for the rest of the
        // RibOuts.
        for (int idx = 0; idx < kPeerCount; idx++) {
            for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
                EXPECT_CALL(*updates_[0],
                    PeerDequeue(qid, peers_[idx], _,
                                Property(&RibPeerSet::empty, true)))
                    .Times(0);
            }
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();

        // Verify expectations and initialize state again.
        Initialize();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for all peers for all the RibOuts.
        // Return true to get the peers in sync.
        for (int idx = 0; idx < kPeerCount; idx++) {
            for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
                RibPeerSet peerset;
                BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
                EXPECT_CALL(*updates_[ro_idx],
                    PeerDequeue(qid, peers_[idx], peerset,
                                Property(&RibPeerSet::empty, true)))
                    .Times(1)
                    .WillOnce(Return(true));
            }
        }

        // Expect 1 call to TailDequeue for each RibOut as a consequence of
        // the peers getting in sync.
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();
    }
}


//
// All RibOuts are active for all peers for the qid in question.  If the call
// to PeerDequeue for the last RibOut blocks the peer, the peers should not be
// in sync.
//
// If all the peers are unblocked again, PeerDequeue should get called for the
// last RibOut and all the peers should become in sync.
//
// Assumes that PeerDequeue will be called in order of peer index.
// Assumes that RibOuts will be processed in order of ribout index.
//
TEST_F(SGMultiRibOutTest, PeerDequeueBlocks1b) {
    for (int qid = RibOutUpdates::QFIRST; qid <= RibOutUpdates::QFIRST; qid++) {
        Initialize();

        InSequence seq;
        RibPeerSet peerset;
        BuildPeerSet(peerset, 0, 0, kPeerCount-1);

        // Expect call to TailDequeue for the first RibOut and get all peers
        // into blocked state.
        EXPECT_CALL(*updates_[0],
            TailDequeue(qid, peerset, Property(&RibPeerSet::empty, true)))
            .Times(1)
            .WillOnce(DoAll(SetArgPointee<2>(peerset), Return(false)));

        // Expect calls to TailDequeue for the other RibOuts with an empty
        // mysnc mask.
        for (int ro_idx  = 1; ro_idx < kRiboutCount; ro_idx++) {
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, Property(&RibPeerSet::empty, true),
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(false));
        }

        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibOutActive(ribouts_[ro_idx], qid);
        }

        // Verify that all peers are blocked.
        task_util::WaitForIdle();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for each peer for the all RibOuts.
        // Block the peer in question and return false for the last RibOut.
        for (int idx = 0; idx < kPeerCount; idx++) {
            for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
                RibPeerSet peerset;
                BuildPeerSet(peerset, ro_idx, idx, kPeerCount-1);
                if (ro_idx != kRiboutCount - 1) {
                    EXPECT_CALL(*updates_[ro_idx],
                        PeerDequeue(qid, peers_[idx], peerset,
                                    Property(&RibPeerSet::empty, true)))
                        .Times(1)
                        .WillOnce(Return(true));
                } else {
                    RibPeerSet peerbit;
                    BuildPeerSet(peerbit, ro_idx, idx);
                    EXPECT_CALL(*updates_[ro_idx],
                        PeerDequeue(qid, peers_[idx], peerset,
                                    Property(&RibPeerSet::empty, true)))
                        .Times(1)
                        .WillOnce(DoAll(SetArgPointee<3>(peerbit),
                                        Return(false)));
                }
            }
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();

        // Verify expectations and initialize state again.
        Initialize();
        VerifyPeerBlock(0, kPeerCount-1, true);

        // Expect PeerDequeue to be called for all peers for the last RibOut.
        // Return true to get the peers in sync.
        for (int idx = 0; idx < kPeerCount; idx++) {
            RibPeerSet peerset;
            BuildPeerSet(peerset, kRiboutCount-1, 0, kPeerCount-1);
            EXPECT_CALL(*updates_[kRiboutCount-1],
                PeerDequeue(qid, peers_[idx], peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }

        // Expect 1 call to TailDequeue for each RibOut as a consequence of
        // the peers getting in sync.
        for (int ro_idx  = 0; ro_idx < kRiboutCount; ro_idx++) {
            RibPeerSet peerset;
            BuildPeerSet(peerset, ro_idx, 0, kPeerCount-1);
            EXPECT_CALL(*updates_[ro_idx],
                TailDequeue(qid, peerset,
                            Property(&RibPeerSet::empty, true)))
                .Times(1)
                .WillOnce(Return(true));
        }

        // Unblock all peers.
        SchedulerStop();
        SetPeerUnblockNow(0, kPeerCount-1);
        SchedulerStart();
    }
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<RibOutUpdates>(
        boost::factory<RibOutUpdatesMock *>());
    ::testing::InitGoogleMock(&argc, argv);
    bool success = RUN_ALL_TESTS();
    SchedulerTerminate();
    return success;
}
