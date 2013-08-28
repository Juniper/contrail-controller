/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_export_test.h"

#include "base/logging.h"

using namespace std;

//
// OVERVIEW
//
// This is the unit test suite for the BgpExport class. It includes tests for
// Export, Join. Refresh and Leave processing.  As Export/Join/Referesh/Leave
// methods work on one route at a time, each test in this suite also does the
// same. Since a BgpExport object is associated with a single RibOut, we also
// use a single BgpTable with a single RibOut.
//
// Each test associates a specific type of DBState with the route. This could
// be NULL or a RouteState, RouteUpdate or UpdateList. The desired results of
// export policy are then programmed into the mock table. One of the BgpExport
// methods is invoked and the new DBState for the route is verified.
//
// TEST STEPS
//
// 1.  Create mock peers and register them to the ribout for all queues. The
//     peers are stored in the peers_ vector. Create block of RibPeerSets for
//     the case where each peer has a unique attribute for current/scheduled
//     state.
// 2.  Create discrete (A,B,C) and block attributes (attr_, alt_attr_).
// 3.  Create any AdvertiseInfo and UpdateInfo objects that are needed.
// 4.  Stop the TaskScheduler.
// 5.  Create the desired input DBState object and associate it with the route.
// 6.  Build the desired export policy result and associate it with mock table.
// 7.  Run the Export/Join/Leave method that needs to be tested.
// 8.  Verify the new DBState and it's scheduled and current state.
// 9.  Start the scheduler to drain any scheduled updates.
// 10. Delete the final RouteState or verify that there's no DBState.
//
// CONVENTIONS
//
// vSchedPeerCount: Number of peers with scheduled updates in QUPDATE.  If the
// test has a loop that iterates through the RibOutUpdates::QueueId enum, this
// variable is used in place of vPendPeerCount.
//
// vCurrPeerCount: Number of peers with current advertised state i.e. history.
// vPendPeerCount: Number of peers with scheduled updates in QBULK.
// vJoinPeerCount: Number of peers that are to be processing for Join.
// vLeavePeerCount: Number of peers that are to be processing for Leave.
// vAcceptPeerCount: Number of peers for which export policy result is Accept.
// vRejectPeerCount: Number of peers for which export policy result is Reject.
//
// The kPeerCount peers that are created during test setup are viewed as being
// divided into 1 or more (usually 2-3) blocks of the above types.  The blocks
// can overlap.
//
// There are two types of attributes used in the tests.  In one case, all the
// peers share a common discrete attribute (A/B/C) i.e. there's a single entry
// in the UpdateInfoSList/AdvertiseInfoSList.  In another case, each peer has
// a different attribute from one of the attribute blocks (attr_/alt_attr_).
// The idea is to exercise different code paths when setting/clearing/merging
// the RibPeerSet.
//

//
// Used when there's no DBState.
//
class BgpExportNoStateTest : public BgpExportTest {
protected:
    void Initialize() {
        SchedulerStop();
        table_.SetExportResult(false);
    }
};

TEST_F(BgpExportNoStateTest, Basic) {
    Initialize();
    table_.VerifyExportResult(false);
    DrainAndVerifyNoState(&rt_);
}

//
// Description: New route is rejected for all peers by policy.
//
// Old DBState: None.
// Export Rslt: Reject peer x=[0,kPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, Reject1) {
    Initialize();

    RunExport();
    table_.VerifyExportResult(true);

    ExpectNullDBState(&rt_);
    DrainAndVerifyNoState(&rt_);
}

//
// Description: New route is rejected because ribout has no active peers.
//
// Old DBState: None.
// Export Rslt: Not executed, there are no active peers.
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, Reject2) {
    Initialize();

    DeactivatePeers(0, kPeerCount-1);
    RunExport();
    table_.VerifyExportResult(false);

    ExpectNullDBState(&rt_);
    DrainAndVerifyNoState(&rt_);
}

//
// Description: Route previously rejected by policy for all peers is deleted.
//
// Old DBState: None.
// Export Rslt: Not executed, Route is deleted.
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, RejectDeleted) {
    Initialize();

    rt_.MarkDelete();
    RunExport();
    rt_.ClearDelete();
    table_.VerifyExportResult(false);

    ExpectNullDBState(&rt_);
    DrainAndVerifyNoState(&rt_);
}

//
// Description: New route is accepted by policy.
//              Same attribute for all peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[0,vAcceptPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vAcceptPeerCount-1], attr A.
//
TEST_F(BgpExportNoStateTest, Advertise1) {
    for (int vAcceptPeerCount = 1; vAcceptPeerCount < kPeerCount-1;
            vAcceptPeerCount++) {
        Initialize();

        BuildExportResult(attrA_, 0, vAcceptPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrA_, 0, vAcceptPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: New route is accepted by policy.
//              Different attribute for all peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[0,vAcceptPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vAcceptPeerCount-1], attr x.
//
TEST_F(BgpExportNoStateTest, Advertise2) {
    for (int vAcceptPeerCount = 1; vAcceptPeerCount < kPeerCount-1;
            vAcceptPeerCount++) {
        Initialize();

        BuildExportResult(attr_, 0, vAcceptPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, attr_, 0, vAcceptPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: New route is accepted and advertised to all active peers.
//              Same attribute for all peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[vInactivePeerCount,kPeerCount-1], attr A.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vInactivePeerCount,kPeerCount-1], attr A.
//
TEST_F(BgpExportNoStateTest, Advertise3) {
    for (int vInactivePeerCount = 1; vInactivePeerCount < kPeerCount;
            vInactivePeerCount++) {
        Initialize();
        DeactivatePeers(0, vInactivePeerCount-1);

        BuildExportResult(attrA_, vInactivePeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RibPeerSet active_peerset;
        BuildPeerSet(active_peerset, vInactivePeerCount, kPeerCount-1);
        table_.VerifyExportPeerset(active_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, roattrA_, vInactivePeerCount, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
        ReactivatePeers(0, vInactivePeerCount-1);
    }
}

//
// Description: New route is accepted and advertised to all active peers.
//              Different attribute for all peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[vInactivePeerCount,kPeerCount-1], attr x.
// New DBState: RouteUpdate in QUPDATE.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[vInactivePeerCount,kPeerCount-1], attr x.
//
TEST_F(BgpExportNoStateTest, Advertise4) {
    for (int vInactivePeerCount = 1; vInactivePeerCount < kPeerCount;
            vInactivePeerCount++) {
        Initialize();
        DeactivatePeers(0, vInactivePeerCount-1);

        BuildExportResult(attr_, vInactivePeerCount, kPeerCount-1);
        RunExport();
        table_.VerifyExportResult(true);

        RibPeerSet active_peerset;
        BuildPeerSet(active_peerset, vInactivePeerCount, kPeerCount-1);
        table_.VerifyExportPeerset(active_peerset);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_);
        VerifyUpdates(rt_update, attr_, vInactivePeerCount, kPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
        ReactivatePeers(0, vInactivePeerCount-1);
    }
}

//
// Description: Unadvertised route is rejected via policy for all join peers.
//
// Old DBState: None.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Reject peer x=[0,vJoinPeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, JoinReject) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount-1;
            vJoinPeerCount++) {
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Deleted route is rejected for all join peers w/o running policy.
//
// Old DBState: None.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Not executed, Route is deleted.
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, JoinDeleted) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount-1;
            vJoinPeerCount++) {
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        rt_.MarkDelete();
        RunJoin(join_peerset);
        rt_.ClearDelete();
        table_.VerifyExportResult(false);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Unadvertised route is accepted by policy for all join peers.
//              Same attribute for all join peers.
//
// Old DBState: None.
// Join Peers:  Peers x=[0,vJoinPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr A.
// New DBState: RouteUpdate in QBULK.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr A.
//
TEST_F(BgpExportNoStateTest, JoinAdvertise1) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        Initialize();

        BuildExportResult(attrA_, 0, vJoinPeerCount-1);

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, roattrA_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Unadvertised route is accepted by policy for all join peers.
//              Different attribute for all join peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr x.
//
TEST_F(BgpExportNoStateTest, JoinAdvertise2) {
    for (int vJoinPeerCount = 1; vJoinPeerCount <= kPeerCount;
            vJoinPeerCount++) {
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, vJoinPeerCount-1);
        BuildExportResult(attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, attr_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Unadvertised route is accepted by policy for a subset of join
//              peers.
//              Same attribute for all accepted join peers.
//
// Old DBState: None.
// Join Peers:  Peers x=[0,kPeerCount-1].
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr A.
// New DBState: RouteUpdate in QBULK.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr A.
//
TEST_F(BgpExportNoStateTest, JoinAdvertise3) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount-1;
            vJoinPeerCount++) {
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, kPeerCount-1);
        BuildExportResult(attrA_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, roattrA_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Unadvertised route is accepted by policy for a subset of join
//              peers.
//              Different attribute for all accepted join peers.
//
// Old DBState: None.
// Export Rslt: Accept peer x=[0,vJoinPeerCount-1], attr x.
// New DBState: RouteUpdate in QBULK.
//              No AdvertiseInfo.
//              UpdateInfo peer x=[0,vJoinPeerCount-1], attr x.
//
TEST_F(BgpExportNoStateTest, JoinAdvertise4) {
    for (int vJoinPeerCount = 1; vJoinPeerCount < kPeerCount-1;
            vJoinPeerCount++) {
        Initialize();

        RibPeerSet join_peerset;
        BuildPeerSet(join_peerset, 0, kPeerCount-1);
        BuildExportResult(attr_, 0, vJoinPeerCount-1);
        RunJoin(join_peerset);
        table_.VerifyExportResult(true);

        RouteUpdate *rt_update = ExpectRouteUpdate(&rt_, RibOutUpdates::QBULK);
        VerifyUpdates(rt_update, attr_, 0, vJoinPeerCount-1);
        VerifyHistory(rt_update);

        DrainAndDeleteRouteState(&rt_);
    }
}

//
// Description: Leave processing should be a noop for unadvertised route.
//
// Old DBState: None.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, LeaveNoop1) {
    for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount-1;
            vLeavePeerCount++) {
        Initialize();

        RibPeerSet leave_peerset;
        BuildPeerSet(leave_peerset, 0, vLeavePeerCount-1);
        RunLeave(leave_peerset);

        ExpectNullDBState(&rt_);
        DrainAndVerifyNoState(&rt_);
    }
}

//
// Description: Leave processing should be a noop for unadvertised route
//              that is marked for deletion.
//
// Old DBState: None.
// Leave Peers: Peers x=[0,vLeavePeerCount-1].
// New DBState: None.
//
TEST_F(BgpExportNoStateTest, LeaveNoop2) {
    for (int vLeavePeerCount = 1; vLeavePeerCount < kPeerCount-1;
            vLeavePeerCount++) {
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
