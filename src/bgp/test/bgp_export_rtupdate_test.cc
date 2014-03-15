/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_export_test.h"

#include "base/logging.h"

using namespace std;


//
// Common class used for RouteUpdate tests.
//
class BgpExportRouteUpdateCommonTest : public BgpExportTest {
protected:

    BgpExportRouteUpdateCommonTest() : rt_update_(NULL), count_(0) {
    }

    void InitAdvertiseInfo(BgpAttrPtr attrX, int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attrX, start_idx, end_idx, adv_slist_);
        count_ = end_idx - start_idx + 1;
    }

    void InitAdvertiseInfo(BgpAttrPtr attr_blk[], int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attr_blk, start_idx, end_idx, adv_slist_);
        count_ = end_idx - start_idx + 1;
    }

    void InitUpdateInfo(BgpAttrPtr attrX, int start_idx, int end_idx,
            int qid = RibOutUpdates::QUPDATE) {
        qid_ = qid;
        InitUpdateInfoCommon(attrX, start_idx, end_idx, uinfo_slist_);
    }

    void InitUpdateInfo(BgpAttrPtr attr_blk[], int start_idx, int end_idx,
            int qid = RibOutUpdates::QUPDATE) {
        qid_ = qid;
        InitUpdateInfoCommon(attr_blk, start_idx, end_idx, uinfo_slist_);
    }

    void VerifyRouteUpdateDequeueEnqueue(RouteUpdate *rt_update) {
        EXPECT_EQ(rt_update_, rt_update);
        EXPECT_LT(tstamp_, rt_update->tstamp());
    }

    void VerifyRouteUpdateNoDequeue(RouteUpdate *rt_update) {
        EXPECT_EQ(rt_update_, rt_update);
        EXPECT_EQ(tstamp_, rt_update->tstamp());
    }

    int qid_;
    AdvertiseSList adv_slist_;
    UpdateInfoSList uinfo_slist_;
    RouteUpdate *rt_update_;
    uint64_t tstamp_;
    int count_;
};

//
// Used for a RouteUpdate without AdvertiseInfo.
//
class BgpExportRouteUpdateTest1 : public BgpExportRouteUpdateCommonTest {
protected:
    void Initialize() {
        ASSERT_EQ(0, count_);
        ASSERT_TRUE(adv_slist_->empty());
        ASSERT_TRUE(!uinfo_slist_->empty());
        SchedulerStop();
        table_.SetExportResult(false);
        rt_update_ = BuildRouteUpdate(&rt_, qid_, uinfo_slist_);
        tstamp_ = rt_update_->tstamp();
        VerifyAdvertiseCount(0);
        usleep(50);
    }
};

TEST_F(BgpExportRouteUpdateTest1, Basic1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1, qid);
            Initialize();
            DrainAndDeleteRouteState(&rt_);
        }
    }
}

TEST_F(BgpExportRouteUpdateTest1, Basic2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attr_, 0, kPeerCount-1, qid);
            Initialize();
            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle change in attribute for all peers with scheduled state
//              or pending join state.  All scheduled/pending updates share a
//              common attribute before and after the change.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr B.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest1, Advertise1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1, qid);
            Initialize();

            BuildExportResult(attrB_, 0, vSchedPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrB_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle change in attribute for all peers with scheduled state
//              or pending join state. All scheduled and pending updates have
//              different attributes before and after the change.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], alt_attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], alt_attr x.
//
TEST_F(BgpExportRouteUpdateTest1, Advertise2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attr_, 0, vSchedPeerCount-1, qid);
            Initialize();

            BuildExportResult(alt_attr_, 0, vSchedPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, alt_attr_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. Should detect duplicate scheduled state and not
//              dequeue the RouteUpdate.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, Duplicate1) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. Should detect duplicate scheduled state and not
//              dequeue the RouteUpdate.
//              Different attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest1, Duplicate2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not need to receive the route per the new export results.
//              We should not treat this as a duplicate.  Need to cancel the
//              update to the peer.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-2], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr A.
//
TEST_F(BgpExportRouteUpdateTest1, NotDuplicate1) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-2);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-2);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not need to receive the route per the new export results.
//              We should not treat this as a duplicate.  Need to cancel the
//              update to the peer.
//              Different attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-2], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr x.
//
TEST_F(BgpExportRouteUpdateTest1, NotDuplicate2) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-2);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-2);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not have scheduled state but needs to receive the route
//              per the new export results.
//              We should not treat this as a duplicate. Need to schedule the
//              update to the peer.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest1, NotDuplicate3) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-2);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not have scheduled state but needs to receive the route
//              per the new export results.
//              We should not treat this as a duplicate. Need to schedule the
//              update to the peer.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest1, NotDuplicate4) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitUpdateInfo(attr_, 0, vSchedPeerCount-2);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently scheduled/pending for peerset is now
//              rejected for all peers by export policy.  Should get rid of
//              the scheduled/pending updates and clean up the DBState.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, Reject1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1, qid);
            Initialize();

            RunExport();
            table_.VerifyExportResult(true);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Route that is currently scheduled/pending for peerset is now
//              rejected for all peers by export policy.  Should get rid of
//              the scheduled/pending updates and clean up the DBState.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Not executed, Route is deleted.
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, Reject2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1, qid);
            Initialize();

            rt_.MarkDelete();
            RunExport();
            rt_.ClearDelete();
            table_.VerifyExportResult(false);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Join processing for route that is currently scheduled to set
//              of peers. The join peerset is a subset of the peers for which
//              we have currently scheduled state. Should be a noop since all
//              join peers already have scheduled state. Should not even run
//              export policy in this case.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinNoop1) {
    int qid = RibOutUpdates::QUPDATE;
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attrA_, 0, kPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently scheduled to set
//              of peers. The join peerset is a subset of the peers for which
//              we have currently scheduled state. Should be a noop since all
//              join peers already have scheduled state. Should not even run
//              export policy in this case.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinNoop2) {
    int qid = RibOutUpdates::QUPDATE;
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attr_, 0, kPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, attr_, 0, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently scheduled to set
//              of peers. The join peerset is a subset of the peers for which
//              we have currently scheduled state. Should be a noop since the
//              route is deleted.
//              Should not even run export policy in this case.
//              Different attribute for all current peers.
//              Note that current updates will be cancelled from Export.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinNoop3) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
                vJoinPeerCount++) {
            InitUpdateInfo(attrA_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet join_peerset;
            BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
            rt_.MarkDelete();
            RunJoin(join_peerset);
            rt_.ClearDelete();
            table_.VerifyExportResult(false);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrA_, 0, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Join processing for route that is currently scheduled to set
//              of peers. The join peerset is a subset of the peers for which
//              we have currently scheduled state. Should be a noop since the
//              route is deleted.
//              Should not even run export policy in this case.
//              Same attribute for all current peers.
//              Note that current updates will be cancelled from Export.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinNoop4) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
                vJoinPeerCount++) {
            InitUpdateInfo(attr_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet join_peerset;
            BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
            rt_.MarkDelete();
            RunJoin(join_peerset);
            rt_.ClearDelete();
            table_.VerifyExportResult(false);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, attr_, 0, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Join processing for route that already has join pending updates
//              for some peers.  Export policy accepts the route for the join
//              peers.
//              Same attribute for all pending peers and all join peers.
//
// Old DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr A.
// New DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest1, JoinMerge1) {
    int qid = RibOutUpdates::QBULK;
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attrA_, vJoinPeerCount, kPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attrA_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that already has join pending updates
//              for some peers.  Export policy accepts the route for the join
//              peers.
//              Different attribute for all pending peers and all join peers.
//
// Old DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: RouteUpdate in QBULK.
//              UpdateInfo peer x=[0,kPeerCount-1], attr x.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinMerge2) {
    int qid = RibOutUpdates::QBULK;
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attr_, vJoinPeerCount, kPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, attr_, 0, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that already has join pending updates
//              for all peers. Export policy accepts the route for all the join
//              peers, but with a new attribute.
//              Common attribute for all pending peers and different but common
//              attribute for all join peers.
//
// Old DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr B.
// New DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest1, JoinMerge3) {
    int qid = RibOutUpdates::QBULK;
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attrA_, 0, vJoinPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attrB_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, roattrB_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that already has join pending updates
//              for all peers. Export policy accepts the route for all the join
//              peers, but with a new attribute.
//              Different attribute for all pending peers and all join peers.
//
// Old DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], alt_attr x.
// New DBState: RouteUpdate in QBULK.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], alt_attr x.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, JoinMerge4) {
    int qid = RibOutUpdates::QBULK;
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitUpdateInfo(attr_, 0, vJoinPeerCount-1, qid);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(alt_attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, alt_attr_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset does not overlap with the peers that
//              have scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr A.
// Leave Peers: Peers x=[vCurrPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveNoop1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
                vCurrPeerCount++) {
            InitUpdateInfo(attrA_, 0, vCurrPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, vCurrPeerCount, kPeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrA_, 0, vCurrPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset does not overlap with the peers that
//              have scheduled/pending updates.
//              Different attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr A.
// Leave Peers: Peers x=[vCurrPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveNoop2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
                vCurrPeerCount++) {
            InitUpdateInfo(attr_, 0, vCurrPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, vCurrPeerCount, kPeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, attr_, 0, vCurrPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending.The leave peerset is a proper subset of the peers with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveClear1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attrA_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrA_, vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending.The leave peerset is a proper subset of the peers with
//              scheduled/pending updates.
//              Different attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveClear2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attr_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, attr_,
                    vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset is the same as the peerset with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveClear3) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount <= kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attrA_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset is the same as the peerset with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveClear4) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount <= kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attr_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending.The leave peerset is a proper subset of the peers with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//              Route is marked deleted.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveDeleted1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attrA_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrA_, vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending.The leave peerset is a proper subset of the peers with
//              scheduled/pending updates.
//              Different attribute for all scheduled/pending peers.
//              Route is marked deleted.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,kPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//              No AdvertiseInfo.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveDeleted2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attr_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, attr_,
                    vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset is the same as the peerset with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//              Route is marked deleted.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveDeleted3) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount <= kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attrA_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            rt_.MarkDelete();
            RunLeave(leave_peerset);
            rt_.ClearDelete();

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that has scheduled updates or join
//              pending. The leave peerset is the same as the peerset with
//              scheduled/pending updates.
//              Same attribute for all scheduled/pending peers.
//              Route is marked deleted.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteUpdateTest1, LeaveDeleted4) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount <= kPeerCount;
                vLeavePeerCount++) {
            InitUpdateInfo(attr_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            rt_.MarkDelete();
            RunLeave(leave_peerset);
            rt_.ClearDelete();

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Used for a RouteUpdate with AdvertiseInfo.
//
class BgpExportRouteUpdateTest2 : public BgpExportRouteUpdateCommonTest {
protected:
    void Initialize() {
        ASSERT_NE(0, count_);
        ASSERT_TRUE(!adv_slist_->empty());
        ASSERT_TRUE(!uinfo_slist_->empty());
        SchedulerStop();
        table_.SetExportResult(false);
        rt_update_ = BuildRouteUpdate(&rt_, qid_, uinfo_slist_, adv_slist_);
        tstamp_ = rt_update_->tstamp();
        VerifyAdvertiseCount(count_);
        usleep(50);
    }
};

TEST_F(BgpExportRouteUpdateTest2, Basic1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1);
            Initialize();
            DrainAndDeleteRouteState(&rt_);
        }
    }
}

TEST_F(BgpExportRouteUpdateTest2, Basic2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
            InitUpdateInfo(alt_attr_, 0, vSchedPeerCount-1);
            Initialize();
            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle change in attribute back to the original value when we
//              have current advertised state with the original attribute for
//              a subset of the peers and scheduled updates with a different
//              attribute for all the peers. Should schedule updates with the
//              original attribute for peers for which we do nto have current
//              state.
//              Common original attribute for all peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[vCurrPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Advertise1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        InitUpdateInfo(attrB_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle change in attribute back to the original value when we
//              have current advertised state with the original attribute for
//              a subset of the peers and scheduled updates with a different
//              attribute for all the peers. Should schedule updates with the
//              original attribute for peers for which we do nto have current
//              state.
//              DIfferent original attribute for all peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[vCurrPeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Advertise2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        InitUpdateInfo(attrB_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Cancel a scheduled withdraw for all peers and advertise a new
//              attribute to all of them.
//              Common new attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr B.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest2, Advertise3) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        InitUpdateInfo(attr_null_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrB_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Cancel a scheduled withdraw for all peers and advertise a new
//              attribute to all of them.
//              Different new attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr NULL.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Advertise4) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        InitUpdateInfo(attr_null_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised and scheduled/pending to a
//              set of peers has to be withdrawn from all peers due to a policy
//              change. The new policy rejects the route for all peers.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, qid);
            Initialize();

            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Route that is currently advertised and scheduled/pending to a
//              set of peers has to be withdrawn from all peers due to a policy
//              change. The new policy rejects the route for all peers.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, qid);
            Initialize();

            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update, attr_, 0, vSchedPeerCount-1);

            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from the current peers due to policy change. At
//              the same time, scheduled updates to other peers needs to be
//              maintained.
//              Same attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw3) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_, vSchedPeerCount, kPeerCount-1);
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1, qid);
            Initialize();

            BuildExportResult(attrA_, 0, vSchedPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1, 2);
            VerifyUpdates(rt_update, roattr_null_,
                    vSchedPeerCount, kPeerCount-1, 2);
            VerifyHistory(rt_update, roattrA_, vSchedPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from the current peers due to policy change. At
//              the same time, scheduled updates to other peers needs to be
//              maintained.
//              Different attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw4) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attr_, vSchedPeerCount, kPeerCount-1);
            InitUpdateInfo(attr_, 0, vSchedPeerCount-1, qid);
            Initialize();

            BuildExportResult(attr_, 0, vSchedPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, attr_,
                    0, vSchedPeerCount-1, vSchedPeerCount+1);
            VerifyUpdates(rt_update, roattr_null_,
                    vSchedPeerCount, kPeerCount-1, vSchedPeerCount+1);
            VerifyHistory(rt_update, attr_, vSchedPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Process a route that's already scheduled to be withdrawn from
//              all peers.  Export policy rejects the route for all peers so
//              it should still be withdrawn.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw5) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-1);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Process a route that's already scheduled to be withdrawn from
//              all peers.  Export policy rejects the route for all peers so
//              it should still be withdrawn.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, Withdraw6) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-1);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that's currently advertised and has scheduled updates for
//              a set of peers has to be withdrawn from all peers when it gets
//              deleted.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, WithdrawDeleted1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, qid);
            Initialize();

            rt_.MarkDelete();
            RunExport();
            rt_.ClearDelete();
            table_.VerifyExportResult(false);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Route that's currently advertised and has scheduled updates for
//              a set of peers has to be withdrawn from all peers when it gets
//              deleted.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, WithdrawDeleted2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, qid);
            Initialize();

            rt_.MarkDelete();
            RunExport();
            rt_.ClearDelete();
            table_.VerifyExportResult(false);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            VerifyRouteUpdateDequeueEnqueue(rt_update);
            VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
            VerifyHistory(rt_update, attr_, 0, vSchedPeerCount-1);

            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Handle delete for route that's scheduled to be withdrawn from
//              all peers.  Make sure that we don't drop the negative update.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, WithdrawDeleted3) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-1);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Handle delete for route that's scheduled to be withdrawn from
//              all peers.  Make sure that we don't drop` the negative update.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, WithdrawDeleted4) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-1);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be re-advertised with a different attribute to some existing
//              peers and withdrawn from other existing peers. Triggered by
//              policy change or route change.
//              Same attribute for all peers for which route is accepted.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Export Rslt: Reject peer x=[0,vRejectPeerCount-1].
//              Accept peer x=[vRejectPeerCount,kPeerCount-1], attr C.
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vRejectPeerCount-1], attr NULL.
//              UpdateInfo peer x=[vRejectPeerCount,kPeerCount-1], attr C.
//
TEST_F(BgpExportRouteUpdateTest2, AdvertiseAndWithdraw1) {
    for (int vRejectPeerCount = 1; vRejectPeerCount < kPeerCount;
            vRejectPeerCount++) {
        EXPECT_TRUE(vRejectPeerCount > 0 && vRejectPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attrC_, vRejectPeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vRejectPeerCount-1, 2);
        VerifyUpdates(rt_update, roattrC_, vRejectPeerCount, kPeerCount-1, 2);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be re-advertised with a different attribute to some existing
//              peers, withdrawn from some existing peers and doesn't need to
//              to be re-advertised or withdrawn from other existing peers.
//              Triggered by policy change or route change.
//              Each set of peers share the same attribute.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Export Rslt: Reject peer x=[0,vRejectPeerCount-1].
//              Accept peer x=[vRejectPeerCount,
//                             vRejectPeerCount+vChangePeerCount-1], attr C.
//              Accept peer x=[vRejectPeerCount+vChangePeerCount-1,
//                             kPeerCount-1] attr B.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vRejectPeerCount-1], attr NULL.
//              UpdateInfo peer x=[vRejectPeerCount,
//                                 vRejectPeerCount+vChangePeerCount-1], attr C.
//              UpdateInfo peer x=[vRejectPeerCount+vChangePeerCount-1,
//                                 kPeerCount-1] attr B.
//
TEST_F(BgpExportRouteUpdateTest2, AdvertiseAndWithdraw2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        int vRejectPeerCount = 2;
        int vChangePeerCount = 2;
        EXPECT_TRUE(vRejectPeerCount > 0 && vRejectPeerCount < kPeerCount);
        EXPECT_TRUE(vChangePeerCount > 0 && vChangePeerCount < kPeerCount);
        EXPECT_TRUE(vRejectPeerCount + vChangePeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, kPeerCount-1, qid);
        Initialize();

        BgpAttrVec res_attr_vec;
        RibPeerSetVec res_peerset_vec;
        RibPeerSet change_peerset;
        BuildPeerSet(change_peerset, vRejectPeerCount,
                vRejectPeerCount+vChangePeerCount-1);
        BuildVectors(res_attr_vec, res_peerset_vec, attrC_, &change_peerset);
        RibPeerSet nochange_peerset;
        BuildPeerSet(nochange_peerset, vRejectPeerCount+vChangePeerCount,
                kPeerCount-1);
        BuildVectors(res_attr_vec, res_peerset_vec, attrB_, &nochange_peerset);

        UpdateInfoSList res_slist;
        BuildUpdateInfo(&res_attr_vec, &res_peerset_vec, res_slist);
        table_.SetExportResult(res_slist);

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        RibPeerSet reject_peerset;
        BuildPeerSet(reject_peerset, 0, vRejectPeerCount-1);
        VerifyUpdates(rt_update, roattr_null_, reject_peerset, 3);
        VerifyUpdates(rt_update, roattrC_, change_peerset, 3);
        VerifyUpdates(rt_update, roattrB_, nochange_peerset, 3);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers.  All peers also have current scheduled state.
//              Should detect duplicate scheduled state and not dequeue the
//              RouteUpdate.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate1) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attrB_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattrB_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers.  All peers also have current scheduled state.
//              Should detect duplicate scheduled state and not dequeue the
//              RouteUpdate.
//              Different attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. There's one peer that does not have any current
//              advertised state.  Should detect duplicate scheduled state and
//              not dequeue the RouteUpdate.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate3) {
    for (int vSchedPeerCount = 2; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attrB_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattrB_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. There's one peer that does not have any current
//              advertised state.  Should detect duplicate scheduled state and
//              not dequeue the RouteUpdate.
//              Different attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate4) {
    for (int vSchedPeerCount = 2; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. Some of the peers still have scheduled updates
//              while others have current advertised state. Should detect a
//              duplicate and not dequeue the RouteUpdate.
//              Same attribute for all scheduled and current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate5) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all peers. Some of the peers still have scheduled updates
//              while others have current advertised state. Should detect a
//              duplicate and not dequeue the RouteUpdate.
//              Different attribute for all scheduled and current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Duplicate6) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, attr_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all scheduled peers, but the current advertised state has
//              other peers that are not covered by the scheduled peerset. We
//              should not treat this as a duplicate update.  Instead we need
//              to withdraw the route from some current advertised peers.
//              Same attribute for all current advertised peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate1) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattr_null_,
                vSchedPeerCount, kPeerCount-1, 2);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1, 2);
        VerifyHistory(rt_update, roattrA_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all scheduled peers, but the current advertised state has
//              other peers that are not covered by the scheduled peerset. We
//              should not treat this as a duplicate update.  Instead we need
//              to withdraw the route from some current advertised peers.
//              Different attribute for all current advertised peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, vSchedPeerCount, kPeerCount-1,
                vSchedPeerCount+1);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1,
                vSchedPeerCount+1);
        VerifyHistory(rt_update, attr_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not need to receive the route per the new export results.
//              We should not treat this as a duplicate.  Need to cancel the
//              update to the peer.
//              Same attribute for all current advertised peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0, vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-2], attr A.
//              Reject peer x=[vSchedPeerCount-1,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate3) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrB_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-2);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-2);
        VerifyHistory(rt_update, roattrB_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not need to receive the route per the new export results.
//              We should not treat this as a duplicate.  Need to cancel the
//              update to the peer.
//              Different attribute for all current advertised peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0, vSchedPeerCount-2], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-2], attr x.
//              Reject peer x=[vSchedPeerCount-1,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate4) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(alt_attr_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-2);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-2);
        VerifyHistory(rt_update, alt_attr_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not have scheduled state but needs to receive the route
//              per the new export results.
//              We should not treat this as a duplicate. Need to schedule the
//              update to the peer.
//              Same attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0, vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr A.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr A.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], attr B.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate5) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrB_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attrA_, 0, vSchedPeerCount-2);
        Initialize();

        BuildExportResult(attrA_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattrA_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrB_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle route change where the old and new attributes are same
//              for all but one of the scheduled peers.  The peer in question
//              does not have scheduled state but needs to receive the route
//              per the new export results.
//              We should not treat this as a duplicate. Need to schedule the
//              update to the peer.
//              Different attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0, vSchedPeerCount-2], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr x.
// Export Rslt: Accept peer x=[0,vSchedPeerCount-1], attr x.
//              Reject peer x=[vSchedPeerCount,kPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-2], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate6) {
    for (int vSchedPeerCount = 2; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(alt_attr_, 0, vSchedPeerCount-2);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-2);
        Initialize();

        BuildExportResult(attr_, 0, vSchedPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, alt_attr_, 0, vSchedPeerCount-2);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Process a route that's already scheduled to be withdrawn from
//              all but one of the current peers.  Export policy rejects the
//              route for all peers so it should be withdrawn from all peers.
//              We should not treat this as a duplicate.  Need to schedule a
//              withdraw to the peer to which one isn't already scheduled.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr NULL.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate7) {
    for (int vSchedPeerCount = 2; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-2);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Process a route that's already scheduled to be withdrawn from
//              all but one of the current peers.  Export policy rejects the
//              route for all peers so it should be withdrawn from all peers.
//              We should not treat this as a duplicate.  Need to schedule a
//              withdraw to the peer to which one isn't already scheduled.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-2], attr NULL.
// Export Rslt: Reject peer x=[0,vSchedPeerCount-1].
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], alt_attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteUpdateTest2, NotDuplicate8) {
    for (int vSchedPeerCount = 2; vSchedPeerCount <= kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(alt_attr_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vSchedPeerCount-2);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateDequeueEnqueue(rt_update);
        VerifyUpdates(rt_update, roattr_null_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, alt_attr_, 0, vSchedPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that is currently advertised to all peers and scheduled
//              to all peers with a different attribute is processed again and
//              the export policy produces the same attribute as the advertised
//              state.  Should get rid of scheduled updates and convert to a
//              RouteState.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr B.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr A.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Redundant1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
                vCurrPeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
            InitUpdateInfo(attrB_, 0, vCurrPeerCount-1, qid);
            Initialize();

            BuildExportResult(attrA_, 0, vCurrPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, roattrA_, 0, vCurrPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Route that is currently advertised to all peers and scheduled
//              to all peers with a different attribute is processed again and
//              the export policy produces the same attribute as the advertised
//              state.  Should get rid of scheduled updates and convert to a
//              RouteState.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr x.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Redundant2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
                vCurrPeerCount++) {
            InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
            InitUpdateInfo(attrA_, 0, vCurrPeerCount-1, qid);
            Initialize();

            BuildExportResult(attr_, 0, vCurrPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, attr_, 0, vCurrPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Route that is currently advertised to all peers and scheduled
//              to to be withdrawn from all peers is processed again and the
//              export policy produces the same attribute as the advertised
//              state. Should get rid of scheduled withdraw and convert to a
//              RouteState.
//              Same attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr A.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, Redundant3) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        VerifyHistory(rstate, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to all peers and scheduled
//              to to be withdrawn from all peers is processed again and the
//              export policy produces the same attribute as the advertised
//              state. Should get rid of scheduled withdraw and convert to a
//              RouteState.
//              Different attribute for all current peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr x.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, Redundant4) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        InitUpdateInfo(attr_null_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        VerifyHistory(rstate, attr_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers and is scheduled for other peers.  This should be a noop
//              since all join peers have current or scheduled state for the
//              route. Should not even run export policy in this case.
//              Same attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Join Peers:  Peers x=[0,kPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest2, JoinNoop1) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vSchedPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, kPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, roattrB_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers and is scheduled for other peers.  This should be a noop
//              since all join peers have current or scheduled state for the
//              route. Should not even run export policy in this case.
//              Different attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
// Join Peers:  Peers x=[0,kPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vSchedPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vSchedPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, JoinNoop2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, vSchedPeerCount, kPeerCount-1);
        InitUpdateInfo(attr_, 0, vSchedPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, kPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyRouteUpdateNoDequeue(rt_update);
        EXPECT_EQ(rt_update_, rt_update);
        VerifyUpdates(rt_update, attr_, 0, vSchedPeerCount-1);
        VerifyHistory(rt_update, attr_, vSchedPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that has scheduled updates to some
//              peers, but to none of the joins peers. Export policy accepts
//              the route for all the join peers. Should upgrade RouteUpdate
//              to an UpdateList.
//              Same attribute for all current peers and all join peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr B.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr B.
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vJoinPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest2, JoinMerge1) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attrA_, vJoinPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, vJoinPeerCount, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attrB_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update[RibOutUpdates::QCOUNT];
        UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);

        VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                0, vJoinPeerCount-1);
        VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                vJoinPeerCount, kPeerCount-1);
        VerifyHistory(uplist, roattrA_, vJoinPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that has scheduled updates to some
//              peers, but to onoe of the joins peers. Export policy accepts
//              the route for all the join peers. Should upgrade RouteUpdate
//              to an UpdateList.
//              Different attribute for all current peers and all join peers.
//
// Old DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vJoinPeerCount-1], attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, JoinMerge2) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attrA_, vJoinPeerCount, kPeerCount-1);
        InitUpdateInfo(attr_, vJoinPeerCount, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
        UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
        VerifyUpdates(rt_update[RibOutUpdates::QBULK], attr_,
                0, vJoinPeerCount-1);
        VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], attr_,
                vJoinPeerCount, kPeerCount-1);
        VerifyHistory(uplist, roattrA_, vJoinPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised and
//              scheduled to all peers. The leave peerset is a proper subset
//              of the current/scheduled peerset. The peers corresponding to
//              the leave peerset should be removed from current/scheduled
//              state but the RouteUpdate itself should remain intact.
//              Common attribute for all current peers and a different but
//              common attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear1) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
            InitUpdateInfo(attrB_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrB_, vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update, roattrA_, vLeavePeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that is currently advertised to some
//              peers and scheduled to all the peers. The leave peerset is same
//              as the current peerset. The peers in the leave peerset should
//              be removed and the current state should become empty. The peers
//              in the leave peerset should also be removed from the scheduled
//              state. The RouteUpdate itself should remain intact.
//              Common attribute for all current peers and a different but
//              common attribute for all scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[0,vLeavePeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vLeavePeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vLeavePeerCount-1);
            InitUpdateInfo(attrB_, 0, kPeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, qid);
            VerifyRouteUpdateNoDequeue(rt_update);
            EXPECT_EQ(rt_update_, rt_update);
            VerifyUpdates(rt_update, roattrB_, vLeavePeerCount, kPeerCount-1);
            VerifyHistory(rt_update);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that is currently advertised to some
//              peers and scheduled to other peers. The leave peerset is same
//              as the scheduled peerset.  Should convert to a RouteUpdate and
//              preserve the current state.
//              Same attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear3) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, vLeavePeerCount, kPeerCount-1);
            InitUpdateInfo(attrA_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, roattrA_, vLeavePeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that is currently advertised to some
//              peers and scheduled to other peers. The leave peerset is same
//              as the scheduled peerset.  Should convert to a RouteUpdate and
//              preserve the current state.
//              Different attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr x.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear4) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attr_, vLeavePeerCount, kPeerCount-1);
            InitUpdateInfo(attr_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, attr_, vLeavePeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that is currently advertised to some
//              peers and scheduled to other peers. The leave peerset is union
//              of the current and scheduled peersets.  Remove all current and
//              scheduled state and clean up the DBState.
//              Same attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr A.
// Leave Peers: Peers x=[0,kPeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear5) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, vLeavePeerCount, kPeerCount-1);
            InitUpdateInfo(attrA_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, kPeerCount-1);
            RunLeave(leave_peerset);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
}

//
// Description: Leave processing for route that is currently advertised to some
//              peers and scheduled to other peers. The leave peerset is same
//              as the scheduled peerset.  Should convert to a RouteUpdate and
//              preserve the current state.
//              Different attribute for all current and scheduled peers.
//
// Old DBState: RouteUpdate in qid=[QFIRST,QCOUNT-1].
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vLeavePeerCount-1], attr x.
// Leave Peers: Peers x=[0,kPeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteUpdateTest2, LeaveClear6) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attr_, vLeavePeerCount, kPeerCount-1);
            InitUpdateInfo(attr_, 0, vLeavePeerCount-1, qid);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, kPeerCount-1);
            RunLeave(leave_peerset);

            ExpectNullDBState(&rt_);
            DrainAndVerifyNoState(&rt_);
        }
    }
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
