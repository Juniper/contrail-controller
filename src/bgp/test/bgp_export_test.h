/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_export_test_h
#define ctrlplane_bgp_export_test_h

#include "bgp/bgp_export.h"

#include <boost/ptr_container/ptr_vector.hpp>

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_message_builder.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_update.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/scheduling_group.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet/inet_route.h"
#include "db/db.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

static void SchedulerStop() {
    TaskScheduler::GetInstance()->Stop();
}

static void SchedulerStart() {
    TaskScheduler::GetInstance()->Start();
}

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
        return true;
    }

    int update_count() const { return count_; }

private:
    int index_;
    int count_;
};

class InetTableMock : public InetTable {
public:
    InetTableMock(DB *db, const std::string &name) : InetTable(db, name) { }
    virtual ~InetTableMock() { return; }

    virtual bool Export(RibOut *ribout, Route *route,
                        const RibPeerSet &peerset,
                        UpdateInfoSList &uinfo_slist) {
        assert(uinfo_slist->empty());
        executed_ = true;
        peerset_ = peerset;
        res_slist_.swap(uinfo_slist);
        return reach_;
    }

    void SetExportResult(bool reach) {
        assert(!reach);
        assert(res_slist_->empty());
        executed_ = false;
        reach_ = reach;
        peerset_.clear();
    }

    void SetExportResult(UpdateInfoSList &uinfo_slist) {
        assert(!uinfo_slist->empty());
        assert(res_slist_->empty());
        executed_ = false;
        reach_ = true;
        res_slist_.swap(uinfo_slist);
        peerset_.clear();
    }

    void VerifyExportResult(bool executed) {
        assert(res_slist_->empty());
        EXPECT_EQ(executed, executed_);
    }

    void VerifyExportPeerset(RibPeerSet &peerset) {
        assert(res_slist_->empty());
        EXPECT_EQ(peerset, peerset_);
    }

private:
    bool executed_;
    bool reach_;
    RibPeerSet peerset_;
    UpdateInfoSList res_slist_;
};

class MessageMock : public Message {
public:
    virtual bool AddRoute(const BgpRoute *route, const RibOutAttr *attr) {
        return true;
    }
    virtual void Finish() {
    }
    virtual const uint8_t *GetData(IPeerUpdate *peer, size_t *lenp) {
        return NULL;
    }
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

static const int kPeerCount = 6;

class BgpExportTest : public ::testing::Test {
protected:

    typedef std::vector<BgpAttrPtr> BgpAttrVec;
    typedef std::vector<const RibPeerSet *> RibPeerSetVec;

    BgpExportTest()
        : server_(&evm_),
        table_(&db_, "inet.0"),
        ribout_(&table_, &mgr_, RibExportPolicy()),
        prefix_(Ip4Prefix::FromString("0/0")),
        rt_(prefix_) {
        table_.Init();
        tpart_ = table_.GetTablePartition(0);
        export_ = ribout_.bgp_export();
        updates_ = ribout_.updates();
        updates_->SetMessageBuilder(&builder_);
        rt_.set_onlist();
    }

    virtual void SetUp() {
        gbl_index = 0;
        for (int idx = 0; idx < kPeerCount; idx++) {
            CreatePeer();
            peerset_[idx].set(ribout_.GetPeerIndex(&peers_[idx]));
        }

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

        for (int i = 0; i < kPeerCount; i++) {
            BgpAttr *attribute = new BgpAttr(server_.attr_db());
            attribute->set_med(1000 + i);
            attr_[i] = server_.attr_db()->Locate(attribute);
        }

        for (int i = 0; i < kPeerCount; i++) {
            BgpAttr *attribute = new BgpAttr(server_.attr_db());
            attribute->set_med(2000 + i);
            alt_attr_[i] = server_.attr_db()->Locate(attribute);
        }
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    void RunExport() {
        ConcurrencyScope scope("db::DBTable");
        export_->Export(tpart_, &rt_);
    }

    void RunJoin(RibPeerSet &join_peerset) {
        ConcurrencyScope scope("db::DBTable");
        export_->Join(tpart_, join_peerset, &rt_);
    }

    void RunLeave(RibPeerSet &leave_peerset) {
        ConcurrencyScope scope("db::DBTable");
        export_->Leave(tpart_, leave_peerset, &rt_);
    }

    void RibOutRegister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        ribout->Register(peer);
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueJoin(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueJoin(RibOutUpdates::QBULK, bit);
    }

    void RibOutUnregister(RibOut *ribout, IPeerUpdate *peer) {
        ConcurrencyScope scope("bgp::PeerMembership");
        int bit = ribout->GetPeerIndex(peer);
        ribout->updates()->QueueLeave(RibOutUpdates::QUPDATE, bit);
        ribout->updates()->QueueLeave(RibOutUpdates::QBULK, bit);
        ribout->Unregister(peer);
    }

    void CreatePeer() {
        BgpTestPeer *peer = new BgpTestPeer();
        peers_.push_back(peer);
        RibOutRegister(&ribout_, peer);
        ASSERT_TRUE(ribout_.GetSchedulingGroup() != NULL);
    }

    void DeactivatePeers(int start_idx, int end_idx) {
        ConcurrencyScope scope("bgp::PeerMembership");
        for (int idx = start_idx; idx <= end_idx; idx++) {
            ribout_.Deactivate(&peers_[idx]);
        }
    }

    void ReactivatePeers(int start_idx, int end_idx) {
        for (int idx = start_idx; idx <= end_idx; idx++) {
            peerset_[idx].reset(ribout_.GetPeerIndex(&peers_[idx]));
            RibOutUnregister(&ribout_, &peers_[idx]);
            RibOutRegister(&ribout_, &peers_[idx]);
            peerset_[idx].set(ribout_.GetPeerIndex(&peers_[idx]));
            ASSERT_TRUE(ribout_.GetSchedulingGroup() != NULL);
        }
    }

    void BuildPeerSet(RibPeerSet &build_peerset, int start_idx, int end_idx) {
        build_peerset.clear();
        for (int idx = start_idx; idx <= end_idx; idx++) {
            build_peerset.set(ribout_.GetPeerIndex(&peers_[idx]));
        }
    }

    void BuildVectors(BgpAttrVec &attr_vec, RibPeerSetVec &peerset_vec,
            BgpAttrPtr attrX, RibPeerSet *peerset) {
        attr_vec.push_back(attrX);
        peerset_vec.push_back(peerset);
    }

    void BuildVectors(BgpAttrVec &attr_vec, RibPeerSetVec &peerset_vec,
            BgpAttrPtr attr_blk[], int start_idx, int end_idx) {
        for (int idx = start_idx; idx <= end_idx; idx++) {
            attr_vec.push_back(attr_blk[idx]);
            peerset_vec.push_back(&peerset_[idx]);
        }
    }

    void BuildUpdateInfo(BgpAttrVec *attr_vec, RibPeerSetVec *peerset_vec,
            UpdateInfoSList &uu_slist) {
        ASSERT_EQ(attr_vec->size(), peerset_vec->size());
        for (size_t idx = 0; idx < attr_vec->size(); idx++) {
            UpdateInfo *uinfo = new UpdateInfo;
            if (attr_vec->at(idx))
                uinfo->roattr.set_attr(attr_vec->at(idx).get());
            uinfo->target = *peerset_vec->at(idx);
            uu_slist->push_front(*uinfo);
        }
    }

    void InitUpdateInfoCommon(BgpAttrPtr attrX,
            int start_idx, int end_idx, UpdateInfoSList &uinfo_slist) {
        RibPeerSet uu_peerset;
        BgpAttrVec uu_attr_vec;
        RibPeerSetVec uu_peerset_vec;
        BuildPeerSet(uu_peerset, start_idx, end_idx);
        BuildVectors(uu_attr_vec, uu_peerset_vec, attrX, &uu_peerset);
        BuildUpdateInfo(&uu_attr_vec, &uu_peerset_vec, uinfo_slist);
    }

    void InitUpdateInfoCommon(BgpAttrPtr attr_blk[],
            int start_idx, int end_idx, UpdateInfoSList &uinfo_slist) {
        BgpAttrVec uu_attr_vec;
        RibPeerSetVec uu_peerset_vec;
        BuildVectors(uu_attr_vec, uu_peerset_vec, attr_blk, start_idx, end_idx);
        BuildUpdateInfo(&uu_attr_vec, &uu_peerset_vec, uinfo_slist);
    }

    void BuildAdvertiseInfo(BgpAttrVec *attr_vec, RibPeerSetVec *peerset_vec,
            AdvertiseSList &adv_slist) {
        ASSERT_EQ(attr_vec->size(), peerset_vec->size());
        for (size_t idx = 0; idx < attr_vec->size(); idx++) {
            AdvertiseInfo *ainfo = new AdvertiseInfo;
            assert(attr_vec->at(idx));
            ainfo->roattr.set_attr(attr_vec->at(idx).get());
            ainfo->bitset = *peerset_vec->at(idx);
            adv_slist->push_front(*ainfo);
        }
    }

    void InitAdvertiseInfoCommon(BgpAttrPtr attrX,
            int start_idx, int end_idx, AdvertiseSList &adv_slist) {
        RibPeerSet adv_peerset;
        BgpAttrVec adv_attr_vec;
        RibPeerSetVec adv_peerset_vec;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        BuildVectors(adv_attr_vec, adv_peerset_vec, attrX, &adv_peerset);
        BuildAdvertiseInfo(&adv_attr_vec, &adv_peerset_vec, adv_slist);
    }

    void InitAdvertiseInfoCommon(BgpAttrPtr attr_blk[],
            int start_idx, int end_idx, AdvertiseSList &adv_slist) {
        BgpAttrVec adv_attr_vec;
        RibPeerSetVec adv_peerset_vec;
        BuildVectors(adv_attr_vec, adv_peerset_vec, attr_blk,
                start_idx, end_idx);
        BuildAdvertiseInfo(&adv_attr_vec, &adv_peerset_vec, adv_slist);
    }

    RouteState *BuildRouteState(BgpRoute *route, AdvertiseSList &adv_slist) {
        RouteState *rstate = new RouteState;
        rstate->SetHistory(adv_slist);
        route->SetState(&table_, ribout_.listener_id(), rstate);
        return rstate;
    }

    RouteUpdate *BuildRouteUpdate(BgpRoute *route, int qid,
            UpdateInfoSList &uu_slist) {
        ConcurrencyScope scope("db::DBTable");
        RouteUpdate *rt_update = new RouteUpdate(route, qid);
        rt_update->SetUpdateInfo(uu_slist);
        updates_->Enqueue(route, rt_update);
        return rt_update;
    }

    RouteUpdate *BuildRouteUpdate(BgpRoute *route, int qid,
            UpdateInfoSList &uu_slist, AdvertiseSList &adv_slist) {
        ConcurrencyScope scope("db::DBTable");
        RouteUpdate *rt_update = new RouteUpdate(route, qid);
        rt_update->SetUpdateInfo(uu_slist);
        rt_update->SetHistory(adv_slist);
        updates_->Enqueue(route, rt_update);
        return rt_update;
    }

    UpdateList *BuildUpdateList(BgpRoute *route, RouteUpdate *rt_update[],
            AdvertiseSList &adv_slist) {
        ConcurrencyScope scope("db::DBTable");
        UpdateList *uplist = new UpdateList;
        uplist->SetHistory(adv_slist);
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
             qid++) {
            if (rt_update[qid])
                uplist->AddUpdate(rt_update[qid]);
        }
        route->SetState(ribout_.table(), ribout_.listener_id(), uplist);
        return uplist;
    }

    void BuildExportResult(BgpAttrPtr attrX, int start_idx, int end_idx) {
        UpdateInfoSList res_slist;
        RibPeerSet res_peerset;
        BuildPeerSet(res_peerset, start_idx, end_idx);
        BgpAttrVec res_attr_vec;
        RibPeerSetVec res_peerset_vec;
        BuildVectors(res_attr_vec, res_peerset_vec, attrX, &res_peerset);
        BuildUpdateInfo(&res_attr_vec, &res_peerset_vec, res_slist);
        table_.SetExportResult(res_slist);
    }

    void BuildExportResult(BgpAttrPtr attr_blk[], int start_idx, int end_idx) {
        UpdateInfoSList res_slist;
        BgpAttrVec res_attr_vec;
        RibPeerSetVec res_peerset_vec;
        BuildVectors(res_attr_vec, res_peerset_vec, attr_blk,
                start_idx, end_idx);
        BuildUpdateInfo(&res_attr_vec, &res_peerset_vec, res_slist);
        table_.SetExportResult(res_slist);
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
        const UpdateQueue *queue = updates_->queue(qid);
        EXPECT_EQ(queue->attr_set_.size(), rt_update->Updates()->size());
        return rt_update;
    }

    UpdateList *ExpectUpdateList(BgpRoute *route, RouteUpdate *rt_update[],
            int size = 2) {
        DBState *dbstate = route->GetState(&table_, ribout_.listener_id());
        UpdateList *uplist = dynamic_cast<UpdateList *>(dbstate);
        EXPECT_TRUE(uplist != NULL);
        EXPECT_EQ(size, uplist->GetList()->size());
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
             qid++) {
            rt_update[qid] = uplist->FindUpdate(qid);
        }
        return uplist;
    }

    void VerifyUpdates(RouteUpdate *rt_update,
            const RibOutAttr &roattrX, const RibPeerSet &uinfo_peerset,
            int count = 1) {
        EXPECT_TRUE(rt_update != NULL);
        EXPECT_EQ(count, rt_update->Updates()->size());
        const UpdateInfo *uinfo = rt_update->FindUpdateInfo(roattrX);
        EXPECT_TRUE(uinfo != NULL);
        EXPECT_TRUE(uinfo->target == uinfo_peerset);
    }

    void VerifyUpdates(RouteUpdate *rt_update, const RibOutAttr &roattrX,
            int start_idx, int end_idx, int count = 1) {
        RibPeerSet uinfo_peerset;
        BuildPeerSet(uinfo_peerset, start_idx, end_idx);
        EXPECT_TRUE(rt_update != NULL);
        EXPECT_EQ(count, rt_update->Updates()->size());
        const UpdateInfo *uinfo = rt_update->FindUpdateInfo(roattrX);
        EXPECT_TRUE(uinfo != NULL);
        EXPECT_TRUE(uinfo->target == uinfo_peerset);
    }

    void VerifyUpdates(RouteUpdate *rt_update, BgpAttrPtr attr_blk[],
            int start_idx, int end_idx, int count = 0) {
        if (!count) count = end_idx - start_idx + 1;
        EXPECT_TRUE(rt_update != NULL);
        EXPECT_EQ(count, rt_update->Updates()->size());
        for (int idx = start_idx; idx <= end_idx; idx++) {
            RibOutAttr roattrX(attr_blk[idx].get(), 0);
            const UpdateInfo *uinfo = rt_update->FindUpdateInfo(roattrX);
            EXPECT_TRUE(uinfo != NULL);
            EXPECT_TRUE(uinfo->target == peerset_[idx]);
        }
    }

    void VerifyHistory(RouteState *rstate) {
        EXPECT_EQ(0, rstate->Advertised()->size());
    }

    void VerifyHistory(RouteState *rstate,
            const RibOutAttr &roattrX, const RibPeerSet &adv_peerset) {
        EXPECT_EQ(1, rstate->Advertised()->size());
        const AdvertiseInfo *ainfo = rstate->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteState *rstate,
            const RibOutAttr &roattrX, int start_idx, int end_idx) {
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        EXPECT_EQ(1, rstate->Advertised()->size());
        const AdvertiseInfo *ainfo = rstate->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteState *rstate, BgpAttrPtr attr_blk[],
            int start_idx, int end_idx) {
        EXPECT_EQ(end_idx-start_idx+1, rstate->Advertised()->size());

        for (int idx = start_idx; idx < end_idx; idx++) {
            RibOutAttr roattrX(attr_blk[idx].get(), 0);
            const AdvertiseInfo *ainfo = rstate->FindHistory(roattrX);
            EXPECT_TRUE(ainfo != NULL);
            EXPECT_TRUE(ainfo->bitset == peerset_[idx]);
        }
    }

    void VerifyHistory(RouteUpdate *rt_update) {
        EXPECT_EQ(0, rt_update->History()->size());
    }

    void VerifyHistory(RouteUpdate *rt_update,
            const RibOutAttr &roattrX, const RibPeerSet &adv_peerset) {
        EXPECT_EQ(1, rt_update->History()->size());
        const AdvertiseInfo *ainfo = rt_update->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteUpdate *rt_update,
            const RibOutAttr &roattrX, int start_idx, int end_idx) {
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        EXPECT_EQ(1, rt_update->History()->size());
        const AdvertiseInfo *ainfo = rt_update->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(RouteUpdate *rt_update, BgpAttrPtr attr_blk[],
            int start_idx, int end_idx) {
        EXPECT_EQ(end_idx-start_idx+1, rt_update->History()->size());

        for (int idx = start_idx; idx < end_idx; idx++) {
            RibOutAttr roattrX(attr_blk[idx].get(), 0);
            const AdvertiseInfo *ainfo = rt_update->FindHistory(roattrX);
            EXPECT_TRUE(ainfo != NULL);
            EXPECT_TRUE(ainfo->bitset == peerset_[idx]);
        }
    }

    void VerifyHistory(UpdateList *uplist) {
        EXPECT_EQ(0, uplist->History()->size());
    }

    void VerifyHistory(UpdateList *uplist,
            const RibOutAttr &roattrX, const RibPeerSet &adv_peerset) {
        EXPECT_EQ(1, uplist->History()->size());
        const AdvertiseInfo *ainfo = uplist->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyHistory(UpdateList *uplist,
            const RibOutAttr &roattrX, int start_idx, int end_idx) {
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, start_idx, end_idx);
        EXPECT_EQ(1, uplist->History()->size());
        const AdvertiseInfo *ainfo = uplist->FindHistory(roattrX);
        EXPECT_TRUE(ainfo != NULL);
        EXPECT_TRUE(ainfo->bitset == adv_peerset);
    }

    void VerifyAdvertiseCount(int count) {
        EXPECT_EQ(count, ribout_.RouteAdvertiseCount(&rt_));
    }

    void DrainAndDeleteRouteState(BgpRoute *route, int advertise_count = 0) {
        SchedulerStart();
        task_util::WaitForIdle();
        RouteState *rstate = ExpectRouteState(route);
        if (advertise_count)
            VerifyAdvertiseCount(advertise_count);
        route->ClearState(&table_, ribout_.listener_id());
        delete rstate;
    }

    void DrainAndVerifyNoState(BgpRoute *route) {
        SchedulerStart();
        task_util::WaitForIdle();
        ExpectNullDBState(route);
        VerifyAdvertiseCount(0);
    }

    EventManager evm_;
    BgpServer server_;
    DB db_;
    DBTablePartBase *tpart_;
    InetTableMock table_;
    SchedulingGroupManager mgr_;
    RibOut ribout_;
    BgpExport *export_;
    RibOutUpdates *updates_;
    MsgBuilderMock builder_;

    Ip4Prefix prefix_;
    InetRoute rt_;

    boost::ptr_vector<BgpTestPeer> peers_;
    BgpAttrPtr attrA_, attrB_, attrC_;
    BgpAttrPtr attr_null_;
    BgpAttrPtr attr_[kPeerCount];
    BgpAttrPtr alt_attr_[kPeerCount];
    RibOutAttr roattrA_, roattrB_, roattrC_;
    RibOutAttr roattr_null_;
    RibPeerSet peerset_[kPeerCount];
};

#endif

