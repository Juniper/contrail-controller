/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_ribout_updates.h"

#include "base/logging.h"
#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_update_queue.h"
#include "bgp/bgp_update_monitor.h"
#include "bgp/message_builder.h"
#include "bgp/scheduling_group.h"

using namespace std;

//
// Create a new RibOutUpdates.  Also create the necessary UpdateQueue and
// add them to the vector.
//
RibOutUpdates::RibOutUpdates(RibOut *ribout) : ribout_(ribout) {
    for (int i = 0; i < QCOUNT; i++) {
        UpdateQueue *queue = new UpdateQueue(i);
        queue_vec_.push_back(queue);
    }
    monitor_.reset(new RibUpdateMonitor(ribout, &queue_vec_));
    builder_ = MessageBuilder::GetInstance(ribout->ExportPolicy().encoding);
}

//
// Destructor.  Get rid of all the UpdateQueues.
//
RibOutUpdates::~RibOutUpdates() {
    STLDeleteValues(&queue_vec_);
}

//
// Concurrency: Called in the context of the routing table partition task.
//
// Enqueue the RouteUpdate corresponding to the DBEntryBase into the queue.
// This is called in the context of the routing table partition task. All
// the concurrency issues are handled by going through the monitor.
//
// If the UpdateQueue corresponding to the RouteUpdate previously had no
// updates after the tail marker, we kick the scheduling group to perform
// a tail dequeue for the RibOut.
//
void RibOutUpdates::Enqueue(DBEntryBase *db_entry, RouteUpdate *rt_update) {
    CHECK_CONCURRENCY("db::DBTable");

    bool need_tail_dequeue = monitor_->EnqueueUpdate(db_entry, rt_update);
    if (need_tail_dequeue) {
        SchedulingGroup *group = ribout_->GetSchedulingGroup();
        assert(group != NULL);
        group->RibOutActive(ribout_, rt_update->queue_id());
    }
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Common dequeue routine invoked by tail dequeue and peer dequeue. It builds
// and sends updates for each UpdateInfo element in the list hanging off the
// RouteUpdate. For each update that it builds, it also includes prefixes for
// other UpdateInfo elements that share the same attributes, provided that
// the associated RouteUpdate was enqueued after the original one.

// Each update is targeted at the peers in the RibPeerSet of the UpdateMarker
// passed in to us.  This set of peers is subsequently culled based on the
// RibPeerSet in each UpdateInfo.  IOW, the update is sent only to the set
// of peers in the intersection of the UpdateMarker and the UpdateInfo. Note
// that the UpdateMarker could specify a single peer if we are called from
// peer dequeue.
//
// Assumes that the caller has a lock on the RouteUpdate i.e. the RouteUpdate
// has been obtained by dereferencing a RouteUpdatePtr.
//
// Return false if all the peers in the marker get blocked.  In any case, the
// blocked parameter is populated with the set of peers that are send blocked.
//
bool RibOutUpdates::DequeueCommon(UpdateMarker *marker, RouteUpdate *rt_update,
        RibPeerSet *blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

    BgpTable *table = ribout_->table();

    // Go through all UpdateInfo elements for the RouteUpdate.
    RibPeerSet rt_blocked;
    for (UpdateInfoSList::List::iterator iter = rt_update->Updates()->begin();
         iter != rt_update->Updates()->end();) {

        // Get the UpdateInfo and move the iterator to next one before doing
        // any processing, since we may delete the UpdateInfo further down.
        UpdateInfo *uinfo = iter.operator->();
        ++iter;

        // Skip if there's no overlap between the UpdateMarker and the targets
        // for the UpdateInfo.  The intersection is the set of peers to which
        // the message we are about to build will be sent.
        RibPeerSet msgset;
        msgset.BuildIntersection(uinfo->target, marker->members);
        if (msgset.empty()) {
            continue;
        }

        // Generate the update and merge additional updates into that message.
        auto_ptr<Message> message(
            builder_->Create(table, &uinfo->roattr, rt_update->route()));
        UpdatePack(rt_update->queue_id(), message.get(), uinfo, msgset);
        message->Finish();

        // Send the message to the target RibPeerSet.
        RibPeerSet msg_blocked;
        UpdateSend(message.get(), msgset, &msg_blocked);

        // Reset bits in the UpdateInfo.  Note that this has already been done
        // via UpdatePack for all the other UpdateInfo elements that we packed
        // into this message.
        bool empty = ClearAdvertisedBits(rt_update, uinfo, msgset);
        if (empty) {
            rt_update->RemoveUpdateInfo(uinfo);
        }

        // Update RibPeerSet of peers that got blocked while processing this
        // RouteUpdate. Since there's no overlap of peers between UpdateInfos
        // for the same RouteUpdate, we can update the markers for all blocked
        // peers in one shot i.e. outside this loop.
        rt_blocked |= msg_blocked;
    }

    // Update the markers for any peers that got blocked while processing this
    // RouteUpdate. If all peers in the UpdateMarker got blocked, we shouldn't
    // build any more update messages.  Return false to let the callers know
    // that this has happened.
    if (rt_blocked.empty()) {
        return true;
    } else {
        *blocked |= rt_blocked;
        return !UpdateMarkersOnBlocked(marker, rt_update, &rt_blocked);
    }
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Dequeue and build updates for the in-sync peers in the RibPeerSet of the
// tail marker for the given queue id.
//
// Return false if all the peers in the marker get blocked.  In any case, the
// blocked parameter is populated with the set of peers that are send blocked.
//
bool RibOutUpdates::TailDequeue(int queue_id, const RibPeerSet &msync,
        RibPeerSet *blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

    UpdateQueue *queue = queue_vec_[queue_id];
    UpdateMarker *start_marker = queue->tail_marker();
    RouteUpdatePtr update = monitor_->GetNextUpdate(queue_id, start_marker);

    if (update.get() == NULL) {
        return true;
    }

    // Intersect marker membership and in-sync peers to come up with the
    // unsync peers. If all the peers are unsync return right away. The
    // SchedulingGroup will take care of triggering a TailDequeue again
    // when at least one peer becomes in-sync.
    RibPeerSet unsync;
    unsync.BuildComplement(start_marker->members, msync);
    if (unsync == start_marker->members) {
        return false;
    }

    // Split the unsync peers from the tail marker. Note that this updates
    // the RibPeerSet in the tail marker.
    if (!unsync.empty()) {
        queue->MarkerSplit(start_marker, unsync);
    }

    // Update send loop. Select next update to send, format a message.
    // Add other updates with the same attributes and replicate the
    // packet.
    RouteUpdatePtr next_update;
    for (; update.get() != NULL; update = next_update) {
        if (!DequeueCommon(start_marker, update.get(), blocked)) {

            // Be sure to get rid of the RouteUpdate if it's empty.
            if (update->empty()) {
                ClearUpdate(&update);
            }

            return false;
        }

        // Iterate to the next update before we potentially delete the
        // current one. If there are no more updates in the queue, the
        // marker will get moved so that it's after the current update.
        next_update = monitor_->GetNextUpdate(queue_id, update.get());

        // Be sure to get rid of the RouteUpdate if it's empty.
        if (update->empty()) {
            ClearUpdate(&update);
        }
    }

    return true;
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Dequeue and build updates for all the peers that share the same marker as
// the specified peer.  This routine has some extra intelligence beyond the
// TailDequeue. As it encounters update markers, it merges in any send ready
// peers from those with the marker being processed for dequeue. This is done
// to reduce the number of times we build an update message containing the
// the same information.
//
// Return false if all the peers in the marker get blocked.  In any case, the
// blocked parameter is populated with the set of peers that are send blocked.
//
bool RibOutUpdates::PeerDequeue(int queue_id, IPeerUpdate *peer,
        const RibPeerSet &mready,
        RibPeerSet *blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

    UpdateQueue *queue = queue_vec_[queue_id];
    UpdateMarker *start_marker = queue->GetMarker(ribout_->GetPeerIndex(peer));

    // We're done if this is the same as the tail marker.  Updates will be
    // built subsequently via TailDequeue.
    assert(start_marker);
    if (start_marker == queue->tail_marker()) {
        return true;
    }

    // TODO: Look back in the queue for immediate predecessors and move
    // any send-ready peers into the current marker.

    // Get the encapsulator for the first RouteUpdate.  Even if there's no
    // RouteUpdate, we should find another marker or the tail marker.
    UpdateEntry *upentry;
    RouteUpdatePtr update =
        monitor_->GetNextEntry(queue_id, start_marker, &upentry);
    assert(upentry);

    // At least one peer in the start marker i.e. the peer for which we are
    // called must be send ready.
    assert(start_marker->members.intersects(mready));

    // Split out any peers from the marker that are not send ready. Note that
    // this updates the RibPeerSet in the marker.
    RibPeerSet notready;
    notready.BuildComplement(start_marker->members, mready);
    if (!notready.empty()) {
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                    "PeerDequeue Split: " << notready.ToString());
        queue->MarkerSplit(start_marker, notready);
    }

    // Update loop.  Keep going till we reach the tail marker or till all the
    // peers get blocked.  In either case, we simply return from the loop.
    RouteUpdatePtr next_update;
    UpdateEntry *next_upentry;
    for (; upentry != NULL; upentry = next_upentry, update = next_update) {
        UpdateMarker *marker = NULL;
        if (upentry->IsMarker()) {

            // The queue entry is a marker.  We're done if we've reached the
            // tail marker.  Updates will be built later via TailDequeue.
            marker = static_cast<UpdateMarker *>(upentry);
            if (marker == queue->tail_marker()) {
                queue->MarkerMerge(queue->tail_marker(), start_marker,
                        start_marker->members);
                return true;
            }
        } else {

            // The queue entry is a RouteUpdate. Go ahead and build an update
            // message.  Bail if all the peers in the marker get blocked.
            if (!DequeueCommon(start_marker, update.get(), blocked)) {

                // Be sure to get rid of the RouteUpdate if it's empty.
                if (update->empty()) {
                    ClearUpdate(&update);
                }
                return false;
            }
        }

        // Iterate to the next element before we potentially delete the
        // current one.
        next_update =
            monitor_->GetNextEntry(queue_id, upentry, &next_upentry);

        if (upentry->IsMarker()) {

            // As the entry is a marker, merge send-ready peers from it
            // with the marker that is being processed for dequeue.  Note
            // that this updates the RibPeerSet in the marker.
            RibPeerSet mmove;
            mmove.BuildIntersection(marker->members, mready);
            if  (!mmove.empty()) {
                BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG,
                            BGP_LOG_FLAG_TRACE,
                            "PeerDequeue Merge: " << mmove.ToString());
                queue->MarkerMerge(start_marker, marker, mmove);
            }

        } else if (update->empty()) {

            // Be sure to get rid of the RouteUpdate since it's empty.
            ClearUpdate(&update);
        }
    }

    // Should never get here.
    assert(false);
    return false;
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Go through all the UpdateInfo elements that have the same attribute as
// the start parameter and pack the corresponding prefixes into the Message.
// The attributes and the prefix associated with start are already in the
// Message when this method is invoked.
//
// The set of peers for which this update is being built is represented by
// the msgset parameter.  As the msgset has already been determined by the
// caller, we should only add prefixes that need to go to all the peers in
// the msgset.
//
void RibOutUpdates::UpdatePack(int queue_id, Message *message,
        UpdateInfo *start_uinfo, const RibPeerSet &msgset) {
    CHECK_CONCURRENCY("bgp::SendTask");

    UpdateInfo *uinfo, *next_uinfo;
    RouteUpdatePtr next_update;

    // Walk through all the UpdateInfo elements with the same attribute in
    // enqueue order.
    RouteUpdatePtr update =
        monitor_->GetAttrNext(queue_id, start_uinfo, &uinfo);
    for (; update.get() != NULL; update = next_update, uinfo = next_uinfo) {

        // Iterate to the next element before we potentially delete the
        // current one.
        next_update = monitor_->GetAttrNext(queue_id, uinfo, &next_uinfo);

        // Skip if the msgset RibPeerSet is not a subset of the target in
        // UpdateInfo.
        if (!uinfo->target.Contains(msgset))
            continue;

        // Go ahead and add the route to the message.  Terminate the loop
        // if the message doesn't have room for the route.  The route will
        // get included in another update message.
        bool success = message->AddRoute(update->route(), &uinfo->roattr);
        if (!success) {
            break;
        }

        // First clear the advertised bits as represented by msgset from
        // the target RibPeerSet in the UpdateInfo. If the target is now
        // empty, remove the UpdateInfo from the list container in the
        // underlying RouteUpdate.
        //
        // If the RouteUpdate itself is now empty i.e. there are no more
        // UpdateInfo elements associated with it, we can get rid of it.
        bool empty = ClearAdvertisedBits(update.get(), uinfo, msgset);
        if (empty && update->RemoveUpdateInfo(uinfo)) {
            ClearUpdate(&update);
        }
    }
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Go through all the peers in the specified RibPeerSet and send the given
// message to each of them.  Update the blocked RibPeerSet with peers that
// become blocked after sending the message.
//
void RibOutUpdates::UpdateSend(Message *message, const RibPeerSet &dst,
        RibPeerSet *blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

    RibOut::PeerIterator iter(ribout_, dst);
    while (iter.HasNext()) {
        int ix_current = iter.index();
        IPeerUpdate *peer = iter.Next();
        size_t msgsize;
        const uint8_t *data = message->GetData(peer, &msgsize);
        bool more = peer->SendUpdate(data, msgsize);
        if (!more) {
            blocked->set(ix_current);
        }
        IPeer *tmp = dynamic_cast<IPeer *>(peer);
        if (!tmp) continue;
        IPeerDebugStats *stats = tmp->peer_stats();
        if (stats) {
            stats->UpdateTxReachRoute(message->num_reach_routes());
            stats->UpdateTxUnreachRoute(message->num_unreach_routes());
        }
    }
}

//
// Concurrency: caller must own update lock.
//
// Take the AdvertisedInfo history in the RouteUpdate and move it to a new
// RouteState. Go through the monitor to associate the new RouteState as the
// listener state for the Route.
//
void RibOutUpdates::StoreHistory(RouteUpdate *rt_update) {
    CHECK_CONCURRENCY("bgp::SendTask");

    BgpRoute *route = rt_update->route();
    RouteState *rstate = new RouteState();
    rt_update->MoveHistory(rstate);
    monitor_->SetEntryState(route, rstate);
}

//
// Concurrency: caller must own update lock.
//
// Go through the monitor to clear the listener state for the underlying Route
// of the RouteUpdate.
//
void RibOutUpdates::ClearState(RouteUpdate *rt_update) {
    CHECK_CONCURRENCY("bgp::SendTask");

    BgpRoute *route = rt_update->route();
    monitor_->ClearEntryState(route);
}

//
// Concurrency: caller must own entry lock.
//
// Called when the RouteUpdate encapsulated by the RouteUpdatePtr has no more
// UpdateInfo elements. Releases ownership of the RouteUpdate and deletes the
// RouteUpdate, as well as any associated UpdateList if appropriate.
//
void RibOutUpdates::ClearUpdate(RouteUpdatePtr *update) {
    CHECK_CONCURRENCY("bgp::SendTask");

    // Dequeue the route update.
    RouteUpdate *rt_update = update->get();
    monitor_->DequeueUpdate(rt_update);

    if (rt_update->OnUpdateList()) {

        // Remove the route update from the update list and check if the list
        // can now be downgraded back to a route update.
        UpdateList *uplist = rt_update->GetUpdateList(ribout_);
        uplist->RemoveUpdate(rt_update);
        RouteUpdate *last_rt_update = uplist->MakeRouteUpdate();

        // If we were able to downgrade, set the DBEntry to point to the last
        // remaining route update and get rid of the current route update and
        // the update list.  Otherwise, just get rid of the route update.
        if (last_rt_update) {
            monitor_->SetEntryState(rt_update->route(), last_rt_update);
            update->release();
            delete rt_update;
            delete uplist;
        } else {
            update->release();
            delete rt_update;
        }

    } else {

        // Store the history from the route update or clear the state for the
        // DBEntry depending on whether we advertised the route.  In either
        // case, get rid of the route update.
        if (rt_update->IsAdvertised()) {
            StoreHistory(rt_update);
        } else {
            ClearState(rt_update);
        }
        update->release();
        delete rt_update;
    }
}

//
// Concurrency: caller must own update lock
//
// Clear the advertised bits specified by isect from the target RibPeerSet in
// the UpdateInfo.  If the target is now empty, remove the UpdateInfo from the
// set container in the UpdateQueue.  Note that the UpdateInfo will still be
// on the SList in the RouteUpdate.
//
// Return true if the UpdateInfo was removed from the set container.
//
bool RibOutUpdates::ClearAdvertisedBits(RouteUpdate *rt_update,
        UpdateInfo *uinfo, const RibPeerSet &isect) {
    CHECK_CONCURRENCY("bgp::SendTask");

    uinfo->target.Reset(isect);
    rt_update->UpdateHistory(ribout_, &uinfo->roattr, isect);
    bool empty = uinfo->target.empty();
    if (empty) {
        UpdateQueue *queue = queue_vec_[rt_update->queue_id()];
        queue->AttrDequeue(uinfo);
    }
    return empty;
}

//
// Concurrency: Called in the context of the scheduling group task.
//
// Update the markers for all the peers in the blocked RibPeerSet.  In the
// general case we clear the blocked RibPeerSet from the UpdateMarker and
// create a new UpdateMarker for the blocked peers.
//
// Return true in the special case where all peers in the UpdateMarker have
// become blocked.
//
bool RibOutUpdates::UpdateMarkersOnBlocked(UpdateMarker *marker,
        RouteUpdate *rt_update,
        const RibPeerSet *blocked) {
    CHECK_CONCURRENCY("bgp::SendTask");

    assert(!blocked->empty());
    UpdateQueue *queue = queue_vec_[rt_update->queue_id()];

    // If all the peers in the UpdateMarker are blocked, we simply move the
    // marker after the RouteUpdate.
    if (marker->members == *blocked) {
        queue->MoveMarker(marker, rt_update);
        return true;
    }

    // Reset bits in the specified UpdateMarker, create a new one for the
    // blocked peers and insert the new one after the RouteUpdate.
    marker->members.Reset(*blocked);
    assert(!marker->members.empty());
    UpdateMarker *new_marker = new UpdateMarker();
    new_marker->members = *blocked;
    queue->AddMarker(new_marker, rt_update);

    return false;
}

bool RibOutUpdates::Empty() const {
    for (int i = 0; i < RibOutUpdates::QCOUNT; ++i) {
        UpdateQueue *queue = queue_vec_[i];
        if (!queue->empty()) {
            return false;
        }
    }
    return true;
}

void RibOutUpdates::QueueJoin(int queue_id, int bit) {
    UpdateQueue *queue = queue_vec_[queue_id];
    queue->Join(bit);
}

void RibOutUpdates::QueueLeave(int queue_id, int bit) {
    UpdateQueue *queue = queue_vec_[queue_id];
    queue->Leave(bit);
}
