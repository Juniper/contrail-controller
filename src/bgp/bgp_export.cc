/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_export.h"

#include <boost/bind.hpp>

#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_update.h"
#include "bgp/bgp_update_monitor.h"
#include "bgp/scheduling_group.h"
#include "db/db_table_partition.h"

BgpExport::BgpExport(RibOut *ribout)
    : ribout_(ribout) {
}

//
// Check if the desired state as expressed by the UpdateInfoSList is exactly
// the same as the state already stored in the RouteUpdate.
//
// Return true if we have a duplicate, false otherwise.
//
static bool IsDuplicate(const RouteUpdate *rt_update,
        const UpdateInfoSList *uinfo_slist) {
    return rt_update->CompareUpdateInfo(*uinfo_slist);
}

//
// Export Processing.
// 1. Calculate the desired attributes (UpdateInfo list) via BgpTable::Export.
// 2. Dequeue the existing update if present.
//    a) If the current update is the same as desired update then the operation
//    is a NOP.
//    b) If there is a current update, then the code must obtain a lock on it
//    in order to make sure that the output process is not currently reading
//    and modifying its state.
// 3. Calculate the delta between previous state and new state.
// 4. Enqueue a new update at tail.
//
void BgpExport::Export(DBTablePartBase *root, DBEntryBase *db_entry) {
    RouteUpdate *rt_update;
    UpdateInfoSList uinfo_slist;

    // Calculate attributes by running through export policy.
    BgpRoute *route = static_cast<BgpRoute *>(db_entry);
    bool reach = false;
    if (!db_entry->IsDeleted() && !ribout_->PeerSet().empty()) {
        reach = ribout_->table()->Export(ribout_, route, ribout_->PeerSet(),
                uinfo_slist);
    }
    assert(!reach || !uinfo_slist->empty());

    // Find and dequeue any existing DBState.
    bool duplicate = false;
    RibOutUpdates *updates = ribout_->updates();
    RibUpdateMonitor *monitor = updates->monitor();
    DBState *dbstate = monitor->GetDBStateAndDequeue(db_entry,
            boost::bind(IsDuplicate, _1, &uinfo_slist),
            &duplicate);

    // Handle the DBState as appropriate.
    if (dbstate == NULL) {

        // A NULL DBState implies that we either have no previous state or
        // that the previously scheduled updates are duplicates of what we
        // want to send now.

        // Nothing to do if we are looking at duplicates.
        if (duplicate)
            return;

        // If we have no previous state and the route is not reachable we
        // are done.
        if (!reach)
            return;

        // We have no previous state and the route is reachable.  Need to
        // schedule a new update.
        rt_update = new RouteUpdate(route, RibOutUpdates::QUPDATE);

    } else {

        // The DBState in the DBEntryBase must be the same as what we found.
        // This is a paranoid check to make sure that GetDBStateAndDequeue
        // did not forget to update the DBState when handing a RouteUpdate
        // from the QBULK queue or when handling an UpdateList.
        const DBState *entry_db_state =
            db_entry->GetState(root->parent(), ribout_->listener_id());
        assert(entry_db_state == dbstate);

        // We got here because we have previous state, either a RouteState
        // or a RouteUpdate. Note that it cannot be an UpdateList since we
        // get rid of UpdateLists in GetDBStateAndDequeue and return either
        // the RouteUpdate for QUPDATE or a new RouteState with the history.
        assert(dynamic_cast<UpdateList *>(dbstate) == NULL);

        rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        if (rt_update == NULL)  {

            // Previous state is not a RouteUpdate, must be a RouteState.
            RouteState *rstate = static_cast<RouteState *>(dbstate);

            // Bail if the new state that we want to schedule is identical
            // to what we have previously advertised. This can happen when
            // there are back to back changes to a route such that it goes
            // from state A to B and then back to A but the Export routine
            // never saw state B.
            if (rstate->CompareUpdateInfo(uinfo_slist))
                return;

            // We need a new RouteUpdate to advertise the new state and/or
            // withdraw part or all of the previous state. Move history to
            // a new RouteUpdate and get rid of the RouteState.
            rt_update = new RouteUpdate(route, RibOutUpdates::QUPDATE);
            rstate->MoveHistory(rt_update);
            delete rstate;
            dbstate = NULL;
            rstate = NULL;

        } else {

            // The RouteUpdate must be for QUPDATE. Any RouteUpdate that got
            // dequeued from QBULK is also converted to be for QUPDATE.
            assert(rt_update->queue_id() == RibOutUpdates::QUPDATE);

            // The previous state is a RouteUpdate. Get rid of any scheduled
            // UpdateInfos since we have fresh new UpdateInfos to schedule.
            // Note that the history information is still in the RouteUpdate.
            rt_update->ClearUpdateInfo();

            // If the history is empty and the route is not reachable, there
            // is nothing to withdraw. Get rid of the RouteUpdate and return.
            if (rt_update->History()->empty() && !reach) {
                db_entry->ClearState(root->parent(), ribout_->listener_id());
                delete rt_update;
                return;
            }
        }

        // The new UpdateInfos that we want to schedule may not cover all the
        // peers that we advertised state to previously. May need to generate
        // negative state for such peers based on the history in RouteUpdate.
        rt_update->BuildNegativeUpdateInfo(uinfo_slist);

        // At this point we have a RouteUpdate with history information and
        // an UpdateInfoSList that contains the state that we are going to
        // schedule. There may be some commonality between them. We want to
        // trim redundant state in the UpdateInfoSList i.e. if some of it's
        // the same as what's already in the history.
        rt_update->TrimRedundantUpdateInfo(uinfo_slist);

        // If there are no more UpdateInfos in the list, move the history to
        // a new RouteState and get rid of the RouteUpdate.  This can happen
        // if the route goes from state A to B, the Export routine sees B and
        // schedules updates but the state goes back to A before the updates
        // can be sent to anyone.
        if (uinfo_slist->empty()) {
            RouteState *rstate = new RouteState();
            rt_update->MoveHistory(rstate);
            db_entry->SetState(root->parent(), ribout_->listener_id(), rstate);
            delete rt_update;
            return;
        }
    }

    // Associate the new UpdateInfos we want to send with the RouteUpdate
    // and enqueue the RouteUpdate.
    assert(!uinfo_slist->empty());
    rt_update->SetUpdateInfo(uinfo_slist);
    updates->Enqueue(db_entry, rt_update);
}

//
// Join Processing.
// 1. Detect if we have've already sent or scheduled updates to the bitset of
//    peers for the QUPDATE queue.
// 2. Calculate the desired attributes (UpdateInfo list) via BgpTable::Export.
// 3. Create a new RouteUpdate for the QBULK queue and merge it with existing
//    state.
//
bool BgpExport::Join(DBTablePartBase *root, const RibPeerSet &mjoin,
        DBEntryBase *db_entry) {
    RibOutUpdates *updates = ribout_->updates();
    RibUpdateMonitor *monitor = updates->monitor();

    // Bail if the route is already deleted.
    if (db_entry->IsDeleted())
        return true;

    // There may have been a route change before the walk gets to this route.
    // Trim mjoin by resetting mcurrent and mscheduled to prevent enqueueing
    // duplicate updates.
    //
    // TBD:: tweak this further to handle route refresh.
    RibPeerSet mcurrent, mscheduled;
    monitor->GetPeerSetCurrentAndScheduled(db_entry, RibOutUpdates::QUPDATE,
            &mcurrent, &mscheduled);
    RibPeerSet mjoin_subset = mjoin;
    mjoin_subset.Reset(mcurrent);
    mjoin_subset.Reset(mscheduled);
    if (mjoin_subset.empty()) {
        return true;
    }

    // Run export policy to generate the update infos.
    BgpRoute *route = static_cast<BgpRoute *>(db_entry);
    UpdateInfoSList uinfo_slist;
    bool reach = ribout_->table()->Export(ribout_, route, mjoin_subset,
            uinfo_slist);
    assert(!reach || !uinfo_slist->empty());
    if (!reach) {
        return true;
    }

    // Create a new update.
    RouteUpdate *rt_update = new RouteUpdate(route, RibOutUpdates::QBULK);
    rt_update->SetUpdateInfo(uinfo_slist);

    // Merge the update into the BULK queue. If there is an entry present
    // already then the update info is merged.  Kick the scheduling group
    // machinery if needed.
    bool need_tail_dequeue = monitor->MergeUpdate(db_entry, rt_update);
    if (need_tail_dequeue) {
        SchedulingGroup *group = ribout_->GetSchedulingGroup();
        assert(group != NULL);
        group->RibOutActive(ribout_, RibOutUpdates::QBULK);
    }

    return true;
}

//
// Leave Processing.
// 1. Detect if there's no history or scheduled updates for the bitset of
//    peers.
// 2. Remove any history or scheduled updates.
//
bool BgpExport::Leave(DBTablePartBase *root, const RibPeerSet &mleave,
        DBEntryBase *db_entry) {
    RibOutUpdates *updates = ribout_->updates();
    RibUpdateMonitor *monitor = updates->monitor();

    // Nothing to do if there are no current or scheduled updates for any
    // peers in mleave.
    RibPeerSet mcurrent, mscheduled;
    monitor->GetPeerSetCurrentAndScheduled(db_entry, RibOutUpdates::QCOUNT,
            &mcurrent, &mscheduled);
    RibPeerSet munion, mleave_subset;
    munion.Set(mcurrent);
    munion.Set(mscheduled);
    mleave_subset.BuildIntersection(mleave, munion);
    if (mleave_subset.empty()) {
        return true;
    }

    // Cancel scheduled updates for the route and/or remove AdvertiseInfo
    // for current advertised state.
    monitor->ClearPeerSetCurrentAndScheduled(db_entry, mleave_subset);

    return true;
}
