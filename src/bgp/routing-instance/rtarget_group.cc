/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/rtarget_group.h"

#include "bgp/bgp_route.h"
#include "bgp/rtarget/rtarget_route.h"


RtGroup::RtGroup(const RouteTarget &rt) 
    : rt_(rt), dep_(RtGroup::RTargetDepRouteList(DB::PartitionCount())) {
    }

const RtGroup::RTargetDepRouteList &RtGroup::DepRouteList() const {
    return dep_;
}

const RtGroup::RtGroupMemberList &RtGroup::GetImportTables(Address::Family family) {
    return import_[family];
}

const RtGroup::RtGroupMemberList &RtGroup::GetExportTables(Address::Family family) {
    return export_[family];
}

bool RtGroup::AddImportTable(Address::Family family, BgpTable *tbl) {
    bool first = import_[family].empty();
    import_[family].insert(tbl);
    return first;
}

bool RtGroup::AddExportTable(Address::Family family, BgpTable *tbl) {
    bool first = export_[family].empty();
    export_[family].insert(tbl);
    return first;
}

bool RtGroup::RemoveImportTable(Address::Family family, BgpTable *tbl) {
    import_[family].erase(tbl);
    return import_[family].empty();
}

bool RtGroup::RemoveExportTable(Address::Family family, BgpTable *tbl) {
    export_[family].erase(tbl);
    return export_[family].empty();
}

bool RtGroup::empty(Address::Family family) {
    return import_[family].empty() && export_[family].empty();
}

const RouteTarget &RtGroup::rt() {
    return rt_;
}

void RtGroup::AddDepRoute(int part_id, BgpRoute *rt) {
    dep_[part_id].insert(rt);
}

void RtGroup::RemoveDepRoute(int part_id, BgpRoute *rt) {
    dep_[part_id].erase(rt);
}

bool RtGroup::RouteDepListEmpty() {
    for (RtGroup::RTargetDepRouteList::const_iterator it = dep_.begin(); 
         it != dep_.end(); it++) {
        if (!it->empty()) {
            return false;
        }
    }
    return true;
}

const RtGroup::InterestedPeerList &RtGroup::PeerList() const {
    return peer_list_;
}

bool RtGroup::IsPeerInterested(const BgpPeer *peer) const {
    return (peer_list_.find(peer) != peer_list_.end());
}

void RtGroup::GetInterestedPeers(std::set<const BgpPeer *> &peer_set) const {
    for (RtGroup::InterestedPeerList::const_iterator it = peer_list_.begin(); 
         it != peer_list_.end(); it++) {
        peer_set.insert(it->first);
    }
}

const RtGroupInterestedPeerSet& RtGroup::GetInterestedPeers() const {
    return interested_peers_;
}

void RtGroup::AddInterestedPeer(const BgpPeer *peer, RTargetRoute *rt) {
    RtGroup::InterestedPeerList::iterator it = peer_list_.find(peer);
    if (it == peer_list_.end()) {
        it = peer_list_.insert(peer_list_.begin(), 
                               std::pair<const BgpPeer *, 
                               RTargetRouteList>(peer, RTargetRouteList()));
        interested_peers_.set(peer->GetIndex());
    }
    it->second.insert(rt);
}

void RtGroup::RemoveInterestedPeer(const BgpPeer *peer, RTargetRoute *rt) {
    RtGroup::InterestedPeerList::iterator it = peer_list_.find(peer);
    if (it == peer_list_.end()) return;
    it->second.erase(rt);
    if (it->second.empty()) {
        peer_list_.erase(peer);
        interested_peers_.reset(peer->GetIndex());
    }
}

bool RtGroup::peer_list_empty() const {
    return peer_list_.empty();
}
