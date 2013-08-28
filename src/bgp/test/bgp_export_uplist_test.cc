/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_export_test.h"

#include "base/logging.h"

using namespace std;

//
// Used for UpdateList tests.
//
class BgpExportUpdateListTest : public BgpExportTest {
protected:
    void InitAdvertiseInfo(BgpAttrPtr attrX, int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attrX, start_idx, end_idx, adv_slist_);
    }

    void InitAdvertiseInfo(BgpAttrPtr attr_blk[], int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attr_blk, start_idx, end_idx, adv_slist_);
    }

    void InitUpdateInfo(BgpAttrPtr attrX, int start_idx, int end_idx,
            int qid = RibOutUpdates::QUPDATE) {
        InitUpdateInfoCommon(attrX, start_idx, end_idx, uinfo_slist_[qid]);
    }

    void InitUpdateInfo(BgpAttrPtr attr_blk[], int start_idx, int end_idx,
            int qid = RibOutUpdates::QUPDATE) {
        InitUpdateInfoCommon(attr_blk, start_idx, end_idx, uinfo_slist_[qid]);
    }

    void Initialize() {
        SchedulerStop();
        table_.SetExportResult(false);
        for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                qid++) {
            if (!uinfo_slist_[qid]->empty()) {
                rt_update_[qid] =
                    BuildRouteUpdate(&rt_, qid, uinfo_slist_[qid]);
            } else {
                rt_update_[qid] = NULL;
            }
        }
        uplist_ = BuildUpdateList(&rt_, rt_update_, adv_slist_);
    }

    AdvertiseSList adv_slist_;
    UpdateInfoSList uinfo_slist_[RibOutUpdates::QCOUNT];
    RouteUpdate *rt_update_[RibOutUpdates::QCOUNT];
    UpdateList *uplist_;
};

TEST_F(BgpExportUpdateListTest, Basic1) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        DrainAndDeleteRouteState(&rt_);
    }
}

TEST_F(BgpExportUpdateListTest, Basic2) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attr_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(alt_attr_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(alt_attr_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle delete of route for which we have scheduled updates in
//              both QBULK and QUPDATE. Should schedule a withdraw for all the
//              peers for which we have current state. There should be no other
//              scheduled updates other than that.
//              The RouteUpdate for QUPDATE should get reused.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr NULL.
//
TEST_F(BgpExportUpdateListTest, WithdrawDeleted1) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        EXPECT_EQ(rt_update_[RibOutUpdates::QUPDATE], rt_update);
        VerifyUpdates(rt_update, roattr_null_, vPendPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Handle change for route for which we have scheduled updates in
//              both QBULK and QUPDATE.  Export policy results in an attribute
//              for all peers which is the same as the current advertised and
//              scheduled state. Should get rid of update in QBULK and schedule
//              updates in QUPDATE for peers that do not have advertised state.
//              The RouteUpdate for QUPDATE should get reused.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount+vSchedPeerCount,
//                                    kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,
//                                   vPendPeerCount+vSchedPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount+vSchedPeerCount,
//                                    kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vPendPeerCount+vSchedPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, Clobber11) {
    for (int vPendPeerCount = 1; vPendPeerCount <= kPeerCount-2;
            vPendPeerCount++) {
        for (int vSchedPeerCount = 1;
                vSchedPeerCount <= kPeerCount-vPendPeerCount-1;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_,
                    vPendPeerCount+vSchedPeerCount, kPeerCount-1);
            InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
            InitUpdateInfo(attrA_,
                    vPendPeerCount, vPendPeerCount+vSchedPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            BuildExportResult(attrA_, 0, kPeerCount-1);
            RunExport();
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
            EXPECT_EQ(rt_update_[RibOutUpdates::QUPDATE], rt_update);
            VerifyUpdates(rt_update, roattrA_,
                    0, vPendPeerCount+vSchedPeerCount-1);
            VerifyHistory(rt_update, roattrA_,
                    vPendPeerCount+vSchedPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle change for route for which we have scheduled updates in
//              both QBULK and QUPDATE. Export policy results in a new shared
//              attribute for all peers. Should get rid of the update in QBULK
//              and schedule new updates in QUPDATE for all peers.
//              The RouteUpdate for QUPDATE should get reused.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr C.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr C.
//
TEST_F(BgpExportUpdateListTest, Clobber12) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        BuildExportResult(attrC_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        EXPECT_EQ(rt_update_[RibOutUpdates::QUPDATE], rt_update);
        VerifyUpdates(rt_update, roattrC_, 0, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Simulates scenario where the UpdateList has RouteUpdates in
//              multiple queues but none in QUPDATE.  Export policy results
//              in no attribute change for current and join pending peers.
//              Scheduled updates in all queues must be removed but current
//              advertised state will be retained and new udpates in QUPDATE
//              should be scheduled for peers that were join pending earlier.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, Clobber21) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        BuildExportResult(attrA_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrA_, 0, vPendPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Simulates scenario where the UpdateList has RouteUpdates in
//              multiple queues but none in QUPDATE.  Export policy results
//              in an attribute change for current and join pending peers.
//              Scheduled updates in all queues must be removed but current
//              advertised state will be retained and new udpates in QUPDATE
//              should be scheduled for all current and join pending peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr B.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, Clobber22) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        BuildExportResult(attrB_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrB_, 0, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Simulates scenario where the UpdateList has RouteUpdates in
//              multiple queues but none in QUPDATE.  Export policy results
//              in no attribute change for peers to which the route has been
//              advertised and a different attribute for the peers for which
//              the joins were pending. Scheduled updates in all queues must
//              be removed but the current advertised state will be retained
//              and new udpates in QUPDATE should be scheduled for peers that
//              need attibute change.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
// Export Rslt: Accept peer x=[0,vPendPeerCount-1], attr C.
//              Accept peer x=[vPendPeerCount,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vPendPeerCount-1], attr C.
//
TEST_F(BgpExportUpdateListTest, Clobber23) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        BgpAttrVec res_attr_vec;
        RibPeerSetVec res_peerset_vec;
        UpdateInfoSList res_slist;
        RibPeerSet pend_peerset;
        BuildPeerSet(pend_peerset, 0, vPendPeerCount-1);
        BuildVectors(res_attr_vec, res_peerset_vec, attrC_, &pend_peerset);
        RibPeerSet adv_peerset;
        BuildPeerSet(adv_peerset, vPendPeerCount, kPeerCount-1);
        BuildVectors(res_attr_vec, res_peerset_vec, attrA_, &adv_peerset);
        BuildUpdateInfo(&res_attr_vec, &res_peerset_vec, res_slist);
        table_.SetExportResult(res_slist);

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrC_, 0, vPendPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Simulates scenario where the UpdateList has RouteUpdates in
//              multiple queues but none in QUPDATE.  Export policy rejects
//              the route for all join pending peers and accepts for all the
//              current advertised peers. Scheduled updates in all queues must
//              be removed and the current advertised state will be retained.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
// Export Rslt: Reject peer x=[0,vPendPeerCount-1].
//              Accept peer x=[vPendPeerCount,kPeerCount-1], attr A.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, Clobber3) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        BuildExportResult(attrA_, vPendPeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        VerifyHistory(rstate, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle delete of route for which we have scheduled updates in
//              both QBULK and QUPDATE but no current advertised state. Should
//              get rid of all scheduled updates and clean up the DBState.
//
// Old DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
// Export Rslt: Not executed, Route is deleted.
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, Clobber41) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrA_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Handle change for route for which we have scheduled updates in
//              both QBULK and QUPDATE but no current advertised state. Export
//              policy rejects the route for all peers.  Should get rid of all
//              scheduled updates and clean up the DBState.
//
// Old DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,kPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, Clobber42) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrA_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Simulates scenario where an UpdateList has no current state and
//              has RouteUpdates in multiple queues but none in QUPDATE. Export
//              policy rejects the route for all peers. Should get rid of all
//              scheduled updates and clean up the DBState.
//
// Old DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,kPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, Clobber51) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Simulates scenario where an UpdateList has no current state and
//              has RouteUpdates in multiple queues but none in QUPDATE and the
//              route is deleted. Should get rid of all scheduled updates and
//              clean up the DBState.
//
// Old DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr A.
// Export Rslt: Not executed, Route is deleted.
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, Clobber52) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitUpdateInfo(attrA_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Handle join for route where the join peerset is a subset of the
//              peers for which we have current advertised state. Should return
//              without even running export policy.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vCurrPeerCount,
//                                   vCurrPeerCount+vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vCurrPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vCurrPeerCount,
//                                   vCurrPeerCount+vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vCurrPeerCount+vPendPeerCount-1,
//                                   kPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, JoinNoop1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount-2;
            vCurrPeerCount++) {
        for (int vPendPeerCount = 1;
                vPendPeerCount <= kPeerCount-vCurrPeerCount-1;
                vPendPeerCount++) {
            for (int vJoinPeerCount = 1; vJoinPeerCount <= vCurrPeerCount;
                    vJoinPeerCount++) {
                InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
                InitUpdateInfo(attrA_,
                        vCurrPeerCount, vCurrPeerCount+vPendPeerCount-1,
                        RibOutUpdates::QBULK);
                InitUpdateInfo(attrA_,
                        vCurrPeerCount+vPendPeerCount, kPeerCount-1,
                        RibOutUpdates::QUPDATE);
                Initialize();

                RibPeerSet join_peerset;
                BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
                RunJoin(join_peerset);
                table_.VerifyExportResult(false);

                RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
                UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
                EXPECT_EQ(uplist_, uplist);
                for (int qid = RibOutUpdates::QFIRST;
                        qid < RibOutUpdates::QCOUNT; qid++) {
                    EXPECT_EQ(rt_update_[qid], rt_update[qid]);
                }
                VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrA_,
                        vCurrPeerCount, vCurrPeerCount+vPendPeerCount-1);
                VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrA_,
                        vCurrPeerCount+vPendPeerCount, kPeerCount-1);
                VerifyHistory(uplist, roattrA_, 0, vCurrPeerCount-1);

                DrainAndDeleteRouteState(&rt_);
            }
        }
    }
}

//
// Description: Handle join for route where the join peerset is a subset of the
//              peers for which we have scheduled updates in QUPDATE. Should
//              return without even running export policy.
//
// Old DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[0,vSchedPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, JoinNoop2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        for (int vJoinPeerCount = 1; vJoinPeerCount <= vSchedPeerCount;
                vJoinPeerCount++) {
            InitUpdateInfo(attrA_, vSchedPeerCount, kPeerCount-1,
                    RibOutUpdates::QBULK);
            InitUpdateInfo(attrA_, 0, vSchedPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet join_peerset;
            BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
            RunJoin(join_peerset);
            table_.VerifyExportResult(false);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
            EXPECT_EQ(uplist_, uplist);
            for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT;
                    qid++) {
                EXPECT_EQ(rt_update_[qid], rt_update[qid]);
            }
            VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrA_,
                    vSchedPeerCount, kPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrA_,
                    0, vSchedPeerCount-1);
            VerifyHistory(uplist);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Simulates scenario where an UpdateList has no current state and
//              has RouteUpdates in multiple queues but none in QBULK.  Export
//              policy accepts the route for all the peers in the join peerset.
//              Should create a new RouteUpdate for the join peers in QBULK and
//              combine that with the existing RouteUpdate in QUPDATE to create
//              an UpdateList.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr B.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr B.
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vJoinPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, JoinMerge1) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attrA_, vJoinPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, vJoinPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attrB_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
        UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
        EXPECT_EQ(uplist_, uplist);

        VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                0, vJoinPeerCount-1);
        VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                vJoinPeerCount, kPeerCount-1);
        VerifyHistory(uplist, roattrA_, vJoinPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Handle join for route for which we have scheduled updates in
//              both QBULK and QUPDATE.  Merge in the new join peers with the
//              join pending peers in QBULK.  All scheduled and join pending
//              peers have the same attribute.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                    kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vJoinPeerCount,
//                                   vJoinPeerCount+vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr B.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr B.
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                    kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,
//                                   vJoinPeerCount+vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, JoinMerge2) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount-2;
            vJoinPeerCount++) {
        for (int vPendPeerCount = 1;
                vPendPeerCount <= kPeerCount-vJoinPeerCount-1;
                vPendPeerCount++) {
            InitAdvertiseInfo(attrA_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);
            InitUpdateInfo(attrB_,
                    vJoinPeerCount, vJoinPeerCount+vPendPeerCount-1,
                    RibOutUpdates::QBULK);
            InitUpdateInfo(attrB_, vJoinPeerCount+vPendPeerCount, kPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet join_peerset;
            BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
            BuildExportResult(attrB_, 0, vJoinPeerCount-1);
            RunJoin(join_peerset);
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
            EXPECT_EQ(uplist_, uplist);

            VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                    0, vJoinPeerCount+vPendPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);
            VerifyHistory(uplist, roattrA_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Handle join for route for which we have scheduled updates in
//              both QBULK and QUPDATE.  Merge in the new join peers with the
//              join pending peers in QBULK.  All scheduled and join pending
//              peers have different attribute.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                    kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vJoinPeerCount,
//                                   vJoinPeerCount+vPendPeerCount-1], attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                    kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vJoinPeerCount+vPendPeerCount-1], attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vJoinPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr x.
//
TEST_F(BgpExportUpdateListTest, JoinMerge3) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount-2;
            vJoinPeerCount++) {
        for (int vPendPeerCount = 1;
                vPendPeerCount <= kPeerCount-vJoinPeerCount-1;
                vPendPeerCount++) {
            InitAdvertiseInfo(attrA_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);
            InitUpdateInfo(attr_, vJoinPeerCount,
                    vJoinPeerCount+vPendPeerCount-1, RibOutUpdates::QBULK);
            InitUpdateInfo(attr_, vJoinPeerCount+vPendPeerCount, kPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet join_peerset;
            BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
            BuildExportResult(attr_, 0, vJoinPeerCount-1);
            RunJoin(join_peerset);
            table_.VerifyExportResult(true);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
            EXPECT_EQ(uplist_, uplist);

            VerifyUpdates(rt_update[RibOutUpdates::QBULK], attr_,
                    0, vJoinPeerCount+vPendPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], attr_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);
            VerifyHistory(uplist, roattrA_,
                    vJoinPeerCount+vPendPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is a proper
//              subset of the peers for which we have pending updates in QBULK.
//              The pending peers in QBULK will be trimmed, but the UpdateList
//              doesn't go away since there's at least one peers with a pending
//              update in QBULK.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vLeavePeerCount,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, LeaveClear1) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < vPendPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
            InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
            InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
            VerifyHistory(uplist, roattrA_, vPendPeerCount, kPeerCount-1);

            VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                    vLeavePeerCount, vPendPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                    vPendPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is a proper
//              subset of the peers for there are scheduled updates in QUPDATE
//              as well as peers for which we have current state. The scheduled
//              peers in QUPDATE and the current state will be trimmed, but the
//              UpdateList doesn't go away since there's at least one peer with
//              a scheduled update in QUPDATE.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: UpdateList.
//              AdvertiseInfo peer x=[vLeavePeerCount,
//                                    vSchedPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vLeavePeerCount,vSchedPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, LeaveClear2) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        for (int vLeavePeerCount = 1; vLeavePeerCount < vSchedPeerCount;
                vLeavePeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
            InitUpdateInfo(attrB_, vSchedPeerCount, kPeerCount-1,
                    RibOutUpdates::QBULK);
            InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);

            VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                    vSchedPeerCount, kPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                    vLeavePeerCount, vSchedPeerCount-1);
            VerifyHistory(uplist, roattrA_, vLeavePeerCount, vSchedPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as peers for which we have current state. The current state in
//              the UpdateList will become empty, but the UpdateList doesn't go
//              away since we still have pending updates in QBULK and scheduled
//              updates in QUPDATE.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vCurrPeerCount,
//                                   vCurrPeerCount+vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vCurrPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr A
// Leave Peers: Peers x=[0,vCurrPeerCount-1].
// New DBState: UpdateList.
//              No AdvertiseInfo.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vCurrPeerCount,
//                                   vCurrPeerCount+vPendPeerCount-1], attr A.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vCurrPeerCount+vPendPeerCount,
//                                   kPeerCount-1], attr A
//
TEST_F(BgpExportUpdateListTest, LeaveClear3) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount-2;
            vCurrPeerCount++) {
        for (int vPendPeerCount = 1;
                vPendPeerCount <= kPeerCount-vCurrPeerCount-1;
                vPendPeerCount++) {
            InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
            InitUpdateInfo(attrB_,
                    vCurrPeerCount, vCurrPeerCount+vPendPeerCount-1,
                    RibOutUpdates::QBULK);
            InitUpdateInfo(attrB_, vCurrPeerCount+vPendPeerCount, kPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vCurrPeerCount-1);
            RunLeave(leave_peerset);

            RouteUpdate *rt_update[RibOutUpdates::QCOUNT] = { NULL };
            UpdateList *uplist = ExpectUpdateList(&rt_, rt_update);
            VerifyHistory(uplist);

            VerifyUpdates(rt_update[RibOutUpdates::QBULK], roattrB_,
                    vCurrPeerCount, vCurrPeerCount+vPendPeerCount-1);
            VerifyUpdates(rt_update[RibOutUpdates::QUPDATE], roattrB_,
                    vCurrPeerCount+vPendPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as peers for which we have join pending state in QBULK.  The
//              the UpdateList gets downgraded to a RouteUpdate in QUPDATE and
//              the current advertised state is preserved.
//              Common attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vPendPeerCount-1].
// New DBState: Route Update in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, LeaveClear41) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vPendPeerCount-1);
        RunLeave(leave_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrB_, vPendPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as peers for which we have join pending state in QBULK.  The
//              the UpdateList gets downgraded to a RouteUpdate in QUPDATE and
//              the current advertised state is preserved.
//              Different attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr x.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], alt_attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], alt_attr x.
// Leave Peers: Peers x=[0,vPendPeerCount-1].
// New DBState: Route Update in QUPDATE.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr x.
//              UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], alt_attr x.
//
TEST_F(BgpExportUpdateListTest, LeaveClear42) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attr_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(alt_attr_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(alt_attr_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vPendPeerCount-1);
        RunLeave(leave_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, alt_attr_, vPendPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, attr_, vPendPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as peers for which we have scheduled updates in QUPDATE as well
//              as current advertised state.  The scheduled state and current
//              advertised state are cleared, and the UpdateList is downgraded
//              to a RouteUpdate in QBULK.
//              Common attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[0,vSchedPeerCount-1], attr B.
// Leave Peers: Peers x=[0,vSchedPeerCount-1].
// New DBState: Route Update in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportUpdateListTest, LeaveClear51) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vSchedPeerCount-1);
        InitUpdateInfo(attrB_, vSchedPeerCount, kPeerCount-1,
                RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, 0, vSchedPeerCount-1, RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vSchedPeerCount-1);
        RunLeave(leave_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, roattrB_, vSchedPeerCount, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as peers for which we have scheduled updates in QUPDATE as well
//              as current advertised state.  The scheduled state and current
//              advertised state are cleared, and the UpdateList is downgraded
//              to a RouteUpdate in QBULK.
//              Different attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[0,vSchedPeerCount-1], attr x.
//              Route Update in QBULK.
//                UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], alt_attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[0,vSchedPeerCount-1], alt_attr x.
// Leave Peers: Peers x=[0,vSchedPeerCount-1].
// New DBState: Route Update in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vSchedPeerCount,kPeerCount-1], alt_attr x.
//
TEST_F(BgpExportUpdateListTest, LeaveClear52) {
    for (int vSchedPeerCount = 1; vSchedPeerCount < kPeerCount;
            vSchedPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vSchedPeerCount-1);
        InitUpdateInfo(alt_attr_, vSchedPeerCount, kPeerCount-1,
                RibOutUpdates::QBULK);
        InitUpdateInfo(alt_attr_, 0, vSchedPeerCount-1, RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vSchedPeerCount-1);
        RunLeave(leave_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, alt_attr_, vSchedPeerCount, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as the unions of peers for which we have pending joins in QBULK
//              and scheduled updates in QUPDATE.The join pending and scheduled
//              update states are cleared, and the UpdateList is downgraded to
//              a RouteState.
//              Common attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,
//                                   vPendPeerCount+vSchedPeerCount-1], attr B
// Leave Peers: Peers x=[0,vPendPeerCount+vSchedPeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vPendPeerCount+vSchedPeerCount,
//                                    kPeerCount-1], attr A.
//
TEST_F(BgpExportUpdateListTest, LeaveClear61) {
    for (int vPendPeerCount = 1; vPendPeerCount <= kPeerCount-2;
            vPendPeerCount++) {
        for (int vSchedPeerCount = 1;
                vSchedPeerCount <= kPeerCount-vPendPeerCount-1;
                vSchedPeerCount++) {
            InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
            InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
            InitUpdateInfo(attrB_,
                    vPendPeerCount, vPendPeerCount+vSchedPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vPendPeerCount+vSchedPeerCount-1);
            RunLeave(leave_peerset);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, roattrA_,
                    vPendPeerCount+vSchedPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as the unions of peers for which we have pending joins in QBULK
//              and scheduled updates in QUPDATE.The join pending and scheduled
//              update states are cleared, and the UpdateList is downgraded to
//              a RouteState.
//              Different attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], alt_attr x.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,
//                                   vPendPeerCount+vSchedPeerCount-1], attr x
// Leave Peers: Peers x=[0,vPendPeerCount+vSchedPeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vPendPeerCount+vSchedPeerCount,
//                                    kPeerCount-1], alt_attr x.
//
TEST_F(BgpExportUpdateListTest, LeaveClear62) {
    for (int vPendPeerCount = 1; vPendPeerCount <= kPeerCount-2;
            vPendPeerCount++) {
        for (int vSchedPeerCount = 1;
                vSchedPeerCount <= kPeerCount-vPendPeerCount-1;
                vSchedPeerCount++) {
            InitAdvertiseInfo(alt_attr_, vPendPeerCount, kPeerCount-1);
            InitUpdateInfo(attr_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
            InitUpdateInfo(attr_,
                    vPendPeerCount, vPendPeerCount+vSchedPeerCount-1,
                    RibOutUpdates::QUPDATE);
            Initialize();

            RibPeerSet leave_peerset;
            BuildPeerSet(leave_peerset, 0, vPendPeerCount+vSchedPeerCount-1);
            RunLeave(leave_peerset);

            RouteState *rstate = ExpectRouteState(&rt_);
            VerifyHistory(rstate, alt_attr_,
                    vPendPeerCount+vSchedPeerCount, kPeerCount-1);

            DrainAndDeleteRouteState(&rt_);
        }
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as the unions of peers for which we have pending joins in QBULK
//              scheduled updates in QUPDATE and current advertised state.  All
//              pending, scheduled and current state is cleared and DBState is
//              cleaned up.
//              Common attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr A.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], attr B.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], attr B.
// Leave Peers: Peers x=[0,kPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, LeaveClear71) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attrA_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(attrB_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(attrB_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, kPeerCount-1);
        RunLeave(leave_peerset);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing for route where the leave peerset is the same
//              as the unions of peers for which we have pending joins in QBULK
//              scheduled updates in QUPDATE and current advertised state.  All
//              pending, scheduled and current state is cleared and DBState is
//              cleaned up.
//              Different attribute for peers.
//
// Old DBState: UpdateList.
//              AdvertiseInfo peer x=[vPendPeerCount,kPeerCount-1], attr x.
//              Route Update in QBULK.
//                UpdateInfo peer x=[0,vPendPeerCount-1], alt_attr x.
//              Route Update in QUPDATE.
//                UpdateInfo peer x=[vPendPeerCount,kPeerCount-1], alt_attr x.
// Leave Peers: Peers x=[0,kPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportUpdateListTest, LeaveClear72) {
    for (int vPendPeerCount = 1; vPendPeerCount < kPeerCount;
            vPendPeerCount++) {
        InitAdvertiseInfo(attr_, vPendPeerCount, kPeerCount-1);
        InitUpdateInfo(alt_attr_, 0, vPendPeerCount-1, RibOutUpdates::QBULK);
        InitUpdateInfo(alt_attr_, vPendPeerCount, kPeerCount-1,
                RibOutUpdates::QUPDATE);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, kPeerCount-1);
        RunLeave(leave_peerset);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
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
