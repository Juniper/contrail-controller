/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_export_test.h"

#include "base/logging.h"

using namespace std;


// See comments at the top of bgp_export_nostate_test.cc for more info about
// the test methodlogy.

//
// Used for RouteState tests.
//
class BgpExportRouteStateTest : public BgpExportTest {
protected:
    void InitAdvertiseInfo(BgpAttrPtr attrX, int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attrX, start_idx, end_idx, adv_slist_);
    }

    void InitAdvertiseInfo(BgpAttrPtr attr_blk[], int start_idx, int end_idx) {
        InitAdvertiseInfoCommon(attr_blk, start_idx, end_idx, adv_slist_);
    }

    void Initialize() {
        ASSERT_TRUE(!adv_slist_->empty());
        SchedulerStop();
        table_.SetExportResult(false);
        rstate_ = BuildRouteState(&rt_, adv_slist_);
    }

    AdvertiseSList adv_slist_;
    RouteState *rstate_;
};

TEST_F(BgpExportRouteStateTest, Basic1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();
        DrainAndDeleteRouteState(&rt_);
    }
}

TEST_F(BgpExportRouteStateTest, Basic2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();
        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Attribute change for an advertised route.
//              Same new attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr B.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr B.
//
TEST_F(BgpExportRouteStateTest, Advertise1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrB_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Policy change causes existing route to be advertised to an
//              additional set of peers. Need to create updates for the new
//              peers, but not resend to the current set of peers.
//              Same attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[vCurrPeerCount, kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, Advertise2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        EXPECT_TRUE(vCurrPeerCount > 0 && vCurrPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrA_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Policy change or route change causes existing route to be sent
//              again to all current peers and an additional set of peers with
//              a new attribute. Need to create updates for all the peers.
//              Same attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr B.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteStateTest, Advertise3) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        EXPECT_TRUE(vCurrPeerCount > 0 && vCurrPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrB_, 0, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Policy change or route change causes existing route to be sent
//              again to all current peers and an additional set of peers with
//              a new attribute. Need to create updates for all the peers.
//              Different attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0, vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[vCurrPeerCount, kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, Advertise4) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, attr_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers due to a policy change. The new
//              policy rejects the route for all peers.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,vCurrPeerCount-1].
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw1a) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers due to a policy change. The new
//              policy rejects the route for all peers.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Export Rslt: Reject peer x=[0,vCurrPeerCount-1].
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw1b) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vCurrPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers because they are inactive.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: If vCurrPeerCount == kPeerCount
//                Not executed, there are no active peers.
//              else
//                Reject peer x=[0,kPeerCount-1].
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw2a) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();
        DeactivatePeers(0, vCurrPeerCount-1);

        RunExport();
        table_.VerifyExportResult(vCurrPeerCount == kPeerCount ? false : true);

        RibPeerSet active_peerset;
        BuildPeerSet(active_peerset, vCurrPeerCount, kPeerCount-1);
        table_.VerifyExportPeerset(active_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndVerifyNoState(&rt_);
        ReactivatePeers(0, vCurrPeerCount-1);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers because they are inactive.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Export Rslt: If vCurrPeerCount == kPeerCount
//                Not executed, there are no active peers.
//              else
//                Reject peer x=[0,vCurrPeerCount-1].
// New DBState: RouteUpdate.
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw2b) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();
        DeactivatePeers(0, vCurrPeerCount-1);

        RunExport();
        table_.VerifyExportResult(vCurrPeerCount == kPeerCount ? false : true);

        RibPeerSet active_peerset;
        BuildPeerSet(active_peerset, vCurrPeerCount, kPeerCount-1);
        table_.VerifyExportPeerset(active_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vCurrPeerCount-1);

        DrainAndVerifyNoState(&rt_);
        ReactivatePeers(0, vCurrPeerCount-1);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from a subset of the current peers due to policy
//              change. The new policy rejects the route for some peers.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,vRejectPeerCount-1].
//              Accept peer x=[vRejectPeerCount,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vRejectPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw3) {
    for (int vRejectPeerCount = 1; vRejectPeerCount < kPeerCount;
            vRejectPeerCount++) {
        EXPECT_TRUE(vRejectPeerCount > 0 && vRejectPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, vRejectPeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vRejectPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from a subset of the current peers due to policy
//              change. The new policy rejects the route for some peers.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
// Export Rslt: Reject peer x=[0,vRejectPeerCount-1].
//              Accept peer x=[vRejectPeerCount,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vRejectPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, Withdraw4) {
    for (int vRejectPeerCount = 1; vRejectPeerCount < kPeerCount;
            vRejectPeerCount++) {
        EXPECT_TRUE(vRejectPeerCount > 0 && vRejectPeerCount < kPeerCount);

        InitAdvertiseInfo(attr_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attr_, vRejectPeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vRejectPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers when it gets deleted.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, WithdrawDeleted1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be withdrawn from all peers when it gets deleted.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Export Rslt: Not executed, Route is deleted.
// New DBState: RouteUpdate in QUPDATE.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vCurrPeerCount-1], attr NULL.
//
TEST_F(BgpExportRouteStateTest, WithdrawDeleted2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        rt_.MarkDelete();
        RunExport();
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vCurrPeerCount-1);
        VerifyHistory(rt_update, attr_, 0, vCurrPeerCount-1);

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
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,vRejectPeerCount-1].
//              Accept peer x=[vRejectPeerCount,kPeerCount-1], attr B.
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vRejectPeerCount-1], attr NULL.
//              UpdateInfo peer x=[vRejectPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteStateTest, AdvertiseAndWithdraw1) {
    for (int vRejectPeerCount = 1; vRejectPeerCount < kPeerCount;
            vRejectPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        BuildExportResult(attrB_, vRejectPeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vRejectPeerCount-1, 2);
        VerifyUpdates(rt_update, roattrB_, vRejectPeerCount, kPeerCount-1, 2);
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
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
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
TEST_F(BgpExportRouteStateTest, AdvertiseAndWithdraw2) {
    for (int qid = RibOutUpdates::QFIRST; qid < RibOutUpdates::QCOUNT; qid++) {
        int vRejectPeerCount = 2;
        int vChangePeerCount = 2;
        EXPECT_TRUE(vRejectPeerCount > 0 && vRejectPeerCount < kPeerCount);
        EXPECT_TRUE(vChangePeerCount > 0 && vChangePeerCount < kPeerCount);
        EXPECT_TRUE(vRejectPeerCount + vChangePeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
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
        RibPeerSet reject_peerset;
        BuildPeerSet(reject_peerset, 0, vRejectPeerCount-1);
        VerifyUpdates(rt_update, roattr_null_, reject_peerset, 3);
        VerifyUpdates(rt_update, roattrC_,
                vRejectPeerCount, vRejectPeerCount+vChangePeerCount-1, 3);
        VerifyUpdates(rt_update, roattrB_,
                vRejectPeerCount+vChangePeerCount, kPeerCount-1, 3);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to a set of peers has to
//              be re-advertised with a different attribute to some existing
//              peers and withdrawn from other existing peers because they
//              have become inactive.
//              Same attribute for all peers for which route is accepted.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Export Rslt: Reject peer x=[0,vInactivePeerCount-1].
//              Accept peer x=[vInactivePeerCount,kPeerCount-1], attr B.
// New DBState: RouteUpdate.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//              UpdateInfo peer x=[0,vInactivePeerCount-1], attr NULL.
//              UpdateInfo peer x=[vInactivePeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteStateTest, AdvertiseAndWithdraw3) {
    for (int vInactivePeerCount = 1; vInactivePeerCount < kPeerCount;
            vInactivePeerCount++) {
        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();
        DeactivatePeers(0, vInactivePeerCount-1);

        BuildExportResult(attrB_, vInactivePeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RibPeerSet active_peerset;
        BuildPeerSet(active_peerset, vInactivePeerCount, kPeerCount-1);
        table_.VerifyExportPeerset(active_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattr_null_, 0, vInactivePeerCount-1, 2);
        VerifyUpdates(rt_update, roattrB_, vInactivePeerCount, kPeerCount-1, 2);
        VerifyHistory(rt_update, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
        ReactivatePeers(0, vInactivePeerCount-1);
    }
}

//
// Description: Route that is currently advertised to all peers is processed
//              again and the export policy result is unchanged.  Should not
//              enqueue any updates.
//              Same attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr A.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, Redundant1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attrA_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Route that is currently advertised to all peers is processed
//              again and the export policy result is unchanged.  Should not
//              enqueue any updates.
//              Different attribute for all peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Export Rslt: Accept peer x=[0,vCurrPeerCount-1], attr x.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, Redundant2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        BuildExportResult(attr_, 0, vCurrPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, attr_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to all
//              peers.  This should be a noop since all join peers have state
//              for the route. Should not even run export policy in this case.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, JoinNoop1) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        EXPECT_TRUE(vJoinPeerCount > 0 && vJoinPeerCount <= kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to all
//              peers.  This should be a noop since all join peers have state
//              for the route. Should not even run export policy in this case.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, short circuited by noop checks.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, JoinNoop2) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attr_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(false);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for deleted route that is currently advertised
//              to all peers. This should be a noop since the route is deleted.
//              Should not even run export policy in this case.
//              Same attribute for all current peers.
//              Note that negative updates will be generated from Export.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, route is deleted.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, JoinNoop3) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        EXPECT_TRUE(vJoinPeerCount > 0 && vJoinPeerCount <= kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        rt_.MarkDelete();
        RunJoin(join_peerset);
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for deleted route that is currently advertised
//              to all peers. This should be a noop since the route is deleted.
//              Should not even run export policy in this case.
//              Different attribute for all current peers.
//              Note that negative updates will be generated from Export.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, route is deleted.
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, JoinNoop4) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attr_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        rt_.MarkDelete();
        RunJoin(join_peerset);
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers.  Export policy rejects the route for all the join peers.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Join Peers:  Peers x=[vCurrPeerCount,kPeerCount-1].
// Export Rslt: Reject peer x=[vCurrPeerCount,kPeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, JoinReject1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        EXPECT_TRUE(vCurrPeerCount > 0 && vCurrPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, vCurrPeerCount, kPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers.  Export policy accepts the route for all the join peers.
//              Same attribute for all current peers and all join peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Join Peers:  Peers x=[vCurrPeerCount,kPeerCount-1].
// Export Rslt: Accept peer x=[vCurrPeerCount,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QBULK.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1].
//              UpdateInfo peer x=[vCurrPeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, JoinMerge1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        EXPECT_TRUE(vCurrPeerCount > 0 && vCurrPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, vCurrPeerCount, kPeerCount-1);
        BuildExportResult(attrA_, vCurrPeerCount, kPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, roattrA_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers.  Export policy accepts the route for all the join peers.
//              Different attribute for all current peers and all join peers.
//
// Old DBState: RouteState.
//              No AdvertiseInfo peer x=[0,vJoinPeerCount-1].
//              AdvertiseInfo peer x=[vJoinPeerCount, kPeerCount-1], attr x.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: RouteUpdate in QBULK.
//              AdvertiseInfo peer x=[vJoinPeerCount, kPeerCount-1], attr x.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, JoinMerge2) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount;
            vJoinPeerCount++) {
        InitAdvertiseInfo(attr_, vJoinPeerCount, kPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, attr_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update, attr_, vJoinPeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Join processing for route that is currently advertised to some
//              peers.  Export policy accepts the route for all the join peers.
//              All current peers share one attribtue and and all join peers
//              share a different attribute.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Join Peers:  Peers x=[vCurrPeerCount,kPeerCount-1].
// Export Rslt: Accept peer x=[vCurrPeerCount,kPeerCount-1], attr B.
// New DBState: RouteUpdate in QBULK.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attrA_.
//              UpdateInfo peer x=[vCurrPeerCount,kPeerCount-1], attr B.
//
TEST_F(BgpExportRouteStateTest, JoinMerge3) {
    for (int vCurrPeerCount = 1; vCurrPeerCount < kPeerCount;
            vCurrPeerCount++) {
        EXPECT_TRUE(vCurrPeerCount > 0 && vCurrPeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, vCurrPeerCount, kPeerCount-1);
        BuildExportResult(attrB_, vCurrPeerCount, kPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, roattrB_, vCurrPeerCount, kPeerCount-1);
        VerifyHistory(rt_update, roattrA_, 0, vCurrPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to all
//              peers in the leave peerset.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vCurrPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteStateTest, LeaveClear1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vCurrPeerCount-1);
        RunLeave(leave_peerset);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to all
//              peers in the leave peerset.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vCurrPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteStateTest, LeaveClear2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vCurrPeerCount-1);
        RunLeave(leave_peerset);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to a
//              superset of peers in the leave peerset.
//              Same attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, LeaveClear3) {
    for (int vLeavePeerCount = 2; vLeavePeerCount < kPeerCount;
            vLeavePeerCount++) {
        EXPECT_TRUE(vLeavePeerCount > 0 && vLeavePeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
        RunLeave(leave_peerset);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, vLeavePeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to a
//              superset of peers in the leave peerset.
//              Different attribute for all current peers.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, LeaveClear4) {
    for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
            vLeavePeerCount++) {
        InitAdvertiseInfo(attr_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
        RunLeave(leave_peerset);

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, attr_, vLeavePeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to all
//              peers in the leave peerset.
//              Same attribute for all current peers.
//              Route is marked deleted.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vCurrPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteStateTest, LeaveDeleted1) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attrA_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vCurrPeerCount-1);
        rt_.MarkDelete();
        RunLeave(leave_peerset);
        rt_.ClearDelete();

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to all
//              peers in the leave peerset.
//              Different attribute for all current peers.
//              Route is marked deleted.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,vCurrPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vCurrPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportRouteStateTest, LeaveDeleted2) {
    for (int vCurrPeerCount = 1; vCurrPeerCount <= kPeerCount;
            vCurrPeerCount++) {
        InitAdvertiseInfo(attr_, 0, vCurrPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vCurrPeerCount-1);
        rt_.MarkDelete();
        RunLeave(leave_peerset);
        rt_.ClearDelete();

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to a
//              superset of peers in the leave peerset.
//              Same attribute for all current peers.
//              Route is marked deleted.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr A.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportRouteStateTest, LeaveDeleted3) {
    for (int vLeavePeerCount = 2; vLeavePeerCount < kPeerCount;
            vLeavePeerCount++) {
        EXPECT_TRUE(vLeavePeerCount > 0 && vLeavePeerCount < kPeerCount);

        InitAdvertiseInfo(attrA_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
        rt_.MarkDelete();
        RunLeave(leave_peerset);
        rt_.ClearDelete();

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, roattrA_, vLeavePeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing for route that is currently advertised to a
//              superset of peers in the leave peerset.
//              Different attribute for all current peers.
//              Route is marked deleted.
//
// Old DBState: RouteState.
//              AdvertiseInfo peer x=[0,kPeerCount-1], attr x.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: RouteState.
//              AdvertiseInfo peer x=[vLeavePeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportRouteStateTest, LeaveDeleted4) {
    for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount;
            vLeavePeerCount++) {
        InitAdvertiseInfo(attr_, 0, kPeerCount-1);
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
        rt_.MarkDelete();
        RunLeave(leave_peerset);
        rt_.ClearDelete();

        RouteState *rstate = ExpectRouteState(&rt_);
        EXPECT_EQ(rstate_, rstate);
        VerifyHistory(rstate, attr_, vLeavePeerCount, kPeerCount-1);

        DrainAndDeleteRouteState(&rt_);
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
