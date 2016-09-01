/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_update_monitor.h"

#include "base/task_annotations.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_update_queue.h"

RouteUpdatePtr::RouteUpdatePtr(RouteUpdate *rt_update) : rt_update_(rt_update) {
}

RouteUpdatePtr::~RouteUpdatePtr() {
}

RibUpdateMonitor::RibUpdateMonitor(RibOut *ribout, QueueVec *queue_vec)
    : ribout_(ribout), queue_vec_(queue_vec) {
}

//
// Helper routine for GetRouteStateAndDequeue to handle a RouteUpdate. Do
// nothing if it's a duplicate.
//
// Return a pointer to the DBState and update duplicate as appropriate.
//
DBState *RibUpdateMonitor::GetRouteUpdateAndDequeue(DBEntryBase *db_entry,
        RouteUpdate *rt_update, UpdateCmp cmp, bool *duplicate) {
    CHECK_CONCURRENCY("db::DBTable");

    // Compare desired state with current update. If the desired state
    // is a NOP just return without modifying the queue.
    if (rt_update->queue_id() == RibOutUpdates::QUPDATE) {
        *duplicate = cmp(rt_update);
        if (*duplicate) {
            return NULL;
        }
    }

    // Set the state on the DBEntryBase and remove the RouteUpdate from
    // the queue. Change the queue to QUPDATE since that's where we will
    // re-enqueue the RouteUpdate, if we do it.
    db_entry->SetState(ribout_->table(), ribout_->listener_id(), rt_update);
    UpdateQueue *queue = queue_vec_->at(rt_update->queue_id());
    queue->Dequeue(rt_update);
    rt_update->set_queue_id(RibOutUpdates::QUPDATE);
    return rt_update;
}

//
// Helper routine for GetRouteStateAndDequeue to handle a UpdateList.
//
// Since an update operation overrides any join/refresh state, get rid of
// RouteUpdates that are not for QUPDATE.
//
// Note that we should NOT check for duplicate state in QUPDATE and bail.
// Doing so could result in a failure to remove pending RouteUpdates from
// QBULK which are no longer required.
//
// Return a pointer to the RouteUpdate for QUPDATE if it exists, else
// Return a pointer to new RouteState if the UpdateList has history, else
// Return NULL.
//
DBState *RibUpdateMonitor::GetUpdateListAndDequeue(DBEntryBase *db_entry,
        UpdateList *uplist) {
    CHECK_CONCURRENCY("db::DBTable");

    DBState *dbstate;
    RouteUpdate *rt_update = uplist->FindUpdate(RibOutUpdates::QUPDATE);
    if (rt_update) {
        // There's a RouteUpdate for QUPDATE. Remove it from the UpdateList
        // and move the history to it.  The entry will be reused as if that
        // was the DBState instead of the UpdateList.
        uplist->RemoveUpdate(rt_update);
        queue_vec_->at(rt_update->queue_id())->Dequeue(rt_update);
        uplist->MoveHistory(rt_update);
        dbstate = rt_update;
    } else if (!uplist->History()->empty()) {
        // There's no RouteUpdate for QUPDATE, but the history is not empty.
        // Move the history to a new RouteState.
        RouteState *rstate = new RouteState();
        uplist->MoveHistory(rstate);
        dbstate = rstate;
    } else {
        // There's no RouteUpdate for QUPDATE and the history is empty. We
        // can simply pretend that there was no DBState after we get rid
        // of the UpdateList.
        dbstate = NULL;
    }

    // Get rid of all other RouteUpdates and the UpdateList itself.  Note
    // that the RouteUpdate for QUPDATE, if any, has already been removed
    // from the UpdateList at this point.
    UpdateList::List *list = uplist->GetList();
    for (UpdateList::List::iterator iter = list->begin();
         iter != list->end(); iter++) {
        RouteUpdate *temp_rt_update = *iter;
        queue_vec_->at(temp_rt_update->queue_id())->Dequeue(temp_rt_update);
        delete temp_rt_update;
    }

    // Update the DBState and return it.
    if (dbstate) {
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), dbstate);
    } else {
        db_entry->ClearState(ribout_->table(), ribout_->listener_id());
    }
    return dbstate;
}

//
// Get the DBState for the DBEntryBase and dequeue it from an UpdateQueue
// if required.  The DBState could be a RouteState, a RouteUpdate or an
// UpdateList.  If it's a RouteState, it won't be on an UpdateQueue.  If
// it's a RouteUpdate or an UpdateList, see if it's a duplicate i.e. the
// desired state is the same as the currently advertised state.
//
// This routine is used by the export module to obtain exclusive access to
// the RouteUpdate or UpdateList. The access is exclusive by definition if
// there's no DBState or it's a RouteState.
//
// Return a pointer to the DBState and update duplicate as appropriate.
//
DBState *RibUpdateMonitor::GetDBStateAndDequeue(DBEntryBase *db_entry,
        UpdateCmp cmp, bool *duplicate) {
    CHECK_CONCURRENCY("db::DBTable");

    *duplicate = false;

    // Don't need to bother going through the monitor if there's no DBState.
    DBState *dbstate =
        db_entry->GetState(ribout_->table(), ribout_->listener_id());
    if (dbstate == NULL) {
        return dbstate;
    }

    // Go through the monitor and handle the DBState as appropriate.  Note
    // that we still need to check for the DBState being NULL as things may
    // have changed after the check made above.
    while (true) {
        // Get the DBState; bail if there's no existing state.
        DBState *dbstate =
            db_entry->GetState(ribout_->table(), ribout_->listener_id());
        if (dbstate == NULL) {
            return dbstate;
        }

        // Return if it's a RouteState.
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        if (rstate != NULL) {
            return rstate;
        }

        // Handle the case where it's a RouteUpdate.
        RouteUpdate *rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        if (rt_update != NULL) {
            return GetRouteUpdateAndDequeue(db_entry, rt_update, cmp,
                    duplicate);
        }

        // Handle the case where it's a UpdateList.
        UpdateList *uplist = dynamic_cast<UpdateList *>(dbstate);
        DBState *db_state;
        assert(uplist);
        db_state = GetUpdateListAndDequeue(db_entry, uplist);
        delete uplist;
        return db_state;
    }

    assert(false);
    return NULL;
}

//
// Helper routine for GetPeerSetCurrentAndScheduled to handle a RouteState.
// Go through all the AdvertiseInfo to build the bitset.
//
static void RouteStateCurrent(const RouteState *rstate,
        RibPeerSet *mcurrent) {
    CHECK_CONCURRENCY("db::DBTable");

    const AdvertiseSList &adv_slist = rstate->Advertised();
    for (AdvertiseSList::List::const_iterator iter = adv_slist->begin();
         iter != adv_slist->end(); ++iter) {
        mcurrent->Set(iter->bitset);
    }
}

//
// Helper routine for GetPeerSetCurrentAndScheduled to handle a RouteUpdate.
// Go through all the AdvertiseInfo and UpdateInfo to build the bitset.
//
// Do not consider the UpdateInfos if the given queue_id does not match the
// one in the RouteUpdate.  Note that QCOUNT is considered to be a wildcard.
//
static void RouteUpdateCurrentAndScheduled(const RouteUpdate *rt_update,
        int queue_id, RibPeerSet *mcurrent, RibPeerSet *mscheduled) {
    CHECK_CONCURRENCY("db::DBTable");

    const AdvertiseSList &adv_slist = rt_update->History();
    for (AdvertiseSList::List::const_iterator iter = adv_slist->begin();
         iter != adv_slist->end(); ++iter) {
        mcurrent->Set(iter->bitset);
    }

    if (queue_id != RibOutUpdates::QCOUNT && queue_id != rt_update->queue_id())
        return;

    const UpdateInfoSList &uinfo_slist = rt_update->Updates();
    for (UpdateInfoSList::List::const_iterator iter = uinfo_slist->begin();
         iter != uinfo_slist->end(); ++iter) {
        mscheduled->Set(iter->target);
    }
}

//
// Helper routine for GetPeerSetCurrentAndScheduled to handle a UpdateList.
// Go through all AdvertiseInfo in the UpdateList and then each RouteUpdate
// in the UpdateList to build the bitset.
//
static void UpdateListCurrentAndScheduled(const UpdateList *uplist,
        int queue_id, RibPeerSet *mcurrent, RibPeerSet *mscheduled) {
    CHECK_CONCURRENCY("db::DBTable");

    const AdvertiseSList &adv_slist = uplist->History();
    for (AdvertiseSList::List::const_iterator iter = adv_slist->begin();
         iter != adv_slist->end(); ++iter) {
        mcurrent->Set(iter->bitset);
    }

    const UpdateList::List *list = uplist->GetList();
    for (UpdateList::List::const_iterator iter = list->begin();
         iter != list->end(); ++iter) {
        RouteUpdateCurrentAndScheduled(*iter, queue_id, mcurrent, mscheduled);
    }
}

//
// Build the RibPeerSet of peers that are currently advertising the given
// DBEntryBase or are scheduled to advertise it. The mcurrent parameter is
// filled with the contents of the advertised bitmask (history) while the
// mscheduled parameter is filled with the bitmask of updates in the queue.
//
// Return false if there's no associated DBState or if it's a RouteState
// i.e. there's no pending state to be advertised.
//
bool RibUpdateMonitor::GetPeerSetCurrentAndScheduled(DBEntryBase *db_entry,
        int queue_id, RibPeerSet *mcurrent, RibPeerSet *mscheduled) {
    CHECK_CONCURRENCY("db::DBTable");

    // Don't need to bother going through the monitor if there's no DBState.
    DBState *dbstate =
        db_entry->GetState(ribout_->table(), ribout_->listener_id());
    if (dbstate == NULL) {
        return false;
    }

    // Go through the monitor and handle the DBState as appropriate.  Note
    // that we still need to check for the DBState being NULL as things may
    // have changed after the check made above.
    while (true) {
        // Get the DBState; bail if there's no existing state.
        DBState *dbstate = db_entry->GetState(ribout_->table(),
                                         ribout_->listener_id());
        if (dbstate == NULL) {
            return false;
        }

        // Handle the case where it's a RouteState.
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        if (rstate != NULL) {
            RouteStateCurrent(rstate, mcurrent);
            return false;
        }

        // Handle the case where it's a RouteUpdate.
        RouteUpdate *rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        if (rt_update != NULL) {
            RouteUpdateCurrentAndScheduled(rt_update, queue_id,
                    mcurrent, mscheduled);
            break;
        }

        // Handle the case where it's a UpdateList.
        UpdateList *uplist = dynamic_cast<UpdateList *>(dbstate);
        if (uplist != NULL) {
            UpdateListCurrentAndScheduled(uplist, queue_id,
                    mcurrent, mscheduled);
            break;
        }

        // Unknown DBState.
        assert(false);
    }

    return true;
}

//
// Helper routine for MergeUpdate to handle case where the current DBState
// is a RouteState.
//
// Move the previous history from the RouteState to the RouteUpdate and
// enqueue it.
//
// Return true if the BgpUpdateSender needs to trigger a tail dequeue for
// the (RibOut, QueueId).
//
bool RibUpdateMonitor::RouteStateMergeUpdate(DBEntryBase *db_entry,
        RouteUpdate *rt_update, RouteState *rstate) {
    CHECK_CONCURRENCY("db::DBTable");

    rstate->MoveHistory(rt_update);
    delete rstate;
    return EnqueueUpdate(db_entry, rt_update);
}

//
// Helper routine for MergeUpdate to handle case where the current DBState
// is a RouteUpdate.
//
// If the new RouteUpdate is for the same queue as the current RouteUpdate,
// dequeue the current, merge in the UpdateInfo from the new and enqueue it
// again. Otherwise, build an UpdateList containing both RouteUpdates and
// enqueue the new RouteUpdate.
//
// Return true if the BgpUpdateSender needs to trigger a tail dequeue for
// the (RibOut, QueueId).
//
bool RibUpdateMonitor::RouteUpdateMergeUpdate(DBEntryBase *db_entry,
        RouteUpdate *rt_update, RouteUpdate *current_rt_update) {
    CHECK_CONCURRENCY("db::DBTable");

    if (current_rt_update->queue_id() == rt_update->queue_id()) {
        UpdateQueue *queue = queue_vec_->at(current_rt_update->queue_id());
        queue->Dequeue(current_rt_update);
        current_rt_update->MergeUpdateInfo(rt_update->Updates());
        assert(rt_update->Updates()->empty());
        delete rt_update;
        return EnqueueUpdate(db_entry, current_rt_update);
    } else {
        UpdateList *uplist = current_rt_update->MakeUpdateList();
        uplist->AddUpdate(rt_update);
        return EnqueueUpdate(db_entry, rt_update, uplist);
    }
}

//
// Helper routine for MergeUpdate to handle case where the current DBState
// is a UpdateList.
//
// Figure out if the UpdateList already has a RouteUpdate for this queue.
// If so, dequeue the existing RouteUpdate, merge the UpdateInfo from the
// new one and enqueue the existing RouteUpdate again. Otherwise, add the
// new RouteUpdate to the UpdateList and enqueue the RouteUpdate.
//
// Return true if the BgpUpdateSender needs to trigger a tail dequeue for
// the (RibOut, QueueId).
//
bool RibUpdateMonitor::UpdateListMergeUpdate(DBEntryBase *db_entry,
        RouteUpdate *rt_update, UpdateList *uplist) {
    CHECK_CONCURRENCY("db::DBTable");

    RouteUpdate *current_rt_update = uplist->FindUpdate(rt_update->queue_id());
    if (current_rt_update) {
        UpdateQueue *queue = queue_vec_->at(current_rt_update->queue_id());
        queue->Dequeue(current_rt_update);
        current_rt_update->MergeUpdateInfo(rt_update->Updates());
        assert(rt_update->Updates()->empty());
        delete rt_update;
        return EnqueueUpdate(db_entry, current_rt_update, uplist);
    } else {
        uplist->AddUpdate(rt_update);
        return EnqueueUpdate(db_entry, rt_update, uplist);
    }
}

//
// Take the desired state as represented by the RouteUpdate and merge it with
// any previously advertised state for the DBEntryBase. Note that the previous
// state could be for a different queue and/or other peers in the RibOut.
//
// Return true if the BgpUpdateSender needs to trigger a tail dequeue for
// the (RibOut, QueueId).
//
bool RibUpdateMonitor::MergeUpdate(DBEntryBase *db_entry,
        RouteUpdate *rt_update) {
    CHECK_CONCURRENCY("db::DBTable");

    // Go through the monitor and handle the DBState as necessary. Need
    // to use the monitor even if there's no DBState in order to protect
    // against race conditions which could result in an incorrect return
    // value.
    while (true) {
        DBState *dbstate =
            db_entry->GetState(ribout_->table(), ribout_->listener_id());

        // Handle the case where there's no DBState.  Simply add the new
        // RouteUpdate to the queue.
        if (dbstate == NULL) {
            return EnqueueUpdate(db_entry, rt_update);
        }

        // Handle the case where it's a RouteState.
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        if (rstate != NULL) {
            return RouteStateMergeUpdate(db_entry, rt_update, rstate);
        }

        // Handle the case where it's a RouteUpdate.
        RouteUpdate *current_rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        if (current_rt_update != NULL) {
            return RouteUpdateMergeUpdate(db_entry, rt_update,
                    current_rt_update);
        }

        // Handle the case where it's a UpdateList.
        UpdateList *uplist = dynamic_cast<UpdateList *>(dbstate);
        if (uplist != NULL) {
            return UpdateListMergeUpdate(db_entry, rt_update, uplist);
        }

        // Unknown DBState.
        assert(false);
    }

    assert(false);
    return false;
}

//
// Traipse through all the AdvertiseInfo elements in the list and clear the
// RibPeerSet. If the RibPeerSet in an element becomes empty, remove it from
// the list container and get rid of the element.
//
void RibUpdateMonitor::AdvertiseSListClearBits(AdvertiseSList &adv_slist,
        const RibPeerSet &clear) {
    CHECK_CONCURRENCY("db::DBTable");

    for (AdvertiseSList::List::iterator iter = adv_slist->begin();
         iter != adv_slist->end(); ) {
        iter->bitset.Reset(clear);
        if (iter->bitset.empty()) {
            iter = adv_slist->erase_and_dispose(iter, AdvertiseInfoDisposer());
        } else {
            iter++;
        }
    }
}

//
// Traipse through all the UpdateInfo elements in the list and clear the
// RibPeerSet. If the RibPeerSet in an element becomes empty, remove it
// from the set and list containers and get rid of the element.
//
void RibUpdateMonitor::UpdateInfoSListClearBits(UpdateInfoSList &uinfo_slist,
        const RibPeerSet &clear) {
    CHECK_CONCURRENCY("db::DBTable");

    for (UpdateInfoSList::List::iterator iter = uinfo_slist->begin();
         iter != uinfo_slist->end(); ) {
        iter->target.Reset(clear);
        if (iter->target.empty()) {
            RouteUpdate *rt_update = iter->update;
            UpdateQueue *queue = queue_vec_->at(rt_update->queue_id());
            queue->AttrDequeue(iter.operator->());
            iter = uinfo_slist->erase_and_dispose(iter, UpdateInfoDisposer());
        } else {
            iter++;
        }
    }
}

//
// Helper routine for ClearPeerSetCurrentAndScheduled to handle case where
// the current DBState is a RouteState.
//
// Clean up the AdvertiseInfos corresponding to the peers in RibPeerSet.
//
void RibUpdateMonitor::RouteStateClearPeerSet(DBEntryBase *db_entry,
        RouteState *rstate, const RibPeerSet &mleave) {
    CHECK_CONCURRENCY("db::DBTable");

    // Clear the bits for each element in the AdvertiseSList.
    AdvertiseSListClearBits(rstate->Advertised(), mleave);

    // Get rid of the RouteState itself if it's empty.
    if (rstate->Advertised()->empty()) {
        db_entry->ClearState(ribout_->table(), ribout_->listener_id());
        delete rstate;
    }
}

//
// Helper routine for ClearPeerSetCurrentAndScheduled to handle case where
// the current DBState is a RouteUpdate.
//
// Clean up the UpdateInfos corresponding to the peers in RibPeerSet.
//
// Return true if rt_update has to be deleted, false otherwise
//
bool RibUpdateMonitor::RouteUpdateClearPeerSet(DBEntryBase *db_entry,
            RouteUpdate *rt_update, const RibPeerSet &mleave) {
    CHECK_CONCURRENCY("db::DBTable");

    // Clear the bits for each element in the AdvertiseSList.
    AdvertiseSListClearBits(rt_update->History(), mleave);

    // Clear the bits for each element in the UpdateInfoSList.
    UpdateInfoSListClearBits(rt_update->Updates(), mleave);

    // Update the DBstate for the DBentry as appropriate.
    if (!rt_update->Updates()->empty()) {
        // There are more scheduled updates, do nothing.
        return false;
    }

    if (!rt_update->History()->empty()) {
        // No more scheduled updates but there are current updates. Dequeue
        // the RouteUpdate, move the history to a new RouteState and get rid
        // of the RouteUpdate.
        DequeueUpdate(rt_update);
        RouteState *rstate = new RouteState;
        rt_update->MoveHistory(rstate);
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), rstate);
    } else {
        // No more scheduled or current updates.  Dequeue the RouteUpdate,
        // clear the state on the DBEntry and get rid of the RouteUpdate.
        DequeueUpdate(rt_update);
        db_entry->ClearState(ribout_->table(), ribout_->listener_id());
    }

    return true;
}

//
// Helper routine for ClearPeerSetCurrentAndScheduled to handle case where
// the current DBState is a UpdateList.
//
// Return true if uplist has to be deleted, false otherwise
//
bool RibUpdateMonitor::UpdateListClearPeerSet(DBEntryBase *db_entry,
        UpdateList *uplist, const RibPeerSet &mleave) {
    CHECK_CONCURRENCY("db::DBTable");

    UpdateList::List *list = uplist->GetList();

    // Clear the bits for each element in the AdvertiseSList.
    AdvertiseSListClearBits(uplist->History(), mleave);

    // Clear the bits for each element in the UpdateInfoSList for each
    // RouteUpdate. If a RouteUpdate becomes empty as a result, get rid
    // of it.
    for (UpdateList::List::iterator iter = list->begin();
         iter != list->end(); ) {
        RouteUpdate *rt_update = *iter++;
        UpdateInfoSListClearBits(rt_update->Updates(), mleave);
        if (rt_update->Updates()->empty()) {
            uplist->RemoveUpdate(rt_update);
            DequeueUpdate(rt_update);
            delete rt_update;
        }
    }

    // Update the DBstate for the DBentry as appropriate.
    if (list->size() > 1) {
        // There's multiple RouteUpdates on the UpdateList, do nothing.
        return false;
    } else if (list->size() == 1) {
        // There's exactly 1 RouteUpdate on the UpdateList. Downgrade
        // the UpdateList to a RouteUpdate and get rid of UpdateList.
        // Note that the history will be moved to the RouteUpdate as
        // part of the downgrade.
        RouteUpdate *rt_update = uplist->MakeRouteUpdate();
        assert(rt_update);
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), rt_update);
    } else if (!uplist->History()->empty()) {
        // There's no RouteUpdates on the UpdateList, but the history
        // is not empty.  Move the history to a new RouteState and get
        // rid of the UpdateList.
        RouteState *rstate = new RouteState;
        uplist->MoveHistory(rstate);
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), rstate);
    } else {
        // There's no RouteUpdates on the UpdateList and the history is
        // empty. Clear state on the DBEntry and get rid of UpdateList.
        db_entry->ClearState(ribout_->table(), ribout_->listener_id());
    }

    return true;
}

//
// Cancel all scheduled updates and clean up AdvertiseInfo corresponding
// to any current updates for the peers in the RibPeerSet.
//
void RibUpdateMonitor::ClearPeerSetCurrentAndScheduled(DBEntryBase *db_entry,
        const RibPeerSet &mleave) {
    CHECK_CONCURRENCY("db::DBTable");

    // Don't need to bother going through the monitor if there's no DBState.
    DBState *dbstate =
        db_entry->GetState(ribout_->table(), ribout_->listener_id());
    if (dbstate == NULL) {
        return;
    }

    // Go through the monitor and handle the DBState as appropriate.  Note
    // that we still need to check for the DBState being NULL as things may
    // have changed after the check made above.
    while (true) {
        DBState *dbstate =
            db_entry->GetState(ribout_->table(), ribout_->listener_id());

        // Handle the case where there's no DBState.
        if (dbstate == NULL) {
            return;
        }

        // Handle the case where it's a RouteState.
        RouteState *rstate = dynamic_cast<RouteState *>(dbstate);
        if (rstate != NULL) {
            RouteStateClearPeerSet(db_entry, rstate, mleave);
            return;
        }

        // Handle the case where it's a RouteUpdate.
        RouteUpdate *rt_update = dynamic_cast<RouteUpdate *>(dbstate);
        if (rt_update != NULL) {
            bool delete_rt_update =
                RouteUpdateClearPeerSet(db_entry, rt_update, mleave);
            if (delete_rt_update) {
                delete rt_update;
            }
            return;
        }

        // Handle the case where it's a UpdateList.
        UpdateList *uplist = dynamic_cast<UpdateList *>(dbstate);
        bool delete_uplist;

        assert(uplist);
        delete_uplist = UpdateListClearPeerSet(db_entry, uplist, mleave);
        if (delete_uplist) {
            delete uplist;
        }
        return;
    }

    assert(false);
}

//
// Enqueue the specified RouteUpdate to the UpdateQueue and set the listener
// state for the for the DBEntry to point to the RouteUpdate.
// Set the listener state to the UpdateList if it is non-NULL, else set it to
// the RouteUpdate.
//
// Return true if the BgpUpdateSender needs to trigger a tail dequeue for
// the (RibOut, QueueId).
//
bool RibUpdateMonitor::EnqueueUpdate(DBEntryBase *db_entry,
        RouteUpdate *rt_update, UpdateList *uplist) {
    CHECK_CONCURRENCY("db::DBTable");

    UpdateQueue *queue = queue_vec_->at(rt_update->queue_id());
    if (uplist) {
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), uplist);
    } else {
        db_entry->SetState(ribout_->table(), ribout_->listener_id(), rt_update);
    }
    return queue->Enqueue(rt_update);
}

//
// Dequeue the specified RouteUpdate from it's UpdateQueue.
//
void RibUpdateMonitor::DequeueUpdate(RouteUpdate *rt_update) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::SendUpdate");

    UpdateQueue *queue = queue_vec_->at(rt_update->queue_id());
    queue->Dequeue(rt_update);
}

//
// Get the next RouteUpdate after the provided UpdateEntry and return
// the RouteUpdatePtr encapsulator for it.
//
// If the next RouteUpdate is NULL, move the tail marker to be after
// the UpdateEntry.  The tail marker move must be done must be done
// atomically with returning a NULL RouteUpdatePtr. This ensures that
// Enqueue can correctly detect the need to trigger a tail dequeue.
//
RouteUpdatePtr RibUpdateMonitor::GetNextUpdate(int queue_id,
        UpdateEntry *upentry) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    UpdateQueue *queue = queue_vec_->at(queue_id);
    RouteUpdate *next_rt_update = queue->NextUpdate(upentry);
    RouteUpdatePtr update(next_rt_update);
    if (!next_rt_update && upentry->IsUpdate()) {
        RouteUpdate *rt_update = static_cast<RouteUpdate *>(upentry);
        queue->MoveMarker(queue->tail_marker(), rt_update);
    }
    return update;
}

//
// Get the next UpdateEntry after the one provided and return it via
// the output parameter.
//
// Return the RouteUpdatePtr encapsulator for the next UpdateEntry
// if it's a RouteUpdate.  If it's not, return an encapsulator for
// a NULL RouteUpdate.
//
RouteUpdatePtr RibUpdateMonitor::GetNextEntry(int queue_id,
        UpdateEntry *upentry, UpdateEntry **next_upentry_p) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    UpdateQueue *queue = queue_vec_->at(queue_id);
    UpdateEntry *next_upentry = *next_upentry_p = queue->NextEntry(upentry);
    if (next_upentry != NULL && next_upentry->IsUpdate()) {
        RouteUpdate *rt_update = static_cast<RouteUpdate *>(next_upentry);
        RouteUpdatePtr update(rt_update);
        return update;
    }
    return RouteUpdatePtr();
}

//
// Get the next UpdateInfo after the one provided and return it via
// the output parameter.
//
// Return the RouteUpdatePtr encapsulator for the RouteUpdate that
// corresponds to the next UpdateInfo. If there's no next UpdateInfo
// return an encapsulator for a NULL RouteUpdate.
//
RouteUpdatePtr RibUpdateMonitor::GetAttrNext(int queue_id,
        UpdateInfo *current_uinfo, UpdateInfo **next_uinfo_p) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    UpdateQueue *queue = queue_vec_->at(queue_id);
    UpdateInfo *next_uinfo = queue->AttrNext(current_uinfo);
    RouteUpdate *rt_update = NULL;
    if (next_uinfo) {
        rt_update = next_uinfo->update;
    }
    RouteUpdatePtr update(rt_update);
    *next_uinfo_p = next_uinfo;
    return update;
}

//
// Set the listener state for the DBEntryBase to be the DBState.
//
void RibUpdateMonitor::SetEntryState(DBEntryBase *db_entry, DBState *dbstate) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    db_entry->SetState(ribout_->table(), ribout_->listener_id(), dbstate);
}

//
// Clear the listener state for the DBEntryBase.
//
void RibUpdateMonitor::ClearEntryState(DBEntryBase *db_entry) {
    CHECK_CONCURRENCY("bgp::SendUpdate");

    db_entry->ClearState(ribout_->table(), ribout_->listener_id());
}
