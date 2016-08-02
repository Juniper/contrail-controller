/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/rtarget_group.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include <utility>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_table.h"
#include "bgp/rtarget/rtarget_route.h"
#include "db/db.h"

using boost::assign::list_of;
using std::pair;
using std::string;
using std::vector;

RtGroup::RtGroup(const RouteTarget &rt)
    : rt_(rt), dep_(RTargetDepRouteList(DB::PartitionCount())) {
    vector<Address::Family> vpn_family_list = list_of
        (Address::INETVPN)(Address::INET6VPN)(Address::ERMVPN)(Address::EVPN);
    BOOST_FOREACH(Address::Family vpn_family, vpn_family_list) {
        import_[vpn_family] = RtGroupMemberList();
        export_[vpn_family] = RtGroupMemberList();
    }
}

bool RtGroup::MayDelete() const {
    return !HasImportExportTables() && !HasInterestedPeers() && !HasDepRoutes();
}

const RtGroup::RtGroupMemberList &RtGroup::GetImportTables(
    Address::Family family) const {
    return import_.at(family);
}

const RtGroup::RtGroupMemberList &RtGroup::GetExportTables(
    Address::Family family) const {
    return export_.at(family);
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

bool RtGroup::HasImportExportTables() const {
    BOOST_FOREACH(const RtGroupMembers::value_type &family_members, import_) {
        if (!family_members.second.empty())
            return true;
    }
    BOOST_FOREACH(const RtGroupMembers::value_type &family_members, export_) {
        if (!family_members.second.empty())
            return true;
    }
    return false;
}

bool RtGroup::HasVrfTables(Address::Family family) const {
    const BgpTable *table;

    const RtGroupMemberList &import_list = import_.at(family);
    if (import_list.size() > 1)
        return true;
    table = *import_list.begin();
    if (table && !table->IsVpnTable())
        return true;

    const RtGroupMemberList &export_list = export_.at(family);
    if (export_list.size() > 1)
        return true;
    table = *export_list.begin();
    if (table && !table->IsVpnTable())
        return true;

    return false;
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

void RtGroup::NotifyDepRoutes(int part_id) {
    BOOST_FOREACH(BgpRoute *route, dep_[part_id]) {
        DBTablePartBase *dbpart = route->get_table_partition();
        dbpart->Notify(route);
    }
}

bool RtGroup::HasDepRoutes() const {
    for (RTargetDepRouteList::const_iterator it = dep_.begin();
         it != dep_.end(); ++it) {
        if (!it->empty()) {
            return true;
        }
    }
    return false;
}

const RtGroupInterestedPeerSet& RtGroup::GetInterestedPeers() const {
    return interested_peers_;
}

void RtGroup::AddInterestedPeer(const BgpPeer *peer, RTargetRoute *rt) {
    InterestedPeerList::iterator it = peer_list_.find(peer);
    if (it == peer_list_.end()) {
        it = peer_list_.insert(peer_list_.begin(),
            pair<const BgpPeer *, RTargetRouteList>(peer, RTargetRouteList()));
        assert(peer->GetIndex() >= 0);
        interested_peers_.set(peer->GetIndex());
    }
    it->second.insert(rt);
}

void RtGroup::RemoveInterestedPeer(const BgpPeer *peer, RTargetRoute *rt) {
    InterestedPeerList::iterator it = peer_list_.find(peer);
    if (it == peer_list_.end()) return;
    it->second.erase(rt);
    if (it->second.empty()) {
        peer_list_.erase(peer);
        assert(peer->GetIndex() >= 0);
        interested_peers_.reset(peer->GetIndex());
    }
}

bool RtGroup::HasInterestedPeers() const {
    return !peer_list_.empty();
}

bool RtGroup::HasInterestedPeer(const string &name) const {
    BOOST_FOREACH(const InterestedPeerList::value_type &peer, peer_list_) {
        if (peer.first->peer_basename() == name)
            return true;
    }
    return false;
}

void RtGroup::FillMemberTables(const RtGroupMembers &rt_members,
    vector<ShowRtGroupMemberTableList> *member_list) const {
    BOOST_FOREACH(const RtGroupMembers::value_type &rt_tables, rt_members) {
        ShowRtGroupMemberTableList member;
        vector<string> table_names;
        BOOST_FOREACH(BgpTable *table, rt_tables.second) {
            table_names.push_back(table->name());
        }
        member.set_family(Address::FamilyToString(rt_tables.first));
        member.set_tables(table_names);
        member_list->push_back(member);
    }
}

void RtGroup::FillInterestedPeers(vector<string> *interested_peers) const {
    BOOST_FOREACH(const InterestedPeerList::value_type &peer, peer_list_) {
        interested_peers->push_back(peer.first->peer_basename());
    }
}

void RtGroup::FillDependentRoutes(vector<string> *rtlist) const {
    for (RTargetDepRouteList::const_iterator dep_it = dep_.begin();
         dep_it != dep_.end(); ++dep_it) {
        for (RouteList::const_iterator dep_rt_it = dep_it->begin();
             dep_rt_it != dep_it->end(); ++dep_rt_it) {
            rtlist->push_back((*dep_rt_it)->ToString());
        }
    }
}

void RtGroup::FillShowInfoCommon(ShowRtGroupInfo *info,
    bool fill_peers, bool fill_routes) const {
    info->set_rtarget(ToString());

    vector<ShowRtGroupMemberTableList> import_members;
    FillMemberTables(import_, &import_members);
    info->set_import_members(import_members);
    vector<ShowRtGroupMemberTableList> export_members;
    FillMemberTables(export_, &export_members);
    info->set_export_members(export_members);

    if (fill_peers) {
        vector<string> interested_peers;
        FillInterestedPeers(&interested_peers);
        info->set_peers_interested(interested_peers);
    }
    if (fill_routes) {
        vector<string> rtlist;
        FillDependentRoutes(&rtlist);
        info->set_dep_route(rtlist);
    }
}

void RtGroup::FillShowInfo(ShowRtGroupInfo *info) const {
    FillShowInfoCommon(info, true, true);
}

void RtGroup::FillShowSummaryInfo(ShowRtGroupInfo *info) const {
    FillShowInfoCommon(info, true, false);
}

void RtGroup::FillShowPeerInfo(ShowRtGroupInfo *info) const {
    FillShowInfoCommon(info, false, true);
}
