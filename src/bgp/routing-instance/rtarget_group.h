/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_H_
#define SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/bitset.h"
#include "base/util.h"
#include "bgp/rtarget/rtarget_address.h"
#include "net/address.h"

class BgpPeer;
class BgpRoute;
class BgpTable;
class RTargetRoute;
class ShowRtGroupMemberTableList;
class ShowRtGroupInfo;

class RtGroupInterestedPeerSet : public BitSet {
};

//
// This class keeps track of state per RouteTarget.  It maintains three main
// pieces of information.
//
// 1. The RtGroupMembers map and the RtGroupMemberList set are used to keep
//    per address family lists of import and export BgpTables. The lists are
//    updated from the RoutePathReplicator.
//
// 2. The RTargetDepRouteList and the RouteList are used to maintain a per
//    partition list of dependent BgpRoutes i.e. routes with the RouteTarget
//    as one of their route targets.  Each entry in the RTargetDepRouteList
//    vector corresponds to a DB partition.
//
//    The list of dependent BgpRoutes is updated from DB task when handling
//    notifications for BgpRoutes in various VPN tables e.g. bgp.l3vpn.0 and
//    bgp.evpn.0.  Since we could be processing multiple notifications (each
//    from a different partition) in parallel, we keep a separate RouteList
//    per partition id. The RouteLists are accessed from the RTFilter task
//    to trigger re-evaluation of export policy for dependent BgpRoutes based
//    on receiving advertisements/withdrawals for RTargetRoutes.
//
// 3. The InterestedPeerList map and RTargetRouteList set are used to keep a
//    list of peers interested in this RouteTarget.  Since a peer could send
//    multiple RTargetRoutes for a given RouteTarget, we maintain a set of
//    RTargetRoutes per peer. A peer is added to the InterestedPeerList map
//    when the first RTargetRoute is added and removed from the map when the
//    last RTargetRoute is removed.
//
// Note that this class does not take any references on dependent BgpRoutes
// or RTargetRoutes.  It is the RTargetGroupManager's job to do that.  Note
// that each dependent BgpRoute may have multiple RouteTargets, so it doesn't
// make sense to take a reference to the dependent route for each RouteTarget.
//
class RtGroup {
public:
    typedef std::set<BgpTable *> RtGroupMemberList;
    typedef std::map<Address::Family, RtGroupMemberList> RtGroupMembers;
    typedef std::set<BgpRoute *> RouteList;
    typedef std::vector<RouteList> RTargetDepRouteList;
    typedef std::set<RTargetRoute *> RTargetRouteList;
    typedef std::map<const BgpPeer *, RTargetRouteList> InterestedPeerList;

    explicit RtGroup(const RouteTarget &rt);
    const RouteTarget &rt();
    std::string ToString() const { return rt_.ToString(); }
    bool MayDelete() const;

    const RtGroupMemberList GetImportTables(Address::Family family) const;
    const RtGroupMemberList GetExportTables(Address::Family family) const;

    bool AddImportTable(Address::Family family, BgpTable *tbl);
    bool AddExportTable(Address::Family family, BgpTable *tbl);
    bool RemoveImportTable(Address::Family family, BgpTable *tbl);
    bool RemoveExportTable(Address::Family family, BgpTable *tbl);
    bool HasImportExportTables(Address::Family family) const;
    bool HasImportExportTables() const;
    bool HasVrfTables(Address::Family family) const;

    void AddDepRoute(int part_id, BgpRoute *rt);
    void RemoveDepRoute(int part_id, BgpRoute *rt);
    void NotifyDepRoutes(int part_id);
    bool HasDepRoutes() const;

    const RtGroupInterestedPeerSet &GetInterestedPeers() const;
    void AddInterestedPeer(const BgpPeer *peer, RTargetRoute *rt);
    void RemoveInterestedPeer(const BgpPeer *peer, RTargetRoute *rt);
    bool HasInterestedPeers() const;
    bool HasInterestedPeer(const std::string &name) const;

    void FillShowInfo(ShowRtGroupInfo *info) const;
    void FillShowPeerInfo(ShowRtGroupInfo *info) const;
    void FillShowSummaryInfo(ShowRtGroupInfo *info) const;

private:
    void FillMemberTables(const RtGroupMembers &rt_members,
        std::vector<ShowRtGroupMemberTableList> *member_list) const;
    void FillInterestedPeers(std::vector<std::string> *interested_peers) const;
    void FillDependentRoutes(std::vector<std::string> *rtlist) const;
    void FillShowInfoCommon(
        ShowRtGroupInfo *info, bool fill_peers, bool fill_routes) const;

    RouteTarget rt_;
    RtGroupMembers import_;
    RtGroupMembers export_;
    RTargetDepRouteList dep_;
    InterestedPeerList peer_list_;
    RtGroupInterestedPeerSet interested_peers_;

    DISALLOW_COPY_AND_ASSIGN(RtGroup);
};

#endif  // SRC_BGP_ROUTING_INSTANCE_RTARGET_GROUP_H_
