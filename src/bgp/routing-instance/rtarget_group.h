/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_rtarget_group_h
#define ctrlplane_rtarget_group_h

#include <list>
#include <set>
#include <vector>

#include "bgp/bgp_table.h"
#include "bgp/rtarget/rtarget_address.h"

// RouteTarget Group for a given RouteTarget
// Contains two lists of tables 
//       1. Tables that imports the route belonging to this RouteTarget
//       2. Tables to which route needs to be exported

class BgpRoute;
class RTargetRoute;

class RtGroup {
public:
    typedef std::list<BgpTable *> RtGroupMemberList;
    typedef std::map<Address::Family, RtGroupMemberList> RtGroupMembers;
    typedef std::set<BgpRoute *> RouteList;
    typedef std::vector<RouteList> RTargetDepRouteList;
    typedef std::set<RTargetRoute *> RTargetRouteList;
    typedef std::map<const BgpPeer *, RTargetRouteList> InterestedPeerList;

    RtGroup(const RouteTarget &rt);
    const RouteTarget &rt();
    bool empty(Address::Family family);

    const RtGroupMemberList &GetImportTables(Address::Family family);
    const RtGroupMemberList &GetExportTables(Address::Family family);

    bool AddImportTable(Address::Family family, BgpTable *tbl);
    bool AddExportTable(Address::Family family, BgpTable *tbl);
    bool RemoveImportTable(Address::Family family, BgpTable *tbl);
    bool RemoveExportTable(Address::Family family, BgpTable *tbl);


    void AddDepRoute(int part_id, BgpRoute *rt);
    void RemoveDepRoute(int part_id, BgpRoute *rt);
    bool RouteDepListEmpty() const;
    const RTargetDepRouteList &DepRouteList() const;

    const InterestedPeerList &PeerList() const;
    void GetInterestedPeers(std::set<const BgpPeer *> &peer_set) const;
    void AddInterestedPeer(const BgpPeer *peer, RTargetRoute *rt);
    void RemoveInterestedPeer(const BgpPeer *peer, RTargetRoute *rt);
    bool IsPeerInterested(const BgpPeer *peer) const;
    bool peer_list_empty() const;

private:
    RtGroupMembers import_;
    RtGroupMembers export_;
    RouteTarget rt_;
    RTargetDepRouteList dep_;
    InterestedPeerList peer_list_;
    DISALLOW_COPY_AND_ASSIGN(RtGroup);
};

#endif
