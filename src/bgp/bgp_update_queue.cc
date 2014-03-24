/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_update_queue.h"

#include "base/logging.h"

//
// Initialize the UpdateQueue and add the tail marker to the FIFO.
//
UpdateQueue::UpdateQueue(int queue_id)
    : queue_id_(queue_id), marker_count_(0) {
    queue_.push_back(tail_marker_);
}

//
// Destroy the UpdateQueue after removing the tail marker from the FIFO.
//
UpdateQueue::~UpdateQueue() {
    queue_.erase(queue_.iterator_to(tail_marker_));
    assert(queue_.empty());
    assert(attr_set_.empty());
}

//
// Enqueue the specified RouteUpdate to the UpdateQueue.  Updates both the
// FIFO and the set container.
//
// Return true if the UpdateQueue had no RouteUpdates after the tail marker.
//
bool UpdateQueue::Enqueue(RouteUpdate *rt_update) {
    tbb::mutex::scoped_lock lock(mutex_);
    rt_update->set_tstamp_now();

    // Insert at the end of the FIFO. Remember if the FIFO previously had
    // no RouteUpdates after the tail marker.
    UpdateEntry *tail_upentry = &(queue_.back());
    bool need_tail_dequeue = (tail_upentry == &tail_marker_);
    queue_.push_back(*rt_update);

    // Go through the UpdateInfo list and insert each element into the set
    // container for the UpdateQueue.  Also set up the back pointer to the
    // RouteUpdate.
    UpdateInfoSList &uinfo_slist = rt_update->Updates();
    for (UpdateInfoSList::List::iterator iter = uinfo_slist->begin();
         iter != uinfo_slist->end(); ++iter) {
        iter->update = rt_update;
        attr_set_.insert(*iter);
    }
    return need_tail_dequeue;
}

//
// Dequeue the specified RouteUpdate from the UpdateQueue.  All UpdateInfo
// elements for the RouteUpdate are removed from the set container.
//
void UpdateQueue::Dequeue(RouteUpdate *rt_update) {
    tbb::mutex::scoped_lock lock(mutex_);
    queue_.erase(queue_.iterator_to(*rt_update));
    UpdateInfoSList &uinfo_slist = rt_update->Updates();
    for (UpdateInfoSList::List::iterator iter = uinfo_slist->begin();
         iter != uinfo_slist->end(); ++iter) {
        attr_set_.erase(attr_set_.iterator_to(*iter));
    }
}

//
// Return the next RouteUpdate after the given UpdateEntry on the UpdateQueue.
// Traverses the FIFO and skips over MARKERs and other element types to find
// the next RouteUpdate.
//
// Return NULL if there's no such RouteUpdate on the FIFO.
//
RouteUpdate *UpdateQueue::NextUpdate(UpdateEntry *current_upentry) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdatesByOrder::iterator iter = queue_.iterator_to(*current_upentry);
    while (++iter != queue_.end()) {
        UpdateEntry *upentry = iter.operator->();
        if (upentry->IsUpdate()) {
            return static_cast<RouteUpdate *>(upentry);
        }
    }
    return NULL;
}

//
// Return the next UpdateEntry on the UpdateQueue after the one specified.
// Traverses the FIFO and returns the next UpdateEntry, irrespective of the
// type.
//
// Return NULL if there's no such UpdateEntry on the FIFO.
//
UpdateEntry *UpdateQueue::NextEntry(UpdateEntry *current_upentry) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdatesByOrder::iterator iter = queue_.iterator_to(*current_upentry);
    if (++iter == queue_.end()) {
        return NULL;
    }
    return iter.operator->();
}

//
// Dequeue the specified UpdateInfo from the set container.
//
void UpdateQueue::AttrDequeue(UpdateInfo *current_uinfo) {
    tbb::mutex::scoped_lock lock(mutex_);
    attr_set_.erase(attr_set_.iterator_to(*current_uinfo));
}

//
// Return the next UpdateInfo after the one provided. This traverses the set
// container and returns the next one if it has the same BgpAttr.  Note that
// we must not consider the label in order to ensure optimal packing.
//
// Returns NULL if there are no more updates with the same BgpAttr.
//
UpdateInfo *UpdateQueue::AttrNext(UpdateInfo *current_uinfo) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdatesByAttr::iterator iter = attr_set_.iterator_to(*current_uinfo);
    ++iter;
    if (iter == attr_set_.end()) {
        return NULL;
    }
    UpdateInfo *next_uinfo = iter.operator->();
    if (next_uinfo->roattr.attr() == current_uinfo->roattr.attr()) {
        return next_uinfo;
    }
    return NULL;
}

//
// Add the provided UpdateMarker after a specific RouteUpdate. Also update
// the MarkerMap so that all peers in the provided UpdateMarker now point
// to it.
//
void UpdateQueue::AddMarker(UpdateMarker *marker, RouteUpdate *rt_update) {
    assert(!marker->members.empty());
    tbb::mutex::scoped_lock lock(mutex_);
    marker_count_++;
    queue_.insert(++queue_.iterator_to(*rt_update), *marker);

    for (size_t i = marker->members.find_first();
         i != BitSet::npos; i = marker->members.find_next(i)) {
        MarkerMap::iterator loc = markers_.find(i);
        assert(loc != markers_.end());
        loc->second = marker;
    }
}

//
// Move the provided UpdateMarker so that it's after the RouteUpdate. Since
// the entire UpdateMarker itself is being moved, there's no need to update
// the MarkerMap.
//
// If the UpdateMarker is the tail marker, skip over all markers after the
// RouteUpdate till we hit the next RouteUpdate or the end of the queue.
// This is needed for the case where some peers get blocked after sending
// the last RouteUpdate in the queue. In that scenario, the marker for the
// blocked peers is inserted after the RouteUpdate and we then try to move
// the tail marker after the RouteUpdate.
//
void UpdateQueue::MoveMarker(UpdateMarker *marker, RouteUpdate *rt_update) {
    tbb::mutex::scoped_lock lock(mutex_);
    queue_.erase(queue_.iterator_to(*marker));

    UpdatesByOrder::iterator iter = queue_.iterator_to(*rt_update);
    if (marker == &tail_marker_) {
        for (iter++; iter != queue_.end() && iter->IsMarker(); iter++);
        queue_.insert(iter, *marker);
    } else {
        queue_.insert(++iter, *marker);
    }
}

//
// Split the specified UpdateMarker based on the RibPeerSet. A new marker is
// created for the peers in the RibPeerSet and is inserted immediately before
// the existing marker.  The bits corresponding to the RibPeerSet being split
// are reset in the existing marker.
//
// The MarkerMap is updated so that all the peers in in the RibPeerSet point
// to the new marker.
//
void UpdateQueue::MarkerSplit(UpdateMarker *marker, const RibPeerSet &msplit) {
    assert(!msplit.empty());
    UpdateMarker *split_marker = new UpdateMarker();
    split_marker->members = msplit;

    tbb::mutex::scoped_lock lock(mutex_);
    marker->members.Reset(msplit);
    assert(!marker->members.empty());
    UpdatesByOrder::iterator mpos = queue_.iterator_to(*marker);
    queue_.insert(mpos, *split_marker);
    marker_count_++;
    
    for (size_t i = msplit.find_first();
         i != BitSet::npos; i = msplit.find_next(i)) {
        MarkerMap::iterator loc = markers_.find(i);
        assert(loc != markers_.end());
        loc->second = split_marker;
    }
}

//
// Merge the peers corresponding to the RibPeerSet from the src UpdateMarker
// to the dst. The bits corresponding to the RibPeerSet being merged into the
// dst are reset in the src marker. If the src UpdateMarker has no more peers,
// as a result, it is removed from the FIFO and destroyed.
//
void UpdateQueue::MarkerMerge(UpdateMarker *dst_marker,
        UpdateMarker *src_marker, const RibPeerSet &bitset) {
    assert(!bitset.empty());
    tbb::mutex::scoped_lock lock(mutex_);

    // Set the bits in dst and update the MarkerMap.  Be sure to set the dst
    // before we reset the src since bitset maybe a reference to src->members.
    dst_marker->members.Set(bitset);
    for (size_t i = bitset.find_first();
         i != BitSet::npos; i = bitset.find_next(i)) {
        MarkerMap::iterator loc = markers_.find(i);
        assert(loc != markers_.end());
        loc->second = dst_marker;
    }

    // Reset the bits in the src and get rid of it in case it's now empty.
    src_marker->members.Reset(bitset);
    if (src_marker->members.empty()) {
        queue_.erase(queue_.iterator_to(*src_marker));
        assert(src_marker != &tail_marker_);
        delete src_marker;
        marker_count_--;
    }
}

//
// Return the UpdateMarker for the peer specified by the bit position.
//
UpdateMarker *UpdateQueue::GetMarker(int bit) {
    tbb::mutex::scoped_lock lock(mutex_);
    MarkerMap::iterator loc = markers_.find(bit);
    assert(loc != markers_.end());
    return loc->second;
}

//
// Join a new peer, as represented by it's bit index, to the UpdateQueue.
// Since it's a new peer, it starts out at the tail marker.  Also add the
// peer's bit index and the tail marker pair to the MarkerMap.
//
void UpdateQueue::Join(int bit) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdateMarker *marker = &tail_marker_;
    marker->members.set(bit);
    markers_.insert(std::make_pair(bit, marker));
}

//
// Leave a peer, as represented by it's bit index, from the UpdateQueue.
// Find the current marker for the peer and remove the peer's bit from the
// MarkerMap. Reset the peer's bit in the marker and get rid of the marker
// itself if it's now empty.
//
void UpdateQueue::Leave(int bit) {
    tbb::mutex::scoped_lock lock(mutex_);
    MarkerMap::iterator loc = markers_.find(bit);
    assert(loc != markers_.end());
    UpdateMarker *marker = loc->second;
    markers_.erase(loc);
    marker->members.reset(bit);
    if (marker != &tail_marker_  && marker->members.empty()) {
        queue_.erase(queue_.iterator_to(*marker));
        delete marker;
    }
}

bool UpdateQueue::CheckInvariants() const {
    tbb::mutex::scoped_lock lock(mutex_);
    for (MarkerMap::const_iterator iter = markers_.begin();
         iter != markers_.end(); ++iter) {
        UpdateMarker *marker = iter->second;
        CHECK_INVARIANT(marker->members.test(iter->first));
    }
    return true;
}

//
// Return true if the UpdateQueue is empty.  It's considered empty if there
// are as no UpdateInfo elements in the set container. We don't look at the
// FIFO since that may still have the tail marker on it.
//
bool UpdateQueue::empty() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return attr_set_.empty();
}

size_t UpdateQueue::size() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return attr_set_.size();
}

size_t UpdateQueue::marker_count() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return marker_count_;
}
