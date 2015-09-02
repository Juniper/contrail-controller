/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_ribout_updates_test_h
#define ctrlplane_bgp_ribout_updates_test_h

#include "bgp/bgp_ribout_updates.h"

#include <string>
#include <vector>

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_export.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_update.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/scheduling_group.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet/inet_route.h"
#include "db/db.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

static inline void SchedulerStop() {
    TaskScheduler::GetInstance()->Stop();
}

static inline void SchedulerStart() {
    TaskScheduler::GetInstance()->Start();
}

static int gbl_index;

static const int kPeerCount = 4;
static const int kRouteCount = 12;
static const int kAttrCount = 12;

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
        send_block_ = block_set_.find(count_) != block_set_.end();
        return !send_block_;
    }

    void set_block_at(int step) {
        block_set_.insert(step);
    }
    void clear_blocks() {
        block_set_.clear();
    }

    int update_count() const { return count_; }
    void clear_update_count() { count_ = 0; }
    bool send_block() const { return send_block_; }

private:
    int index_;
    std::set<int> block_set_;
    int count_;
    bool send_block_;
};

class MessageMock : public Message {
public:
    MessageMock() : route_count_(1) { }
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *attr) {
        return (++route_count_ == 1000 ? false : true);
    }
    virtual void Finish() {
    }
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp) {
        return NULL;
    }

private:
    int route_count_;
};

// Set to true if/when debugging the tests.
const static bool use_bgp_messages = false;

class MsgBuilderMock : public BgpMessageBuilder {
public:
    MsgBuilderMock() : msg_count_(0) { }
    virtual ~MsgBuilderMock() { }

    virtual Message *Create(const BgpTable *table,
                            const RibOutAttr *attr,
                            const BgpRoute *route) const {
        msg_count_++;
        if (use_bgp_messages) {
            return BgpMessageBuilder::Create(table, attr, route);
        } else {
            return new MessageMock();
        }
    }

    int msg_count() const { return msg_count_; }
    void clear_msg_count() { msg_count_ = 0; }

private:
    mutable int msg_count_;
};

class RibOutUpdatesTest : public ::testing::Test {
protected:

    enum Step {
        STEP_0 = 0,
        STEP_1,
        STEP_2,
        STEP_3,
        STEP_4,
        STEP_5,
        STEP_6,
        STEP_7,
        STEP_8,
        STEP_9,
    };

    enum Count {
        COUNT_0 = 0,
        COUNT_1,
        COUNT_2,
        COUNT_3,
        COUNT_4,
        COUNT_5,
        COUNT_6,
        COUNT_7,
        COUNT_8,
        COUNT_9,
    };

    RibOutUpdatesTest()
        : server_(&evm_),
        table_(&db_, "inet.0"),
        ribout_(&table_, &mgr_, RibExportPolicy()),
        dflt_prefix_(Ip4Prefix::FromString("0/0")),
        dflt_rt_(dflt_prefix_) {
        table_.Init();
        dflt_rt_.set_onlist();
        export_ = ribout_.bgp_export();
        updates_ = ribout_.updates();
        updates_->SetMessageBuilder(&builder_);
    }

    virtual void SetUp() {
        SchedulerStop();

        gbl_index = 0;
        for (int idx = 0; idx < kPeerCount; idx++) {
            CreatePeer();
        }

        ASSERT_EQ(1, mgr_.size());
        sg_ = mgr_.RibOutGroup(&ribout_);
        ASSERT_TRUE(sg_ != NULL);

        for (int idx = 0; idx < kRouteCount; idx++) {
            CreateRoute(idx);
        }
        CreateAttrs();
    }

    virtual void TearDown() {
        server_.Shutdown();
        DrainAndDeleteDBState();
        SchedulerStart();
        task_util::WaitForIdle();
        STLDeleteValues(&peers_);
        STLDeleteValues(&routes_);
    }

    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueJoin(RibOutUpdates::QBULK, bit);
    }

    void CreateAttrs() {
        BgpAttr *attribute;
        attribute = new BgpAttr(server_.attr_db());
        attribute->set_med(101);
        attrA_ = server_.attr_db()->Locate(attribute);
        roattrA_.set_attr(attrA_);

        attribute = new BgpAttr(server_.attr_db());
        attribute->set_med(102);
        attrB_ = server_.attr_db()->Locate(attribute);
        roattrB_.set_attr(attrB_);

        attribute = new BgpAttr(server_.attr_db());
        attribute->set_med(103);
        attrC_ = server_.attr_db()->Locate(attribute);
        roattrC_.set_attr(attrC_);

        attribute = new BgpAttr(server_.attr_db());
        attribute->set_med(199);
        attrZ_ = server_.attr_db()->Locate(attribute);
        roattrZ_.set_attr(attrZ_);

        for (int i = 0; i < kAttrCount; i++) {
            attribute = new BgpAttr(server_.attr_db());
            attribute->set_med(1000 + i);
            attr_.push_back(server_.attr_db()->Locate(attribute));
        }

        for (int i = 0; i < kAttrCount; i++) {
            attribute = new BgpAttr(server_.attr_db());
            attribute->set_med(2000 + i);
            alt_attr_.push_back(server_.attr_db()->Locate(attribute));
        }
    }

    void CreatePeer() {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);
        RibOutRegister(&ribout_, peer);
    }

    void CreateRoute(int idx) {
        ASSERT_TRUE(idx <= 65535);
        std::ostringstream repr;
        repr << "10." << idx/256 << "." << idx%256 << "/24";
        Ip4Prefix prefix = Ip4Prefix::FromString(repr.str());
        InetRoute *route = new InetRoute(prefix);
        route->set_onlist();
        routes_.push_back(route);
    }

    void BuildPeerSet(RibPeerSet &peerset, int start_idx, int end_idx) {
        peerset.clear();
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            peerset.set(ribout_.GetPeerIndex(peers_[idx]));
        }
    }

    void PrependUpdateInfo(UpdateInfoSList &uinfo_slist, BgpAttrPtr attrX,
            int label, int start_idx, int end_idx) {
        RibPeerSet target;
        BuildPeerSet(target, start_idx, end_idx);
        RibOutAttr roattr(attrX.get(), label);
        UpdateInfo *uinfo = new UpdateInfo(target, roattr);
        uinfo_slist->push_front(*uinfo);
    }

    void PrependUpdateInfo(UpdateInfoSList &uinfo_slist, BgpAttrPtr attrX,
            int start_idx, int end_idx) {
        PrependUpdateInfo(uinfo_slist, attrX, 0, start_idx, end_idx);
    }

    void BuildUpdateInfo(UpdateInfoSList &uinfo_slist,
            std::vector<BgpAttrPtr> &attrvec, int label,
            int start_idx, int end_idx) {
        for (int idx = start_idx; idx <= end_idx; idx++) {
            RibPeerSet target;
            target.set(ribout_.GetPeerIndex(peers_[idx]));
            RibOutAttr roattr(attrvec[idx].get(), label);
            UpdateInfo *uinfo = new UpdateInfo(target, roattr);
            uinfo_slist->push_front(*uinfo);
        }
    }

    void BuildUpdateInfo(UpdateInfoSList &uinfo_slist,
            std::vector<BgpAttrPtr> &attrvec, int start_idx, int end_idx) {
        BuildUpdateInfo(uinfo_slist, attrvec, 0, start_idx, end_idx);
    }

    void CloneUpdateInfo(UpdateInfoSList &src_uinfo_slist,
            UpdateInfoSList &dst_uinfo_slist) {
        for (UpdateInfoSList::List::iterator iter = src_uinfo_slist->begin();
             iter != src_uinfo_slist->end(); iter++) {
            UpdateInfo *uinfo = new UpdateInfo(iter->target, iter->roattr);
            dst_uinfo_slist->push_front(*uinfo);
        }
    }

    void FlushUpdateInfo(UpdateInfoSList &uinfo_slist) {
        uinfo_slist->clear_and_dispose(UpdateInfoDisposer());
    }

    void SetPeerBlock(int idx, Step step) {
        ASSERT_TRUE(step >= STEP_1);
        ASSERT_TRUE(idx < (int) peers_.size());
        peers_[idx]->set_block_at(step);
    }

    void SetPeerBlock(int start_idx, int end_idx, Step step) {
        ASSERT_TRUE(step >= STEP_1);
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            peers_[idx]->set_block_at(step);
        }
    }

    void SetEvenPeerBlock(int start_idx, int end_idx, Step step) {
        ASSERT_TRUE(step >= STEP_1);
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if (idx % 2 == 0)
                peers_[idx]->set_block_at(step);
        }
    }

    void SetOddPeerBlock(int start_idx, int end_idx, Step step) {
        ASSERT_TRUE(step >= STEP_1);
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if (idx % 2 == 1)
                peers_[idx]->set_block_at(step);
        }
    }

    void SetPeerBlockNow(int start_idx, int end_idx) {
        ConcurrencyScope scope("bgp::SendTask");
        RibPeerSet blocked;
        for (int idx = start_idx; idx <= end_idx; idx++) {
            blocked.set(ribout_.GetPeerIndex(peers_[idx]));
        }
        sg_->SetSendBlocked(&ribout_, sg_->rib_state_imap_.Find(&ribout_),
                RibOutUpdates::QUPDATE, blocked);
    }

    void SetPeerUnblockNow(int idx) {
        ConcurrencyScope scope("bgp::SendReadyTask");
        ASSERT_TRUE(idx < (int) peers_.size());
        sg_->SendReady(peers_[idx]);
    }

    void SetPeerUnblockNow(int start_idx, int end_idx) {
        ConcurrencyScope scope("bgp::SendReadyTask");
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            sg_->SendReady(peers_[idx]);
        }
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

    void VerifyPeerInSync(int start_idx, int end_idx, bool in_sync) {
        VerifyOddEvenPeerInSync(start_idx, end_idx, true, true, in_sync);
    }

    void VerifyEvenPeerInSync(int start_idx, int end_idx, bool in_sync) {
        VerifyOddEvenPeerInSync(start_idx, end_idx, true, false, in_sync);
    }

    void VerifyOddPeerInSync(int start_idx, int end_idx, bool in_sync) {
        VerifyOddEvenPeerInSync(start_idx, end_idx, false, true, in_sync);
    }

    void ClearAllPeerBlocks(int start_idx, int end_idx) {
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            peers_[idx]->clear_blocks();
        }
    }

    void ClearPeerCount(int start_idx, int end_idx) {
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            peers_[idx]->clear_update_count();
        }
    }

    RouteUpdate *BuildRouteUpdate(BgpRoute *route, UpdateInfoSList &uinfo_slist,
            int qid = RibOutUpdates::QUPDATE) {
        ConcurrencyScope scope("db::DBTable");
        RouteUpdate *rt_update = new RouteUpdate(route, qid);
        rt_update->SetUpdateInfo(uinfo_slist);
        updates_->Enqueue(route, rt_update);
        return rt_update;
    }

    void EnqueueDefaultRoute() {
        UpdateInfoSList uinfo_slist;
        PrependUpdateInfo(uinfo_slist, attrZ_, 0, (int) peers_.size()-1);
        BuildRouteUpdate(&dflt_rt_, uinfo_slist);
    }

    void VerifyDefaultRoute() {
        RouteState *rstate = ExpectRouteState(&dflt_rt_);
        VerifyHistory(rstate, attrZ_, 0, (int) peers_.size()-1);
    }

    void ExpectNullDBState(BgpRoute *route) {
        DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
        EXPECT_TRUE(dbstate == NULL);
    }

    RouteState *ExpectRouteState(BgpRoute *route) {
        DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        EXPECT_TRUE(rstate != NULL);
        return rstate;
    }

    RouteUpdate *ExpectRouteUpdate(BgpRoute *route,
            int qid = RibOutUpdates::QUPDATE) {
        DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
        RouteUpdate *rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        EXPECT_TRUE(rt_update != NULL);
        EXPECT_EQ(qid, rt_update->queue_id());
        return rt_update;
    }

    void VerifyUpdates(RouteUpdate *rt_update, BgpAttrPtr attrX,
            int start_idx, int end_idx, int count = 1) {
        RibPeerSet uinfo_peerset;
        BuildPeerSet(uinfo_peerset, start_idx, end_idx);
        EXPECT_TRUE(rt_update != NULL);
        EXPECT_EQ(count, rt_update->Updates()->size());
        RibOutAttr roattrX(attrX.get(), 0);
        const UpdateInfo *uinfo = rt_update->FindUpdateInfo(roattrX);
        EXPECT_TRUE(uinfo != NULL);
        EXPECT_TRUE(uinfo->target == uinfo_peerset);
    }

    void VerifyUpdates(RouteUpdate *rt_update, std::vector<BgpAttrPtr> &attrvec,
            int start_idx, int end_idx) {
        EXPECT_EQ(end_idx-start_idx+1, rt_update->Updates()->size());

        for (int idx = start_idx; idx <= end_idx; idx++) {
            RibOutAttr roattrX(attrvec[idx].get(), 0);
            const UpdateInfo *uinfo = rt_update->FindUpdateInfo(roattrX);
            EXPECT_TRUE(uinfo != NULL);
            RibPeerSet target;
            target.set(ribout_.GetPeerIndex(peers_[idx]));
            EXPECT_TRUE(uinfo->target == target);
        }
    }

    void VerifyHistory(RouteState *rstate, RibOutAttr roattrX,
            int start_idx, int end_idx, int count = 1) {
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        EXPECT_EQ(count, rstate->Advertised()->size());
        const AdvertiseInfo *ainfo = rstate->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteState *rstate, BgpAttrPtr attrX,
            int start_idx, int end_idx, int count = 1) {
        RibOutAttr roattrX(attrX.get(), 0);
        VerifyHistory(rstate, roattrX, start_idx, end_idx, count);
    }

    void VerifyHistory(RouteState *rstate, std::vector<BgpAttrPtr> &attrvec,
            int label, int start_idx, int end_idx) {
        EXPECT_EQ(end_idx-start_idx+1, rstate->Advertised()->size());

        for (int idx = start_idx; idx <= end_idx; idx++) {
            RibOutAttr roattrX(attrvec[idx].get(), label);
            const AdvertiseInfo *ainfo = rstate->FindHistory(roattrX);
            EXPECT_TRUE(ainfo != NULL);
            RibPeerSet bitset;
            bitset.set(ribout_.GetPeerIndex(peers_[idx]));
            EXPECT_TRUE(ainfo->bitset == bitset);
        }
    }

    void VerifyHistory(RouteState *rstate, std::vector<BgpAttrPtr> &attrvec,
            int start_idx, int end_idx) {
        VerifyHistory(rstate, attrvec, 0, start_idx, end_idx);
    }

    void VerifyHistory(RouteUpdate *rt_update) {
        EXPECT_EQ(0, rt_update->History()->size());
    }

    void VerifyHistory(RouteUpdate *rt_update, BgpAttrPtr attrX,
            int start_idx, int end_idx, int count = 1) {
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        EXPECT_EQ(count, rt_update->History()->size());
        RibOutAttr roattrX(attrX.get(), 0);
        const AdvertiseInfo *ainfo = rt_update->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteUpdate *rt_update, std::vector<BgpAttrPtr> &attrvec,
            int start_idx, int end_idx) {
        EXPECT_EQ(end_idx-start_idx+1, rt_update->History()->size());

        for (int idx = start_idx; idx <= end_idx; idx++) {
            RibOutAttr roattrX(attrvec[idx].get(), 0);
            const AdvertiseInfo *ainfo = rt_update->FindHistory(roattrX);
            EXPECT_TRUE(ainfo != NULL);
            RibPeerSet bitset;
            bitset.set(ribout_.GetPeerIndex(peers_[idx]));
            EXPECT_TRUE(ainfo->bitset == bitset);
        }
    }

    void VerifyUpdateCount(int idx, Count count) {
        ASSERT_TRUE(idx < (int) peers_.size());
        EXPECT_EQ(count, peers_[idx]->update_count());
    }

    void VerifyUpdateCount(int start_idx, int end_idx, Count count) {
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            EXPECT_EQ(count, peers_[idx]->update_count());
        }
    }

    void VerifyEvenUpdateCount(int start_idx, int end_idx, Count count) {
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if (idx % 2 == 0)
                EXPECT_EQ(count, peers_[idx]->update_count());
        }
    }

    void VerifyOddUpdateCount(int start_idx, int end_idx, Count count) {
        ASSERT_TRUE(start_idx <= end_idx);
        ASSERT_TRUE(end_idx < (int) peers_.size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            if (idx % 2 == 1)
                EXPECT_EQ(count, peers_[idx]->update_count());
        }
    }

    void VerifyMessageCount(int count) {
        EXPECT_EQ(count, builder_.msg_count());
    }

    void ClearMessageCount() {
        builder_.clear_msg_count();
    }

    void DeleteRouteState(BgpRoute *route) {
        DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
        if (!dbstate) return;
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        EXPECT_TRUE(rstate != NULL);
        route->ClearState(&table_, ribout_.listener_id());
        delete rstate;
    }

    void DeleteRouteState(int start_idx, int end_idx) {
        for (int idx = start_idx; idx <= end_idx; idx++) {
            BgpRoute *route = routes_[idx];
            DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
            if (!dbstate) continue;
            RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
            EXPECT_TRUE(rstate != NULL);
            route->ClearState(&table_, ribout_.listener_id());
            delete rstate;
        }
    }

    void CheckInvariants() {
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                qid++) {
            UpdateQueue *queue = updates_->queue_vec_[qid];
            queue->CheckInvariants();
        }
    }

    void CheckTerminationInvariants() {
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                qid++) {
            UpdateQueue *queue = updates_->queue_vec_[qid];
            queue->CheckInvariants();
            ASSERT_EQ(1, queue->queue_.size());
        }
    }

    void DrainAndDeleteDBState() {
        BGP_DEBUG_UT("Cleaning up for next iteration");
        CheckInvariants();
        ClearAllPeerBlocks(0, (int) peers_.size()-1);
        SetPeerUnblockNow(0, (int) peers_.size()-1);
        UpdateAllPeers();
        UpdateRibOut();
        ClearPeerCount(0, (int) peers_.size()-1);
        ClearMessageCount();
        DeleteRouteState(0, (int) routes_.size()-1);
        DeleteRouteState(&dflt_rt_);
        CheckTerminationInvariants();
    }

    void UpdateRibOut(int qid = RibOutUpdates::QUPDATE) {
        ConcurrencyScope scope("bgp::SendTask");
        sg_->UpdateRibOut(&ribout_, qid);
    }

    void UpdatePeer(BgpTestPeer *peer) {
        ConcurrencyScope scope("bgp::SendTask");
        sg_->UpdatePeer(peer);
    }

    void UpdateAllPeers() {
        for (int idx = 0; idx < (int) peers_.size(); idx++) {
            UpdatePeer(peers_[idx]);
        }
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    InetTable table_;
    SchedulingGroupManager mgr_;
    RibOut ribout_;
    BgpExport *export_;
    RibOutUpdates *updates_;
    MsgBuilderMock builder_;
    SchedulingGroup *sg_;

    Ip4Prefix dflt_prefix_;
    InetRoute dflt_rt_;

    std::vector<BgpTestPeer *> peers_;
    std::vector<BgpRoute *> routes_;
    std::vector<BgpAttrPtr> attr_;
    std::vector<BgpAttrPtr> alt_attr_;
    BgpAttrPtr attrA_, attrB_, attrC_, attrZ_;
    BgpAttrPtr attr_null_;
    RibOutAttr roattrA_, roattrB_, roattrC_, roattrZ_;
    RibOutAttr roattr_null_;
};

#endif

