/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>

#include <boost/bind.hpp>

#include "base/string_util.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_ribout_updates.h"
#include "bgp/bgp_export.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_update.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/scheduling_group.h"

using std::find;

//
// Implement operator< for RibExportPolicy by comparing each of the fields.
//
bool RibExportPolicy::operator<(const RibExportPolicy &rhs) const {
    if (encoding < rhs.encoding) {
        return true;
    }
    if (encoding > rhs.encoding) {
        return false;
    }
    if (type < rhs.type) {
        return true;
    }
    if (type > rhs.type) {
        return false;
    }
    if (as_number < rhs.as_number) {
        return true;
    }
    if (as_number > rhs.as_number) {
        return false;
    }
    if (affinity < rhs.affinity)  {
        return true;
    }
    if (affinity > rhs.affinity) {
        return false;
    }
    if (cluster_id < rhs.cluster_id) {
        return true;
    }
    if (cluster_id > rhs.cluster_id) {
        return false;
    }
    return false;
}

RibOutAttr::NextHop::NextHop(const BgpTable *table, IpAddress address,
                             uint32_t label, const ExtCommunity *ext_community,
                             bool vrf_originated)
    : address_(address),
      label_(label),
      origin_vn_index_(-1) {
    if (ext_community) {
        encap_ = ext_community->GetTunnelEncap();
        origin_vn_index_ = ext_community->GetOriginVnIndex();
    }
    if (origin_vn_index_ < 0 && vrf_originated) {
        if (table)
            origin_vn_index_ = table->routing_instance()->virtual_network_index();
        else
            origin_vn_index_ = 0;
    }
}

int RibOutAttr::NextHop::CompareTo(const NextHop &rhs) const {
    if (address_ < rhs.address_) return -1;
    if (address_ > rhs.address_) return 1;
    if (label_ < rhs.label_) return -1;
    if (label_ > rhs.label_) return 1;
    if (origin_vn_index_ < rhs.origin_vn_index_) return -1;
    if (origin_vn_index_ > rhs.origin_vn_index_) return 1;
    if (encap_.size() < rhs.encap_.size()) return -1;
    if (encap_.size() > rhs.encap_.size()) return 1;
    for (size_t idx = 0; idx < encap_.size(); idx++) {
        if (encap_[idx] < rhs.encap_[idx]) return -1;
        if (encap_[idx] > rhs.encap_[idx]) return 1;
    }
    return 0;
}

bool RibOutAttr::NextHop::operator==(const NextHop &rhs) const {
    return CompareTo(rhs) == 0;
}

bool RibOutAttr::NextHop::operator!=(const NextHop &rhs) const {
    return CompareTo(rhs) != 0;
}

RibOutAttr::RibOutAttr(const BgpTable *table, const BgpAttr *attr,
                       uint32_t label, bool include_nh) : attr_out_(attr) {
    if (attr && include_nh) {
        nexthop_list_.push_back(NextHop(table, attr->nexthop(), label,
                                        attr->ext_community(), false));
    }
}

RibOutAttr::RibOutAttr(BgpRoute *route, const BgpAttr *attr, bool is_xmpp) {
    // Attribute should not be set already
    assert(!attr_out_);

    const BgpTable *table = static_cast<const BgpTable *>(route->get_table());

    // Always encode best path's attributes (including it's nexthop) and label.
    if (!is_xmpp) {
        set_attr(table, attr, route->BestPath()->GetLabel(), false);
        return;
    }

    // Encode ECMP NextHops only for XMPP peers.
    // Vrf Origination matters only for XMPP peers.
    set_attr(table, attr, route->BestPath()->GetLabel(),
             route->BestPath()->IsVrfOriginated());

    for (Route::PathList::iterator it = route->GetPathList().begin();
        it != route->GetPathList().end(); it++) {
        const BgpPath *path = static_cast<BgpPath *>(it.operator->());

        // Skip the best path.
        if (path == route->BestPath())
            continue;

        // Check if the path is ECMP eligible. If not, bail out, as the paths
        // are sorted in cost order anyways.
        if (route->BestPath()->PathCompare(*path, true))
            break;

        // We have an eligible ECMP path.
        // Remember if the path was originated in the VRF.  This is used to
        // determine if VRF's VN name can be used as the origin VN for the
        // nexthop.
        NextHop nexthop(table, path->GetAttr()->nexthop(), path->GetLabel(),
                        path->GetAttr()->ext_community(),
                        path->IsVrfOriginated());

        // Skip if we have already encoded this next-hop
        if (find(nexthop_list_.begin(), nexthop_list_.end(), nexthop) !=
                nexthop_list_.end()) {
            continue;
        }
        nexthop_list_.push_back(nexthop);
    }
}

//
// Comparator for RibOutAttr. First compare the BgpAttr and then the nexthops.
//
int RibOutAttr::CompareTo(const RibOutAttr &rhs) const {
    if (attr_out_.get() < rhs.attr_out_.get()) {
        return -1;
    }
    if (attr_out_.get() > rhs.attr_out_.get()) {
        return 1;
    }
    if (nexthop_list_.size() < rhs.nexthop_list_.size()) {
        return -1;
    }
    if (nexthop_list_.size() > rhs.nexthop_list_.size()) {
        return 1;
    }
    for (size_t i = 0; i < nexthop_list_.size(); i++) {
        int cmp = nexthop_list_[i].CompareTo(rhs.nexthop_list_[i]);
        if (cmp) {
            return cmp;
        }
    }

    return 0;
}

void RibOutAttr::set_attr(const BgpTable *table, const BgpAttrPtr &attrp,
                          uint32_t label, bool vrf_originated) {
    if (!attr_out_) {
        attr_out_ = attrp;
        assert(nexthop_list_.empty());
        NextHop nexthop(table, attrp->nexthop(), label, attrp->ext_community(),
                        vrf_originated);
        nexthop_list_.push_back(nexthop);
        return;
    }

    if (!attrp) {
        clear();
        return;
    }

    bool nexthop_changed = attr_out_->nexthop() != attrp->nexthop();
    attr_out_ = attrp;

    // Update the next-hop list ? We would need label too then.
    if (nexthop_changed) {
        assert(false);
    }
}

RouteState::RouteState() {
}

//
// Move history from RouteState to RouteUpdate.
//
void RouteState::MoveHistory(RouteUpdate *rt_update) {
    AdvertiseSList adv_slist;
    SwapHistory(adv_slist);
    rt_update->SwapHistory(adv_slist);
}

//
// Find the AdvertiseInfo element with matching RibOutAttr.
//
const AdvertiseInfo *RouteState::FindHistory(
        const RibOutAttr &roattr) const {
    for (AdvertiseSList::List::const_iterator iter = advertised_->begin();
         iter != advertised_->end(); iter++) {
        if (iter->roattr == roattr) return iter.operator->();
    }
    return NULL;
}

//
// Compare AdevrtiseInfos in this RouteState with the UpdateInfo elements
// in the given list.
//
// Uses brute force since the UpdateInfo and AdvertiseInfo lists typically
// contain just a single element.
//
// Return true if the information in the RouteState is same as that in the
// UpdateInfoSList.
//
bool RouteState::CompareUpdateInfo(const UpdateInfoSList &uinfo_slist) const {
    // Both lists must have the same number of elements.
    if (uinfo_slist->size() != advertised_->size())
        return false;

    // Compare the peerset for each UpdateInfo in the UpdateInfoSList to
    // the peerset for the corresponding AdvertiseInfo in the advertised
    // list.
    for (UpdateInfoSList::List::const_iterator iter = uinfo_slist->begin();
         iter != uinfo_slist->end(); ++iter) {
        const AdvertiseInfo *ainfo = FindHistory(iter->roattr);
        if (!ainfo || iter->target != ainfo->bitset)
            return false;
    }

    return true;
}

//
// Create a new RibOut based on the BgpTable and RibExportPolicy.
//
RibOut::RibOut(BgpTable *table, SchedulingGroupManager *mgr,
               const RibExportPolicy &policy)
    : table_(table), mgr_(mgr), policy_(policy),
    listener_id_(DBTableBase::kInvalidId),
    updates_(BgpObjectFactory::Create<RibOutUpdates>(this)),
    bgp_export_(BgpObjectFactory::Create<BgpExport>(this)) {
    name_ = "RibOut";
    if (policy_.type == BgpProto::XMPP) {
        name_ += " Type: XMPP";
    } else if (policy_.type == BgpProto::IBGP) {
        name_ += " Type: IBGP";
    } else {
        name_ += " Type: EBGP";
        name_ += " (AS " + integerToString(policy_.as_number) + ")";
    }
}

//
// Destructor for RibOut.  Takes care of unregistering the RibOut from
// the DBTableBase.
//
RibOut::~RibOut() {
    if (listener_id_ != DBTableBase::kInvalidId) {
        table_->Unregister(listener_id_);
        listener_id_ = DBTableBase::kInvalidId;
    }
}

//
// Register the RibOut as a listener with the underlying DBTableBase. This
// is separated out from the constructor to let the unit testing code work
// using the bare bones RibOut functionality.
//
// Note that the corresponding unregister from the DBTableBase will happen
// implicitly from the destructor.
//
void RibOut::RegisterListener() {
    if (listener_id_ != DBTableBase::kInvalidId)
        return;
    listener_id_ = table_->Register(
        boost::bind(&BgpExport::Export, bgp_export_.get(), _1, _2),
        ToString());
}

//
// Register a new peer to the RibOut. If the peer is not present in the
// PeerStateMap, create a new PeerState and add it to the map.
//
// Also inform the SchedulingGroupManager so that the peer can be added
// to an existing SchedulingGroup or multiple SchedulingGroup could be
// merged.
//
void RibOut::Register(IPeerUpdate *peer) {
    PeerState *ps = state_map_.Locate(peer);
    assert(ps != NULL);
    active_peerset_.set(ps->index);
    mgr_->Join(this, peer);
}

//
// Unregister a IPeerUpdate from the RibOut. Removes it from the PeerStateMap.
// If this was the last IPeerUpdate in the RibOut, remove the RibOut from the
// BgpTable.  That will cause this RibOut itself to get destructed.
//
// Also inform the SchedulingGroupManager so that the peer can be removed
// from it's SchedulingGroup and the group could possibly be split.
//
void RibOut::Unregister(IPeerUpdate *peer) {
    PeerState *ps = state_map_.Find(peer);
    assert(ps != NULL);
    assert(!active_peerset_.test(ps->index));
    state_map_.Remove(peer, ps->index);
    mgr_->Leave(this, peer);

    if (state_map_.empty()) {
        table_->RibOutDelete(policy_);
    }
}

//
// Return true if the IPeerUpdate is registered to this RibOut.
//
bool RibOut::IsRegistered(IPeerUpdate *peer) {
    PeerState *ps = state_map_.Find(peer);
    return (ps != NULL);
}

//
// Deactivate a IPeerUpdate from the RibOut. Removes it from the RibPeerSet of
// active peers without removing it from the PeerStateMap.
//
// This must be called when the peer starts the process of leaving the RibOut
// in order to prevent any new or existing routes from getting exported while
// the route table walk for the leave processing is in progress.
//
void RibOut::Deactivate(IPeerUpdate *peer) {
    PeerState *ps = state_map_.Find(peer);
    assert(ps != NULL);
    assert(active_peerset_.test(ps->index));
    active_peerset_.reset(ps->index);
}

bool RibOut::IsActive(IPeerUpdate *peer) const {
    int index = GetPeerIndex(peer);
    return (index < 0 ? false : active_peerset_.test(index));
}

// Return the number of peers this route has been advertised to.
int RibOut::RouteAdvertiseCount(const BgpRoute *rt) const {
    const DBState *dbstate = rt->GetState(table_, listener_id_);
    if (dbstate == NULL) {
        return 0;
    }

    const RouteState *rstate = dynamic_cast<const RouteState *>(dbstate);
    if (rstate != NULL) {
        int count = 0;
        for (AdvertiseSList::List::const_iterator iter =
             rstate->Advertised()->begin();
             iter != rstate->Advertised()->end(); ++iter) {
            count += iter->bitset.count();
        }
        return count;
    }

    const RouteUpdate *rt_update = dynamic_cast<const RouteUpdate *>(dbstate);
    if (rt_update != NULL) {
        int count = 0;
        for (AdvertiseSList::List::const_iterator iter =
             rt_update->History()->begin();
             iter != rt_update->History()->end(); ++iter) {
            count += iter->bitset.count();
        }
        return count;
    }

    const UpdateList *uplist = dynamic_cast<const UpdateList *>(dbstate);
    if (uplist != NULL) {
        int count = 0;
        for (AdvertiseSList::List::const_iterator iter =
             uplist->History()->begin();
             iter != uplist->History()->end(); ++iter) {
            count += iter->bitset.count();
        }
        return count;
    }

    return 0;
}

//
// Return the SchedulingGroup for this RibOut.
//
SchedulingGroup *RibOut::GetSchedulingGroup() {
    return mgr_->RibOutGroup(this);
}

//
// Return the active RibPeerSet for this RibOut.  We keep track of the active
// peers via the calls to Register and Deactivate.
//
// The active RibPeerSet is always a subset of the registered RibPeerSet that
// is in the PeerStateMap.
//
const RibPeerSet &RibOut::PeerSet() const {
    return active_peerset_;
}

//
// Return the peer corresponding to the specified bit index.
//
IPeerUpdate *RibOut::GetPeer(int index) const {
    PeerState *ps = state_map_.At(index);
    if (ps != NULL) {
        return ps->peer;
    }
    return NULL;
}

//
// Return the bit index corresponding to the specified peer.
//
int RibOut::GetPeerIndex(IPeerUpdate *peer) const {
    PeerState *ps = state_map_.Find(peer);
    return (ps ? ps->index : -1);
}
