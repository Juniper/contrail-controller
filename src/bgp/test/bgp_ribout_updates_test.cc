/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/test/bgp_ribout_updates_test.h"

#include "base/logging.h"

using namespace std;

// Routes:   Nothing is enqueued.
// Blocking: None.
// Result:   Noop.
TEST_F(RibOutUpdatesTest, TailDequeueNoop1) {
    UpdateRibOut();
    VerifyUpdateCount(0, kPeerCount-1, COUNT_0);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(0);
}

// Pre-Condition: All peers are unblocked but out of sync.
// Result: Noop.
TEST_F(RibOutUpdatesTest, TailDequeueNoop2) {

    // First get all the peers blocked and hence out of sync.
    EnqueueDefaultRoute();
    SetPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(1);

    // Now unblock all of them and enqueue another update.
    ClearMessageCount();
    SetPeerUnblockNow(0, kPeerCount-1);
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);
    BuildRouteUpdate(routes_[0], uinfo_slist);

    // Verify that we didn't send the update.
    UpdateRibOut();
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(0);
}

// Routes:   Default route enqueued to all peers.
// Blocking: None.
// Result:   Route gets sent to all peers.
TEST_F(RibOutUpdatesTest, TailDequeueBasic1) {
    EnqueueDefaultRoute();
    UpdateRibOut();
    VerifyDefaultRoute();
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(1);
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers, attr A.
// Blocking: None.
// Result:   Routes get sent to all peers in 1 update.
TEST_F(RibOutUpdatesTest, TailDequeueBasic2a) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Loop for different number of routes.
    for (int vRouteCount = 1; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers for withdraw.
// Blocking: None.
// Result:   Routes get withdrawn from all peers in 1 update.
TEST_F(RibOutUpdatesTest, TailDequeueBasic2b) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attr_null_, 0, kPeerCount-1);

    // Loop for different number of routes.
    for (int vRouteCount = 1; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            ExpectNullDBState(routes_[idx]);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers.
//           Routes with even x have attrA.
//           Routes with odd x have attr_null_ i.e. need to be withdrawn.
// Blocking: None.
// Result:   Even routes get advertised to all peers in 1 update.
//           Odd routes get withdrawn from all peers in 1 update.
TEST_F(RibOutUpdatesTest, TailDequeueBasic2c) {

    // Build UpdateInfo for attr A/null with all peers.
    UpdateInfoSList uinfo_slist[2];
    PrependUpdateInfo(uinfo_slist[0], attrA_, 0, kPeerCount-1);
    PrependUpdateInfo(uinfo_slist[1], attr_null_, 0, kPeerCount-1);

    // Loop for different number of routes.
    for (int vRouteCount = 2; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            if (idx % 2 == 0) {
                RouteState *rstate = ExpectRouteState(routes_[idx]);
                VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
            } else {
                ExpectNullDBState(routes_[idx]);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers, attr x.
// Blocking: None
// Result:   Routes get sent to all peers in vRouteCount updates.
TEST_F(RibOutUpdatesTest, TailDequeueBasic3) {

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kRouteCount];
    for (int idx = 0; idx < kRouteCount; idx++) {
        PrependUpdateInfo(uinfo_slist[idx], attr_[idx], 0, kPeerCount-1);
    }

    // Loop for different number of routes.
    for (int vRouteCount = 1; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, (Count) vRouteCount);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyMessageCount(vRouteCount);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attr_[idx], 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers.  Each route
//           has just one UpdateInfo. The RibOutAttr in the UpdateInfos for
//           all the routes have same BgpAttrPtr but different labels.
//           The label for route at index x is 16+x.
// Blocking: None
// Result:   Routes get sent to all peers in 1 update since we should pack
//           prefixes with the same BgpAttr but different labels in a single
//           update.
TEST_F(RibOutUpdatesTest, TailDequeueBasic4) {

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kRouteCount];
    for (int idx = 0; idx < kRouteCount; idx++) {
        PrependUpdateInfo(uinfo_slist[idx], attrA_, 16+idx, 0, kPeerCount-1);
    }

    // Loop for different number of routes.
    for (int vRouteCount = 1; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, (Count) 1);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            RibOutAttr roattrX(attrA_.get(), 16+idx);
            VerifyHistory(rstate, roattrX, 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers. Each route
//           has one of vAttrCount attributes.  Every vAttrCount'th route
//           has the same attribute.
// Blocking: None
// Result:   Routes get sent to all peers in vAttrCount updates.
TEST_F(RibOutUpdatesTest, TailDequeueBasic5) {

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
            0, kPeerCount-1);
    }

    // Loop for different number of attributes.
    for (int vAttrCount = 1; vAttrCount <= kAttrCount; vAttrCount++) {
        BGP_DEBUG_UT("vAttrCount = " << vAttrCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for all routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            int attr_idx = idx % vAttrCount;
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[attr_idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, (Count) vAttrCount);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyMessageCount(vAttrCount);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            int attr_idx = idx % vAttrCount;
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attr_[attr_idx], 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,2048-1] enqueued to all peers, attr A.
// Blocking: None.
// Result:   Routes get sent to all peers in 3 updates. Each update message
//           can hold approx. 1000 prefixes when the prefixes are IPv4 /24s.
TEST_F(RibOutUpdatesTest, TailDequeueScaled1a) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Create additional routes so that the total is 2K.
    for (int idx = kRouteCount; idx < 2048; idx++) {
        CreateRoute(idx);
    }

    // Build updates for all the routes.
    for (int idx = 0; idx < 2048; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_3);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(3);

    // Verify DB State for the routes.
    for (int idx = 0; idx < 2048; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,2048-1] enqueued to all peers.
//           Routes with even x have attr A.
//           Routes with odd x have attr B.
// Blocking: None.
// Result:   Routes get sent to all peers in 4 updates. Each update message
//           can hold approx. 1000 prefixes when the prefixes are IPv4 /24s.
TEST_F(RibOutUpdatesTest, TailDequeueScaled1b) {

    // Build UpdateInfos for attr A/B with all peers.
    UpdateInfoSList uinfo_slist[2];
    PrependUpdateInfo(uinfo_slist[0], attrA_, 0, kPeerCount-1);
    PrependUpdateInfo(uinfo_slist[1], attrB_, 0, kPeerCount-1);

    // Create additional routes so that the total is 2K.
    for (int idx = kRouteCount; idx < 2048; idx++) {
        CreateRoute(idx);
    }

    // Build updates for all the routes.
    for (int idx = 0; idx < 2048; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_4);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(4);

    // Verify DB State for the routes.
    for (int idx = 0; idx < 2048; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        if (idx % 2 == 0) {
            VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
        } else {
            VerifyHistory(rstate, attrB_, 0, kPeerCount-1);
        }
    }
}

// Routes:   Routes x=[0,2048-1] enqueued to all peers.
//           Routes with even x have attr A.
//           Routes with odd x have attr null i.e. need to be withdrawn.
// Blocking: None.
// Result:   Routes get sent to all peers in 4 updates. Each update message
//           can hold approx. 1000 prefixes when the prefixes are IPv4 /24s.
TEST_F(RibOutUpdatesTest, TailDequeueScaled1c) {

    // Build UpdateInfos for attr A/B with all peers.
    UpdateInfoSList uinfo_slist[2];
    PrependUpdateInfo(uinfo_slist[0], attrA_, 0, kPeerCount-1);
    PrependUpdateInfo(uinfo_slist[1], attr_null_, 0, kPeerCount-1);

    // Create additional routes so that the total is 2K.
    for (int idx = kRouteCount; idx < 2048; idx++) {
        CreateRoute(idx);
    }

    // Build updates for all the routes.
    for (int idx = 0; idx < 2048; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_4);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(4);

    // Verify DB State for the routes.
    for (int idx = 0; idx < 2048; idx++) {
        if (idx % 2 == 0) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
        } else {
            ExpectNullDBState(routes_[idx]);
        }
    }
}

// Routes:   Default route enqueued to all peers.
// Peers:    Total of 2K peers.
// Blocking: None.
// Result:   Route gets sent to all peers.
TEST_F(RibOutUpdatesTest, TailDequeueScaled2a) {
    for (int idx = kPeerCount; idx < 2048; idx++) {
        CreatePeer();
    }
    EnqueueDefaultRoute();
    UpdateRibOut();
    VerifyDefaultRoute();
    VerifyUpdateCount(0, 2048-1, COUNT_1);
    VerifyPeerInSync(0, 2048-1, true);
    VerifyMessageCount(1);
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Peers:    Total of 2K peers.
// Blocking: None.
// Result:   Routes get sent to all peers in 1 update.
TEST_F(RibOutUpdatesTest, TailDequeueScaled2b) {

    // Create additional peers so that the total is 2K.
    for (int idx = kPeerCount; idx < 2048; idx++) {
        CreatePeer();
    }

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, 2048-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, 2048-1, COUNT_1);
    VerifyPeerBlock(0, 2048-1, false);
    VerifyPeerInSync(0, 2048-1, true);
    VerifyMessageCount(1);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attrA_, 0, 2048-1);
    }
}

// Routes:   Route x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peer x=[0,vBlockPeerCount-1] are blocked and hence unsync.
// Result:   Routes x=[0,kRouteCount-1] still need to be advertised to the
//           unsync peers. Hence there should be a RouteUpdate with the
//           UpdateInfo for those peers.
//           The TailDequeue loop terminated because there are no more
//           updates for the peers in the tail marker.
TEST_F(RibOutUpdatesTest, TailDequeueUnsync1a) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for all the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlockNow(0, vBlockPeerCount-1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vBlockPeerCount-1, COUNT_0);
        VerifyUpdateCount(vBlockPeerCount, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attrA_, 0, vBlockPeerCount-1);
            VerifyHistory(rt_update, attrA_, vBlockPeerCount, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers with attr[x].
// Blocking: Peer x=[0,vBlockPeerCount-1] are blocked and hence unsync.
// Result:   Routes x=[0,kRouteCount-1] still need to be advertised to the
//           unsync peers. Hence there should be a RouteUpdate with the
//           UpdateInfo for those peers.
//           The TailDequeue loop terminated because there are no more
//           updates for the peers in the tail marker.
TEST_F(RibOutUpdatesTest, TailDequeueUnsync1b) {

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kRouteCount];
    for (int idx = 0; idx < kRouteCount; idx++) {
        PrependUpdateInfo(uinfo_slist[idx], attr_[idx], 0, kPeerCount-1);
    }

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for all routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlockNow(0, vBlockPeerCount-1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vBlockPeerCount-1, COUNT_0);
        VerifyUpdateCount(vBlockPeerCount, kPeerCount-1, (Count) kRouteCount);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyMessageCount(kRouteCount);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attr_[idx], 0, vBlockPeerCount-1);
            VerifyHistory(rt_update, attr_[idx], vBlockPeerCount, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Route x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peer x=[0,kPeerCount-1] are blocked and hence unsync.
// Result:   No routes should be advertised to any peers since all of them are
//           unsync. Hence there should be a RouteUpdate with the UpdateInfo
//           for all peers.
TEST_F(RibOutUpdatesTest, TailDequeueUnsync2a) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetPeerBlockNow(0, kPeerCount-1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_0);
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyMessageCount(0);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attrA_, 0, kPeerCount-1);
        VerifyHistory(rt_update);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers with attr[x].
// Blocking: Peer x=[0,vBlockPeerCount-1] are blocked and hence unsync.
// Result:   Routes x=[0,kRouteCount-1] still need to be advertised to the
//           unsync peers. Hence there should be a RouteUpdate with the
//           UpdateInfo for those peers.
//           The TailDequeue loop terminated because there are no more
//           updates for the peers in the tail marker.
TEST_F(RibOutUpdatesTest, TailDequeueUnsync2b) {

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kRouteCount];
    for (int idx = 0; idx < kRouteCount; idx++) {
        PrependUpdateInfo(uinfo_slist[idx], attr_[idx], 0, kPeerCount-1);
    }

    // Build updates for all routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetPeerBlockNow(0, kPeerCount-1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_0);
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyMessageCount(0);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attr_[idx], 0, kPeerCount-1);
        VerifyHistory(rt_update);
    }
}

// Routes:   Default route enqueued to all peers.
// Blocking: All peers block after STEP_1.
// Result:   Route gets sent to all peers, hence it must have a RouteState.
TEST_F(RibOutUpdatesTest, TailDequeueAllBlock1) {
    EnqueueDefaultRoute();

    SetPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(1);

    VerifyDefaultRoute();
}

// Routes:   Route x=[0,vRouteCount-1] enqueued to all peers, attr A.
// Blocking: All peers block after STEP_1.
// Result:   Routes get sent to all peers in 1 update.
//           Since the routes were all sent, they should have RouteState.
TEST_F(RibOutUpdatesTest, TailDequeueAllBlock2) {


    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Loop for different number of routes.
    for (int vRouteCount = 1; vRouteCount <= kRouteCount; vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, kPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Route x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peer x=[0,vBlockPeerCount-1] blocked after STEP_1.
// Result:   Routes x=[0,kRouteCount-1] should get advertised to all the peers
//           including blocked peers.  Hence there should be a RouteState for
//           all routes.
//           The TailDequeue loop terminated because there are no more updates.
TEST_F(RibOutUpdatesTest, TailDequeuePartialBlock0) {
    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for all routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vBlockPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyPeerBlock(vBlockPeerCount, kPeerCount-1, false);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default Route enqueued to all peers.
//           Route x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peer x=[0,vBlockPeerCount-1] blocked after STEP_1.
//           Peer x=[vBlockPeerCount,kPeerCount-1] blocked after STEP_2.
// Result:   Routes x=[0,kRouteCount-1] still need to be advertised to the
//           blocked peers. Hence there should be a RouteUpdate with the
//           UpdateInfo for those peers.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_2.
TEST_F(RibOutUpdatesTest, TailDequeuePartialBlock1) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for the default route and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vBlockPeerCount-1, STEP_1);
        SetPeerBlock(vBlockPeerCount, kPeerCount-1, STEP_2);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vBlockPeerCount-1, COUNT_1);
        VerifyUpdateCount(vBlockPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        VerifyDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attrA_, 0, vBlockPeerCount-1);
            VerifyHistory(rt_update, attrA_, vBlockPeerCount, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default Route enqueued to all peers.
//           Route x=[0,kRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peer x=[0,vBlockPeerCount-1] blocked after STEP_1.
// Result:   Routes x=[0,kRouteCount-1] still need to be advertised to the
//           blocked peers. Hence there should be a RouteUpdate with the
//           UpdateInfo for those peers.
//           The TailDequeue loop terminated because there are no more updates.
//           Peer x=[vBlockPeerCount, kPeerCount-1] are in sync.
//
TEST_F(RibOutUpdatesTest, TailDequeuePartialBlock2) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for the default route and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vBlockPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vBlockPeerCount-1, COUNT_1);
        VerifyUpdateCount(vBlockPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyPeerBlock(vBlockPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(vBlockPeerCount, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        VerifyDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attrA_, 0, vBlockPeerCount-1);
            VerifyHistory(rt_update, attrA_, vBlockPeerCount, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default Route enqueued to all peers.
//           Routes x=[0,kRouteCount-1] enqueued to all peers. Each route
//           has a different attribute.
// Blocking: Peer x=[0,kPeerCount-1] blocks after STEP_x+1.
// Result:   Routes x=[0,kPeerCount-2] will have both current and scheduled
//           state.  The rest of the route will only have scheduled state.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_#kPeerCount.
TEST_F(RibOutUpdatesTest, TailDequeuePartialBlock3) {
    ASSERT_TRUE(kPeerCount < kRouteCount);
    ASSERT_TRUE(kAttrCount >= kRouteCount);

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                0, kPeerCount-1);
    }

    // Build updates for the default route and all other routes.
    EnqueueDefaultRoute();
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Setup the block steps for the peers.
    for (int idx = 0; idx < kPeerCount; idx++) {
        SetPeerBlock(idx, (Step)(idx+1));
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    for (int idx = 0; idx < kPeerCount; idx++) {
        VerifyUpdateCount(idx, (Count) (idx+1));
    }
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    VerifyDefaultRoute();
    for (int idx = 0; idx <= kPeerCount-2; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attr_[idx], 0, idx);
        VerifyHistory(rt_update, attr_[idx], idx+1, kPeerCount-1);
    }
    for (int idx = kPeerCount-1; idx < kRouteCount; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attr_[idx], 0, kPeerCount-1);
        VerifyHistory(rt_update);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers. Each route
//           has a different attribute.
// Blocking: Peer x=[0,vBlockPeerCount-1] blocks after STEP_x+1.
// Result:   Routes x=[0,vBlockPeerCount-1] will each have scheduled updates
//           to different sets of peers. The rest of the routes have scheduled
//           updates for the first vBlockPeerCount peers.
//           The TailDequeue loop terminated because there are no more updates.
//           Peer x=[vBlockPeerCount, kPeerCount-1] are in sync.
TEST_F(RibOutUpdatesTest, TailDequeuePartialBlock4) {
    ASSERT_TRUE(kPeerCount < kRouteCount);
    ASSERT_TRUE(kAttrCount >= kRouteCount);

    // Build UpdateInfos for each attr index with all peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                0, kPeerCount-1);
    }

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for the default route and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Setup the block steps for the peers.
        for (int idx = 0; idx < vBlockPeerCount; idx++) {
            SetPeerBlock(idx, (Step)(idx+1));
        }

        // Dequeue the updates.
        UpdateRibOut();
        for (int idx = 0; idx < vBlockPeerCount; idx++) {
            VerifyUpdateCount(idx, (Count) (idx+1));
        }

        // Verify update counts and blocked state.
        VerifyUpdateCount(vBlockPeerCount, kPeerCount-1,
                (Count)(kRouteCount+1));
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyPeerBlock(vBlockPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(vBlockPeerCount, kPeerCount-1, true);
        VerifyMessageCount(kRouteCount+1);

        // Verify DB State for the routes.
        VerifyDefaultRoute();
        for (int idx = 0; idx < vBlockPeerCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attr_[idx], 0, idx);
            VerifyHistory(rt_update, attr_[idx], idx+1, kPeerCount-1);
        }
        for (int idx = vBlockPeerCount; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attr_[idx], 0, vBlockPeerCount-1);
            VerifyHistory(rt_update, attr_[idx], vBlockPeerCount, kPeerCount-1);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route the first vPeerCount peers have attr A and the
//           rest have attr B.
// Blocking: None.
// Result:   Routes get sent to all peers with one update per peer.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrs1) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfo for attr A with the first vPeerCount peers and for
        // attr B with the rest.
        UpdateInfoSList uinfo_slist;
        PrependUpdateInfo(uinfo_slist, attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist, attrB_, vPeerCount, kPeerCount-1);

        // Build updates for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
            VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers.
//           For each route for even x, the first vPeerCount peers have attr A
//           and the rest have attr B, and the other way round for odd x.
// Blocking: None.
// Result:   Routes get sent to all peers with 2 updates per peer.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrs2) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the odd and even routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[0], attrB_, vPeerCount, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrB_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, vPeerCount, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(4);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            if (idx % 2 == 0) {
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            } else {
                VerifyHistory(rstate, attrB_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrA_, vPeerCount, kPeerCount-1, 2);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, the first vPeerCount peers have attr A
//           and the rest have attr B.
//           For each route for odd x, all peers have attr A.
// Blocking: None.
// Result:   Routes get sent to the first vPeerCount peers with 1 update.
//           Routes get sent to the remaining peers with 2 updates.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrs3a) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the even and odd routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[0], attrB_, vPeerCount, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, 0, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vPeerCount-1, COUNT_1);
        VerifyUpdateCount(vPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(3);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            if (idx % 2 == 0) {
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            } else {
                VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, all peers have attr A.
//           For each route for odd x, the first vPeerCount peers have attr A
//           and the rest have attr B.
// Blocking: None.
// Result:   Routes get sent to the first vPeerCount peers with 2 updates.
//           Routes get sent to the remaining peers with 2 updates as well.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrs3b) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the even and odd routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrB_, vPeerCount, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(3);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            if (idx % 2 == 0) {
                VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
            } else {
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers. For each route
//           the first vPeerCount peers have attribue attr[x] and the rest have
//           alt_attr[x].
// Blocking: None.
// Result:   Routes get sent to all peers with one update per route per peer.
//           Hence each peer sends kRouteCount updates.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrs4) {
    ASSERT_TRUE(kRouteCount <= kAttrCount);

    // Loop for different number of peers for the attribute split.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for the routes with the UpdateInfos for each route
        // as described above.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList uinfo_slist;
            PrependUpdateInfo(uinfo_slist, attr_[idx], 0, vPeerCount-1);
            PrependUpdateInfo(uinfo_slist, alt_attr_[idx],
                    vPeerCount, kPeerCount-1);
            BuildRouteUpdate(routes_[idx], uinfo_slist);
        }

        // Dequeue the updates.
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, (Count) kRouteCount);
        VerifyPeerBlock(0, kPeerCount-1, false);
        VerifyPeerInSync(0, kPeerCount-1, true);
        VerifyMessageCount(2*kRouteCount);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attr_[idx], 0, vPeerCount-1, 2);
            VerifyHistory(rstate, alt_attr_[idx], vPeerCount, kPeerCount-1, 2);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route the first vPeerCount peers have attr A and the
//           rest have attr B.
// Blocking: Peers x=[0,kPeerCount-1] block after STEP_1.
// Result:   Routes get sent to all peers with one update per peer.
//           Since the routes were all sent, they should have RouteState.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_1.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrsBlock1) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfo for attr A with the first vPeerCount peers and for
        // attr B with the rest.
        UpdateInfoSList uinfo_slist;
        PrependUpdateInfo(uinfo_slist, attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist, attrB_, vPeerCount, kPeerCount-1);

        // Build updates for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, kPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, true);
        VerifyPeerInSync(0, kPeerCount-1, false);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
            VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, the first vPeerCount peers have attr A
//           and the rest have attr B, and the other way round for odd x.
// Blocking: Peers x=[0,kPeerCount-1] block after STEP_1.
// Result:   Routes x=[0,kRouteCount-1] for even x get sent, but not for odd x.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_1.
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrsBlock2a) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the odd and even routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[0], attrB_, vPeerCount, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrB_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, vPeerCount, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, kPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, true);
        VerifyPeerInSync(0, kPeerCount-1, false);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            if (idx % 2 == 0) {
                RouteState *rstate = ExpectRouteState(routes_[idx]);
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            } else {
                RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
                VerifyUpdates(rt_update, attrB_, 0, vPeerCount-1, 2);
                VerifyUpdates(rt_update, attrA_, vPeerCount, kPeerCount-1, 2);
                VerifyHistory(rt_update);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, the first vPeerCount peers have attr A
//           and the rest have attr B, and the other way round for odd x.
// Blocking: Peers y=[0,vPeerCount-1] block after STEP_1.
// Result:   Routes for even x get sent to all peers.
//           Routes for odd x get sent to peers y=[vPeerCount,kPeerCount-1].
//           The TailDequeue loop terminated because there are no more updates
//           for peers y=[vPeerCount,kPeerCount-1].
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrsBlock2b) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the odd and even routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[0], attrB_, vPeerCount, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrB_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, vPeerCount, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vPeerCount-1, COUNT_1);
        VerifyUpdateCount(vPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, vPeerCount-1, true);
        VerifyPeerBlock(vPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(0, vPeerCount-1, false);
        VerifyPeerInSync(vPeerCount, kPeerCount-1, true);
        VerifyMessageCount(3);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            if (idx % 2 == 0) {
                RouteState *rstate = ExpectRouteState(routes_[idx]);
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            } else {
                RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
                VerifyUpdates(rt_update, attrB_, 0, vPeerCount-1);
                VerifyHistory(rt_update, attrA_, vPeerCount, kPeerCount-1);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, the first vPeerCount peers have attr A
//           and the rest have attr B.
//           For each route for odd x, all peers have attr A.
// Blocking: Peers y=[0,vPeerCount-1] block after STEP_1.
// Result:   Routes get sent to the first vPeerCount peers with 1 update.
//           Routes get sent to the remaining peers with 2 updates.
//           The TailDequeue loop terminated because there are no more updates
//           for peers y=[vPeerCount,kPeerCount-1].
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrsBlock3a) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the even and odd routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[0], attrB_, vPeerCount, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, 0, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vPeerCount-1, COUNT_1);
        VerifyUpdateCount(vPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, vPeerCount-1, true);
        VerifyPeerBlock(vPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(0, vPeerCount-1, false);
        VerifyPeerInSync(vPeerCount, kPeerCount-1, true);
        VerifyMessageCount(3);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            if (idx % 2 == 0) {
                VerifyHistory(rstate, attrA_, 0, vPeerCount-1, 2);
                VerifyHistory(rstate, attrB_, vPeerCount, kPeerCount-1, 2);
            } else {
                VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route for even x, all peers have attr A.
//           For each route for odd x, the first vPeerCount peers have attr A
//           and the rest have attr B.
// Blocking: Peers y=[0,vPeerCount-1] block after STEP_1.
// Result:   Routes for even x get sent to all peers with 1 update.
//           Routes for odd x do not get sent to the first vPeerCount peers.
//           Routes for odd x get sent to the remaining peers with 1 update.
//           The TailDequeue loop terminated because there are no more updates
//           for peers y=[vPeerCount,kPeerCount-1].
TEST_F(RibOutUpdatesTest, DequeueCommonTwoAttrsBlock3b) {

    // Loop for different number of peers with attr A/B.
    for (int vPeerCount = 1; vPeerCount < kPeerCount; vPeerCount++) {
        BGP_DEBUG_UT("vPeerCount = " << vPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for the even and odd routes as described above.
        UpdateInfoSList uinfo_slist[2];
        PrependUpdateInfo(uinfo_slist[0], attrA_, 0, kPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrA_, 0, vPeerCount-1);
        PrependUpdateInfo(uinfo_slist[1], attrB_, vPeerCount, kPeerCount-1);

        // Build updates for desired number of routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, vPeerCount-1, COUNT_1);
        VerifyUpdateCount(vPeerCount, kPeerCount-1, COUNT_2);
        VerifyPeerBlock(0, vPeerCount-1, true);
        VerifyPeerBlock(vPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(0, vPeerCount-1, false);
        VerifyPeerInSync(vPeerCount, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            if (idx % 2 == 0) {
                RouteState *rstate = ExpectRouteState(routes_[idx]);
                VerifyHistory(rstate, attrA_, 0, kPeerCount-1);
            } else {
                RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
                VerifyUpdates(rt_update, attrA_, 0, vPeerCount-1);
                VerifyHistory(rt_update, attrB_, vPeerCount, kPeerCount-1);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route, peer y=[0,kPeerCount-1] has attribute attr[y].
// Blocking: None.
// Result:   Routes get sent to all peers with one update per peer.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrs1a) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist;
    BuildUpdateInfo(uinfo_slist, attr_, 0, kPeerCount-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route x, peer y=[0,kPeerCount-1] has BgpAttr attr[y] and
//           label 16+x.
// Blocking: None.
// Result:   Routes get sent to all peers with one update per peer. All routes
//           advertised to a peer have different labels but the same BgpAttr.
//           Hence they should all get packed into a single update.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrs1b) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build updates for all the routes with attributes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList uinfo_slist;
        BuildUpdateInfo(uinfo_slist, attr_, 16+idx, 0, kPeerCount-1);
        BuildRouteUpdate(routes_[idx], uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attr_, 16+idx, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For even x, peer y=[0,kPeerCount-1] has attribute attr[y].
//           For odd x, peer y=[0,kPeerCount-1] has attribute alt_attr[y].
// Blocking: None.
// Result:   Routes get sent to all peers with 2 updates per peer.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrs2a) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist[2];
    BuildUpdateInfo(uinfo_slist[0], attr_, 0, kPeerCount-1);
    BuildUpdateInfo(uinfo_slist[1], alt_attr_, 0, kPeerCount-1);

    // Build updates for even and odd routes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_2);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(2*kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        if (idx % 2 == 0) {
            VerifyHistory(rstate, attr_, 0, kPeerCount-1);
        } else {
            VerifyHistory(rstate, alt_attr_, 0, kPeerCount-1);
        }
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           Even x, peer y=[0,kPeerCount-1] has BgpAttr attr[y], label x.
//           Odd x, peer y=[0,kPeerCount-1] has BgpAttr alt_attr[y], label x.
// Blocking: None.
// Result:   Routes get sent to all peers with two update per peer. All routes
//           advertised to a peer have different labels but one of 2 BgpAttrs.
//           Hence they should all get packed into 2 updates.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrs2b) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build updates for all the routes with attributes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList uinfo_slist;
        if (idx % 2 == 0) {
            BuildUpdateInfo(uinfo_slist, attr_, 16+idx, 0, kPeerCount-1);
        } else {
            BuildUpdateInfo(uinfo_slist, alt_attr_, 16+idx, 0, kPeerCount-1);
        }
        BuildRouteUpdate(routes_[idx], uinfo_slist);
    }

    // Dequeue the updates.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_2);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(2*kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        if (idx % 2 == 0) {
            VerifyHistory(rstate, attr_, 16+idx, 0, kPeerCount-1);
        } else {
            VerifyHistory(rstate, alt_attr_, 16+idx, 0, kPeerCount-1);
        }
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route, peer y=[0,kPeerCount-1] has attribute attr[y].
// Blocking: Peers y=[0,kPeerCount-1] block after STEP_1.
// Result:   Routes get sent to all peers with one update per peer.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_1.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsBlock1) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist;
    BuildUpdateInfo(uinfo_slist, attr_, 0, kPeerCount-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For even x, peer y=[0,kPeerCount-1] has attribute attr[y].
//           For odd x, peer y=[0,kPeerCount-1] has attribute alt_attr[y].
// Blocking: Peers y=[0,kPeerCount-1] block after STEP_1.
// Result:   Routes for even x get sent to all peers with 1 update per peer.
//           Routes for odd x do not get sent to any peers.
//           The TailDequeue loop terminated because all peers are blocked
//           after STEP_1.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsBlock2) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist[2];
    BuildUpdateInfo(uinfo_slist[0], attr_, 0, kPeerCount-1);
    BuildUpdateInfo(uinfo_slist[1], alt_attr_, 0, kPeerCount-1);

    // Build updates for even and odd routes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, kPeerCount-1, true);
    VerifyPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        if (idx % 2 == 0) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attr_, 0, kPeerCount-1);
        } else {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, alt_attr_, 0, kPeerCount-1);
        }
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route, peer y=[0,kPeerCount-1] has attribute attr[y].
// Blocking: Peers y=[0,kPeerCount-1] for even y block after STEP_1.
// Result:   Routes get sent to all peers with one update per peer.
//           The TailDequeue loop terminated because there are no more updates
//           for peers with odd y.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsAltBlock1a) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist;
    BuildUpdateInfo(uinfo_slist, attr_, 0, kPeerCount-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetEvenPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyEvenPeerBlock(0, kPeerCount-1, true);
    VerifyOddPeerBlock(0, kPeerCount-1, false);
    VerifyEvenPeerInSync(0, kPeerCount-1, false);
    VerifyOddPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For each route, peer y=[0,kPeerCount-1] has attribute attr[y].
// Blocking: Peers y=[0,kPeerCount-1] for odd y block after STEP_1.
// Result:   Routes get sent to all peers with one update per peer.
//           The TailDequeue loop terminated because there are no more updates
//           for peers with even y.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsAltBlock1b) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist;
    BuildUpdateInfo(uinfo_slist, attr_, 0, kPeerCount-1);

    // Build updates for all the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetOddPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyEvenPeerBlock(0, kPeerCount-1, false);
    VerifyOddPeerBlock(0, kPeerCount-1, true);
    VerifyEvenPeerInSync(0, kPeerCount-1, true);
    VerifyOddPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(kPeerCount);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteState *rstate = ExpectRouteState(routes_[idx]);
        VerifyHistory(rstate, attr_, 0, kPeerCount-1);
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For even x, peer y=[0,kPeerCount-1] has attribute attr[y].
//           For odd x, peer y=[0,kPeerCount-1] has attribute alt_attr[y].
// Blocking: Peers y=[0,kPeerCount-1] for even y block after STEP_1.
// Result:   Routes for even x get sent to even y peers with 1 update per peer.
//           Routes for even x get sent to odd y peers with 1 update per peer.
//           Routes for odd x don't get sent to even y peers.
//           Routes for odd x get sent to odd y peers with 1 update per peer.
//           The TailDequeue loop terminated because there are no more updates
//           for peers with odd y.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsAltBlock2a) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);
    ASSERT_TRUE(kPeerCount % 2 == 0);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist[2];
    BuildUpdateInfo(uinfo_slist[0], attr_, 0, kPeerCount-1);
    BuildUpdateInfo(uinfo_slist[1], alt_attr_, 0, kPeerCount-1);

    // Build updates for even and odd routes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetEvenPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyEvenUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyOddUpdateCount(0, kPeerCount-1, COUNT_2);
    VerifyEvenPeerBlock(0, kPeerCount-1, true);
    VerifyOddPeerBlock(0, kPeerCount-1, false);
    VerifyEvenPeerInSync(0, kPeerCount-1, false);
    VerifyOddPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(3*kPeerCount/2);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        if (idx % 2 == 0) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            EXPECT_TRUE(rstate != NULL);
        } else {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            EXPECT_TRUE(rt_update != NULL);
        }
    }
}

// Routes:   Routes x=[0,kRouteCount-1] enqueued to all peers.
//           For even x, peer y=[0,kPeerCount-1] has attribute attr[y].
//           For odd x, peer y=[0,kPeerCount-1] has attribute alt_attr[y].
// Blocking: Peers y=[0,kPeerCount-1] for odd y block after STEP_1.
// Result:   Routes for even x get sent to even y peers with 1 update per peer.
//           Routes for even x get sent to odd y peers with 1 update per peer.
//           Routes for odd x get sent to even y peers with 1 update per peer.
//           Routes for odd x don't get sent to odd y peers.
//           The TailDequeue loop terminated because there are no more updates
//           for peers with even y.
TEST_F(RibOutUpdatesTest, DequeueCommonMultipleAttrsAltBlock2b) {
    ASSERT_TRUE(kPeerCount <= kAttrCount);
    ASSERT_TRUE(kPeerCount % 2 == 0);

    // Build UpdateInfo list as described above.
    UpdateInfoSList uinfo_slist[2];
    BuildUpdateInfo(uinfo_slist[0], attr_, 0, kPeerCount-1);
    BuildUpdateInfo(uinfo_slist[1], alt_attr_, 0, kPeerCount-1);

    // Build updates for even and odd routes as described above.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx%2], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the updates.
    SetOddPeerBlock(0, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyEvenUpdateCount(0, kPeerCount-1, COUNT_2);
    VerifyOddUpdateCount(0, kPeerCount-1, COUNT_1);
    VerifyEvenPeerBlock(0, kPeerCount-1, false);
    VerifyOddPeerBlock(0, kPeerCount-1, true);
    VerifyEvenPeerInSync(0, kPeerCount-1, true);
    VerifyOddPeerInSync(0, kPeerCount-1, false);
    VerifyMessageCount(3*kPeerCount/2);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        if (idx % 2 == 0) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            EXPECT_TRUE(rstate != NULL);
        } else {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            EXPECT_TRUE(rt_update != NULL);
        }
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,kRouteCount-1] queued to peers y=[0,vBlockPeerCount-1]
//           with attr A.
// Blocking: Peers y=[0,vBlockPeerCount-1] get blocked after STEP_1.
// Result:   Routes other than the default do not get sent to anyone since all
//           the target peers are blocked.
//           The TailDequeue loop terminated because there are no more updates.
TEST_F(RibOutUpdatesTest, DequeueCommonNoOverlap1) {

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfo for attr A containing all blocked peers.
        UpdateInfoSList uinfo_slist;
        PrependUpdateInfo(uinfo_slist, attrA_, 0, vBlockPeerCount-1);

        // Build updates for the default route and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vBlockPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyPeerBlock(vBlockPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(vBlockPeerCount, kPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        VerifyDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attrA_, 0, vBlockPeerCount-1);
            VerifyHistory(rt_update);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,kRouteCount-1] queued to peers y=[0,vBlockPeerCount-1]
//           with attr[x].
// Blocking: Peers y=[0,vBlockPeerCount-1] get blocked after STEP_1.
// Result:   Routes other than the default do not get sent to anyone since all
//           the target peers are blocked.
//           The TailDequeue loop terminated because there are no more updates.
TEST_F(RibOutUpdatesTest, DequeueCommonNoOverlap2) {
    ASSERT_TRUE(kAttrCount >= kRouteCount);

    // Loop for different number of blocked peers.
    for (int vBlockPeerCount = 1; vBlockPeerCount < kPeerCount;
            vBlockPeerCount++) {
        BGP_DEBUG_UT("vBlockPeerCount = " << vBlockPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build UpdateInfos for each attr index with all blocked peers.
        UpdateInfoSList uinfo_slist[kAttrCount];
        for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
            PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                    0, vBlockPeerCount-1);
        }

        // Build updates for the default route and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the updates.
        SetPeerBlock(0, vBlockPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, vBlockPeerCount-1, true);
        VerifyPeerBlock(vBlockPeerCount, kPeerCount-1, false);
        VerifyPeerInSync(vBlockPeerCount, kPeerCount-1, true);
        VerifyMessageCount(1);

        // Verify DB State for the routes.
        VerifyDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attr_[idx], 0, vBlockPeerCount-1);
            VerifyHistory(rt_update);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Routes x=[0,vRouteCount-1] enqueued to all peers, attr A.
// Blocking: None.
// Action:   Attempt peer dequeue for all peers.
// Result:   Nothing gets dequeued for any peer since they are all on
//           the tail marker.
TEST_F(RibOutUpdatesTest, PeerDequeueNoop1) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Build updates for all routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Try to dequeue the updates for all peers.
    UpdateAllPeers();

    // Verify update counts and blocked state.
    VerifyUpdateCount(0, kPeerCount-1, COUNT_0);
    VerifyPeerBlock(0, kPeerCount-1, false);
    VerifyPeerInSync(0, kPeerCount-1, true);
    VerifyMessageCount(0);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attrA_, 0, kPeerCount-1);
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,vRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peers x=[1,kPeerCount-1] block after STEP_1.
// Prep:     Tail dequeue.  This gets peers x=[1,kPeerCount-1] into blocked
//           state and split from the tail marker, which contains peer 0 and
//           is at the end of the queue.
// Action:   Attempt peer dequeue for all peers.
// Result:   Nothing gets dequeued for any peer since they are all blocked.
TEST_F(RibOutUpdatesTest, PeerDequeueNoop2) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    // Build updates for default and all other routes.
    EnqueueDefaultRoute();
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Dequeue the tail marker.
    SetPeerBlock(1, kPeerCount-1, STEP_1);
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyDefaultRoute();
    VerifyUpdateCount(0, COUNT_2);
    VerifyUpdateCount(1, kPeerCount-1, COUNT_1);
    VerifyPeerBlock(0, false);
    VerifyPeerBlock(1, kPeerCount-1, true);
    VerifyMessageCount(2);

    // Attempt peer dequeue for all peers.
    UpdateAllPeers();
    VerifyMessageCount(2);

    // Verify DB State for the routes.
    for (int idx = 0; idx < kRouteCount; idx++) {
        RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
        VerifyUpdates(rt_update, attrA_, 1, kPeerCount-1);
        VerifyHistory(rt_update, attrA_, 0, 0);
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,vRouteCount-1] enqueued to all peers, attr A.
// Blocking: Peers x=[1,kPeerCount-1] block after STEP_1.
// Prep:     Tail dequeue.  This gets peers x=[1,kPeerCount-1] into blocked
//           state and split from the tail marker, which contains peer 0 and
//           is at the end of the queue.
// Action:   Unblock peers x=[1,vReadyPeerCount].
//           Attempt peer dequeue for peer 1.
// Result:   All routes should get advertised to peers x=[1,vReadyPeerCount]
//           while the rest of the peers should get split.  All ready peers
//           should get merged into the tail marker.
TEST_F(RibOutUpdatesTest, PeerDequeueReady1) {

    // Build UpdateInfo for attr A with all peers.
    UpdateInfoSList uinfo_slist;
    PrependUpdateInfo(uinfo_slist, attrA_, 0, kPeerCount-1);

    for (int vReadyPeerCount = 1; vReadyPeerCount < kPeerCount-1;
            vReadyPeerCount++) {
        BGP_DEBUG_UT("vReadyPeerCount = " << vReadyPeerCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for default and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist, temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the tail marker.
        SetPeerBlock(1, kPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyDefaultRoute();
        VerifyUpdateCount(0, COUNT_2);
        VerifyUpdateCount(1, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, false);
        VerifyPeerBlock(1, kPeerCount-1, true);
        VerifyMessageCount(2);

        // Unblock the vReadyPeerCount peers starting at index 1.
        SetPeerUnblockNow(1, vReadyPeerCount);
        VerifyPeerBlock(0, vReadyPeerCount, false);
        UpdatePeer(peers_[1]);

        // Verify update counts and blocked state after peer dequeue.
        VerifyUpdateCount(1, vReadyPeerCount, COUNT_2);
        VerifyPeerBlock(1, vReadyPeerCount, false);
        VerifyMessageCount(3);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attrA_, vReadyPeerCount+1, kPeerCount-1);
            VerifyHistory(rt_update, attrA_, 0, vReadyPeerCount);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,vRouteCount-1] enqueued to all peers, attr x.
// Blocking: Peers x=[1,kPeerCount-1] block after STEP_1.
//           Peer 0 blocks after step vRouteCount + 1.
// Prep:     Tail dequeue.  The tail marker will contain peer 0 and is after
//           route vRouteCount-1.
// Action:   Unblock peers x=[1,kPeerCount-1].
//           Attempt peer dequeue for peer 1.
// Result:   Routes x=[0,vRouteCount-1] get sent to peers y=[1,kPeerCount-1].
//           All the peers then merge with the tail marker.
TEST_F(RibOutUpdatesTest, PeerDequeueTailMerge1) {

    // Build UpdateInfos for each attr index with all blocked peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                0, kPeerCount-1);
    }

    for (int vRouteCount = 1; vRouteCount <= kRouteCount-1;
            vRouteCount++) {
        BGP_DEBUG_UT("vRouteCount = " << vRouteCount);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for default and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the tail marker.
        SetPeerBlock(0, (Step) (vRouteCount + 1));
        SetPeerBlock(1, kPeerCount-1, STEP_1);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, (Count) (vRouteCount + 1));
        VerifyUpdateCount(1, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, kPeerCount-1, true);
        VerifyMessageCount(vRouteCount+1);

        // Unblock the vReadyPeerCount peers starting at index 1.
        SetPeerUnblockNow(1, kPeerCount-1);
        UpdatePeer(peers_[1]);

        // Verify update counts and blocked state after peer dequeue.
        VerifyUpdateCount(1, kPeerCount-1, (Count) (vRouteCount + 1));
        VerifyMessageCount(2*vRouteCount+1);

        // Verify DB State for the routes.
        for (int idx = 0; idx < vRouteCount; idx++) {
            RouteState *rstate = ExpectRouteState(routes_[idx]);
            VerifyHistory(rstate, attr_[idx], 0, kPeerCount-1);
        }
        for (int idx = vRouteCount; idx < kRouteCount; idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
            VerifyUpdates(rt_update, attr_[idx], 0, kPeerCount-1);
            VerifyHistory(rt_update);
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,kRouteCount-1] enqueued to all peers, attr x.
// Blocking: Peers x=[1,kPeerCount-1] block after STEP_1.
//           Peers x=[1,kPeerCount-1] block after STEP_n, n=[2,kRouteCount].
// Prep:     Tail dequeue.  The tail marker will contain peer 0 and is at the
//           end of the queue.
// Action:   Unblock peers x=[1,kPeerCount-1].
//           Attempt peer dequeue for peer 1.
// Result:   Routes x=[0,step-2] get sent to peers y=[1,kPeerCount-1].
//           The peers get blocked before they can reach the tail marker.
TEST_F(RibOutUpdatesTest, PeerDequeueAllBlock1) {

    // Build UpdateInfos for each attr index with all blocked peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                0, kPeerCount-1);
    }

    for (Step step = STEP_2; step <= (Step) kRouteCount;
            step = (Step) (step + 1)) {
        BGP_DEBUG_UT("Step = " << step);
        VerifyPeerInSync(0, kPeerCount-1, true);

        // Build updates for default and all other routes.
        EnqueueDefaultRoute();
        for (int idx = 0; idx < kRouteCount; idx++) {
            UpdateInfoSList temp_uinfo_slist;
            CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
            BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
        }

        // Dequeue the tail marker.
        SetPeerBlock(1, kPeerCount-1, STEP_1);
        SetPeerBlock(1, kPeerCount-1, step);
        UpdateRibOut();

        // Verify update counts and blocked state.
        VerifyUpdateCount(0, (Count) (kRouteCount + 1));
        VerifyUpdateCount(1, kPeerCount-1, COUNT_1);
        VerifyPeerBlock(0, false);
        VerifyPeerBlock(1, kPeerCount-1, true);
        VerifyMessageCount(kRouteCount+1);

        // Attempt peer dequeue for peer 1.
        SetPeerUnblockNow(1, kPeerCount-1);
        UpdatePeer(peers_[1]);

        // Verify update counts and blocked state after peer dequeue.
        VerifyUpdateCount(1, kPeerCount-1, (Count) step);
        VerifyMessageCount(kRouteCount+step);

        // Verify DB State for the routes.
        for (int idx = 0; idx < kRouteCount; idx++) {
            if (idx <= step-2) {
                RouteState *rstate = ExpectRouteState(routes_[idx]);
                VerifyHistory(rstate, attr_[idx], 0, kPeerCount-1);
            } else {
                RouteUpdate *rt_update = ExpectRouteUpdate(routes_[idx]);
                VerifyUpdates(rt_update, attr_[idx], 1, kPeerCount-1);
                VerifyHistory(rt_update, attr_[idx], 0, 0);
            }
        }

        // Clean up before the next iteration.
        DrainAndDeleteDBState();
    }
}

// Routes:   Default route enqueued to all peers.
//           Routes x=[0,kRouteCount-1] enqueued to all peers, attr x.
// Blocking: Peer x=kPeerCount-1 does not block.
//           Peer x=[0,kPeerCount-2] blocks after steps s=[x+1,kRouteRount].
// Prep:     Tail dequeue.  The tail marker will contain peer kPeerCount-1 and
//           is at the end of the queue.
// Action:   Unblock peers x=[1,kPeerCount-1].
//           Attempt peer dequeue for peer 0.
// Result:   The next peer should get merged with the marker for peer 0 after
//           each iteration.
TEST_F(RibOutUpdatesTest, PeerDequeueSplitMerge1) {

    // Build UpdateInfos for each attr index with all blocked peers.
    UpdateInfoSList uinfo_slist[kAttrCount];
    for (int attr_idx = 0; attr_idx < kAttrCount; attr_idx++) {
        PrependUpdateInfo(uinfo_slist[attr_idx], attr_[attr_idx],
                0, kPeerCount-1);
    }

    // Build updates for default and all other routes.
    EnqueueDefaultRoute();
    for (int idx = 0; idx < kRouteCount; idx++) {
        UpdateInfoSList temp_uinfo_slist;
        CloneUpdateInfo(uinfo_slist[idx], temp_uinfo_slist);
        BuildRouteUpdate(routes_[idx], temp_uinfo_slist);
    }

    // Setup the blocking steps as described above.
    for (int idx = 0; idx < kPeerCount-1; idx++) {
        for (Step step = (Step) (idx + 1); step <= (Step) kRouteCount;
                step = (Step) (step + 1)) {
            SetPeerBlock(idx, step);
        }
    }

    // Dequeue the tail marker.
    UpdateRibOut();

    // Verify update counts and blocked state.
    VerifyUpdateCount(kPeerCount-1, (Count) (kRouteCount + 1));
    for (int idx = 0; idx < kPeerCount-1; idx++) {
        VerifyUpdateCount(idx, (Count) (idx + 1));
    }
    VerifyPeerBlock(kPeerCount-1, false);
    VerifyPeerBlock(0, kPeerCount-2, true);
    VerifyMessageCount(kRouteCount+1);

    // Unblock all peers and peer dequeue for peer 0 repeatedly.  This will
    // cause the next peer to get merged with the marker for peer 0 at every
    // step.
    for (int idx = 0; idx < kPeerCount-1; idx++) {
        SetPeerUnblockNow(0, kPeerCount-1);
        UpdatePeer(peers_[0]);

        // Verify update counts and blocked state.
        VerifyPeerBlock(0, idx, true);
        VerifyMessageCount(kRouteCount+1+idx+1);

        // Verify DB State for the routes.
        for (int rt_idx = 0; rt_idx <= idx; rt_idx++) {
            RouteState *rstate = ExpectRouteState(routes_[rt_idx]);
            VerifyHistory(rstate, attr_[rt_idx], 0, kPeerCount-1);
        }
        for (int rt_idx = idx+1; rt_idx < kPeerCount-1; rt_idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[rt_idx]);
            VerifyUpdates(rt_update, attr_[rt_idx], 0, rt_idx);
        }
        for (int rt_idx = kPeerCount-1; rt_idx < kRouteCount; rt_idx++) {
            RouteUpdate *rt_update = ExpectRouteUpdate(routes_[rt_idx]);
            VerifyUpdates(rt_update, attr_[rt_idx], 0, kPeerCount-2);
            VerifyHistory(rt_update, attr_[rt_idx], kPeerCount-1, kPeerCount-1);
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
