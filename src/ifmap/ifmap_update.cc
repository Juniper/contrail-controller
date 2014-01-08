/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update.h"

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

IFMapUpdate::IFMapUpdate(IFMapNode *node, bool positive)
    : IFMapListEntry(positive ? UPDATE : DELETE),
      data_(node) {
}

IFMapUpdate::IFMapUpdate(IFMapLink *link, bool positive)
    : IFMapListEntry(positive ? UPDATE : DELETE),
      data_(link) {
}

std::string IFMapUpdate::ToString() {
    std::string first;
    if (data().type == IFMapObjectPtr::NODE) {
        IFMapNode *node = data().u.node;
        first = node->ToString();
    } else if (data().type == IFMapObjectPtr::LINK) {
        IFMapLink *link = data().u.link;
        first = link->ToString();
    }
    return first + ":" + TypeToString();
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

IFMapState::IFMapState() : sig_(kInvalidSig) {
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

IFMapNodeState::IFMapNodeState() {
}

bool IFMapNodeState::HasDependents() const {
    return !dependents_.empty();
}

IFMapLinkState::IFMapLinkState(IFMapLink *link)
    : left_(link), right_(link) {
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

