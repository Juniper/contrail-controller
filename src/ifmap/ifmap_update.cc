/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update.h"

#include "base/time_util.h"
#include "ifmap/ifmap_node.h"

IFMapObjectPtr::IFMapObjectPtr() 
    : type(NIL) {
      u.ptr = NULL;
}

IFMapObjectPtr::IFMapObjectPtr(IFMapNode *node) 
    : type(NODE) {
      u.node = node;
}

IFMapObjectPtr::IFMapObjectPtr(IFMapLink *link) 
    : type(LINK) {
      u.link = link;
}

void IFMapListEntry::set_queue_insert_at_to_now() {
    queue_insert_at = UTCTimestampUsec();
}

std::string IFMapListEntry::queue_insert_ago_str() {
    return duration_usecs_to_string(UTCTimestampUsec() - queue_insert_at);
}

IFMapUpdate::IFMapUpdate(IFMapNode *node, bool positive)
    : IFMapListEntry(positive ? UPDATE : DELETE),
      data_(node) {
}

IFMapUpdate::IFMapUpdate(IFMapLink *link, bool positive)
    : IFMapListEntry(positive ? UPDATE : DELETE),
      data_(link) {
}

std::string IFMapUpdate::ConfigName() {
    std::string name;
    if (data().type == IFMapObjectPtr::NODE) {
        IFMapNode *node = data().u.node;
        name = node->ToString();
    } else if (data().type == IFMapObjectPtr::LINK) {
        IFMapLink *link = data().u.link;
        name = link->ToString();
    }
    return name;
}

std::string IFMapUpdate::ToString() {
    return TypeToString() + ":" + ConfigName();
}

void IFMapUpdate::AdvertiseReset(const BitSet &set) {
    advertise_.Reset(set);
}

void IFMapUpdate::AdvertiseOr(const BitSet &set) {
    advertise_ |= set;
}

void IFMapUpdate::SetAdvertise(const BitSet &set) {
    advertise_ = set;
}

IFMapMarker::IFMapMarker()
    : IFMapListEntry(MARKER) {
}

std::string IFMapMarker::ToString() {
    return std::string("Marker:") + mask.ToNumberedString();
}

IFMapState::IFMapState(IFMapNode *node)
    : sig_(kInvalidSig), data_(node), crc_(0) {
}

IFMapState::IFMapState(IFMapLink *link)
    : sig_(kInvalidSig), data_(link), crc_(0) {
}

IFMapState::~IFMapState() {
    assert(update_list_.empty());
}

IFMapUpdate *IFMapState::GetUpdate(IFMapListEntry::EntryType type) {
    for (UpdateList::iterator iter = update_list_.begin();
         iter != update_list_.end(); ++iter) {
        IFMapUpdate *update = iter.operator->();
        if (update->type == type) {
            return update;
        }
    }
    return NULL;
}

void IFMapState::Insert(IFMapUpdate *update) {
    update_list_.push_front(*update);
}

void IFMapState::Remove(IFMapUpdate *update) {
    update_list_.erase(update_list_.s_iterator_to(*update));
}

IFMapNode *IFMapState::GetIFMapNode() const {
    if (data_.IsNode()) {
        return data_.u.node;
    } else {
        return NULL;
    }
}

IFMapLink *IFMapState::GetIFMapLink() const {
    if (data_.IsLink()) {
        return data_.u.link;
    } else {
        return NULL;
    }
}

IFMapNodeState::IFMapNodeState(IFMapNode *node)
    : IFMapState(node) {
}

bool IFMapNodeState::HasDependents() const {
    return !dependents_.empty();
}

IFMapLinkState::IFMapLinkState(IFMapLink *link)
    : IFMapState(link), left_(link), right_(link) {
}

void IFMapLinkState::SetDependency(IFMapNodeState *first,
                                   IFMapNodeState *second) {
    left_.reset(first);
    right_.reset(second);
}

void IFMapLinkState::RemoveDependency() {
    left_.clear();
    right_.clear();
}

bool IFMapLinkState::HasDependency() const {
    return left_.get() != NULL;
}

