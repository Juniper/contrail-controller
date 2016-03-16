/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_route.h"

#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/extended-community/default_gateway.h"
#include "bgp/extended-community/es_import.h"
#include "bgp/extended-community/esi_label.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

using std::string;
using std::vector;

BgpRoute::BgpRoute() {
}

BgpRoute::~BgpRoute() {
    assert(GetPathList().empty());
}

//
// Return the best path for this route.
//
const BgpPath *BgpRoute::BestPath() const {
    const BgpPath *path = static_cast<const BgpPath *>(front());
    return path;
}

//
// Insert given path and redo path selection.
//
void BgpRoute::InsertPath(BgpPath *path) {
    assert(!IsDeleted());
    const Path *prev_front = front();

    BgpTable *table = static_cast<BgpTable *>(get_table());
    if (table && table->IsRoutingPolicySupported()) {
        RoutingInstance *rtinstance = table->routing_instance();
        rtinstance->ProcessRoutingPolicy(this, path);
    }
    insert(path);

    Sort(&BgpTable::PathSelection, prev_front);

    // Update counters.
    if (table) table->UpdatePathCount(path, +1);
    path->UpdatePeerRefCount(+1);
}

//
// Delete given path and redo path selection.
//
void BgpRoute::DeletePath(BgpPath *path) {
    const Path *prev_front = front();

    remove(path);
    Sort(&BgpTable::PathSelection, prev_front);

    // Update counters.
    BgpTable *table = static_cast<BgpTable *>(get_table());
    if (table) table->UpdatePathCount(path, -1);
    path->UpdatePeerRefCount(-1);

    delete path;
}

//
// Find first path with given path source.
// Skips secondary paths and resolved paths.
//
const BgpPath *BgpRoute::FindPath(BgpPath::PathSource src) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        // Skip secondary paths.
        if (dynamic_cast<const BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        if (path->GetFlags() & BgpPath::ResolvedPath) {
            continue;
        }
        if (path->GetSource() == src) {
            return path;
        }
    }
    return NULL;
}

//
// Find path added by peer with given path id and path source.
// Skips secondary paths and resolved paths.
//
BgpPath *BgpRoute::FindPath(BgpPath::PathSource src, const IPeer *peer,
                            uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        // Skip secondary paths.
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetFlags() & BgpPath::ResolvedPath) {
            continue;
        }
        if (path->GetPeer() == peer && path->GetPathId() == path_id &&
            path->GetSource() == src) {
            return path;
        }
    }
    return NULL;
}

//
// Find path added given source and path id - peer must be NULL.
// Skips secondary paths and resolved paths.
//
BgpPath *BgpRoute::FindPath(BgpPath::PathSource src, uint32_t path_id) {
    return FindPath(src, NULL, path_id);
}

//
// Remove path added by peer with given path id and source.
// Skips secondary paths.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemovePath(BgpPath::PathSource src, const IPeer *peer,
                          uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
         BgpPath *path = static_cast<BgpPath *>(it.operator->());

        //
        // Skip secondary paths.
        //
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        if (path->GetPeer() == peer && path->GetPathId() == path_id &&
            path->GetSource() == src) {
            DeletePath(path);
            return true;
        }
    }
    return false;
}

//
// Remove path added given source and path id - peer must be NULL.
// Skips secondary paths.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemovePath(BgpPath::PathSource src, uint32_t path_id) {
    return RemovePath(src, NULL, path_id);
}

//
// Remove path added by peer with given path id and source.
// Skips secondary paths.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemovePath(const IPeer *peer) {
    bool ret = false;

    for (Route::PathList::iterator it = GetPathList().begin(), next = it;
         it != GetPathList().end(); it = next) {
        next++;
        BgpPath *path = static_cast<BgpPath *>(it.operator->());

        //
        // Skip secondary paths.
        //
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        if (path->GetPeer() == peer) {
            DeletePath(path);
            ret = true;
        }
    }
    return ret;
}

//
// Check if the route is usable.
//
bool BgpRoute::IsUsable() const {
    if (IsDeleted())
        return false;

    const BgpPath *path = BestPath();
    if (!path || !path->IsFeasible())
        return false;

    return true;
}

//
// Check if the route is valid.
//
bool BgpRoute::IsValid() const {
    return IsUsable();
}

//
// Check if there's a better path with the same forwarding information.
// The forwarding information we look at is the label and the next hop.
// Return true if we find such a path, false otherwise.
//
bool BgpRoute::DuplicateForwardingPath(const BgpPath *in_path) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());

        // Bail if we reached the input path since the paths are sorted.
        if (path == in_path)
            return false;

        // Check the forwarding information.
        if ((path->GetAttr()->nexthop() == in_path->GetAttr()->nexthop()) &&
            (path->GetLabel() == in_path->GetLabel())) {
            return true;
        }
    }

    return false;
}

//
// Find the secondary path matching secondary replicated info.
//
BgpPath *BgpRoute::FindSecondaryPath(BgpRoute *src_rt,
        BgpPath::PathSource src, const IPeer *peer, uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        BgpSecondaryPath *path = dynamic_cast<BgpSecondaryPath *>(
            it.operator->());
        if (path && path->src_rt() == src_rt &&
            path->GetPeer() == peer && path->GetPathId() == path_id &&
            path->GetSource() == src) {
            return path;
        }
    }
    return NULL;
}

//
// Remove the secondary path matching secondary replicated info.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemoveSecondaryPath(const BgpRoute *src_rt,
        BgpPath::PathSource src, const IPeer *peer, uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
         BgpSecondaryPath *path =
            dynamic_cast<BgpSecondaryPath *>(it.operator->());
        if (path && path->src_rt() == src_rt &&
            path->GetPeer() == peer && path->GetPathId() == path_id &&
            path->GetSource() == src) {
            DeletePath(path);
            return true;
        }
    }

    return false;
}

size_t BgpRoute::count() const {
    return GetPathList().size();
}

void BgpRoute::FillRouteInfo(const BgpTable *table,
    ShowRouteBrief *show_route) const {
    show_route->set_prefix(ToString());
    vector<ShowRoutePathBrief> show_route_paths;
    for (Route::PathList::const_iterator it = GetPathList().begin();
        it != GetPathList().end(); ++it) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        ShowRoutePathBrief srp;
        const IPeer *peer = path->GetPeer();
        if (peer) {
            srp.set_source(peer->ToString());
        }

        srp.set_protocol(path->GetSourceString());

        const BgpAttr *attr = path->GetAttr();
        srp.set_local_preference(attr->local_pref());
        srp.set_med(attr->med());
        srp.set_next_hop(attr->nexthop().to_string());
        srp.set_label(path->GetLabel());
        show_route_paths.push_back(srp);
    }
    show_route->set_paths(show_route_paths);
}

static void FillRoutePathClusterListInfo(const ClusterList *clist,
    ShowRoutePath *show_path) {
    const vector<uint32_t> &list = clist->cluster_list().cluster_list;
    for (vector<uint32_t>::const_iterator it = list.begin(); it != list.end();
         ++it) {
        show_path->cluster_list.push_back(Ip4Address(*it).to_string());
    }
}

static void FillRoutePathCommunityInfo(const Community *comm,
    ShowRoutePath *show_path) {
    comm->BuildStringList(&show_path->communities);
}

static void FillRoutePathExtCommunityInfo(const BgpTable *table,
    const ExtCommunity *extcomm,
    ShowRoutePath *show_path) {
    const RoutingInstance *ri = table->routing_instance();
    const RoutingInstanceMgr *ri_mgr = ri->manager();

    const ExtCommunity::ExtCommunityList &v = extcomm->communities();
    for (ExtCommunity::ExtCommunityList::const_iterator it = v.begin();
        it != v.end(); ++it) {
        if (ExtCommunity::is_route_target(*it)) {
            RouteTarget rt(*it);
            show_path->communities.push_back(rt.ToString());
        } else if (ExtCommunity::is_default_gateway(*it)) {
            DefaultGateway dgw(*it);
            show_path->communities.push_back(dgw.ToString());
        } else if (ExtCommunity::is_es_import(*it)) {
            EsImport es_import(*it);
            show_path->communities.push_back(es_import.ToString());
        } else if (ExtCommunity::is_esi_label(*it)) {
            EsiLabel esi_label(*it);
            show_path->communities.push_back(esi_label.ToString());
        } else if (ExtCommunity::is_mac_mobility(*it)) {
            MacMobility mm(*it);
            show_path->communities.push_back(mm.ToString());
            show_path->set_sequence_no(mm.ToString());
        } else if (ExtCommunity::is_origin_vn(*it)) {
            OriginVn origin_vn(*it);
            show_path->communities.push_back(origin_vn.ToString());
            int vn_index = origin_vn.vn_index();
            show_path->set_origin_vn(
                ri_mgr->GetVirtualNetworkByVnIndex(vn_index));
        } else if (ExtCommunity::is_security_group(*it)) {
            SecurityGroup sg(*it);
            show_path->communities.push_back(sg.ToString());
        } else if (ExtCommunity::is_route_target(*it)) {
            SiteOfOrigin soo(*it);
            show_path->communities.push_back(soo.ToString());
        } else if (ExtCommunity::is_tunnel_encap(*it)) {
            TunnelEncap encap(*it);
            show_path->communities.push_back(encap.ToString());
            TunnelEncapType::Encap id = encap.tunnel_encap();
            show_path->tunnel_encap.push_back(
                TunnelEncapType::TunnelEncapToString(id));
        } else if (ExtCommunity::is_load_balance(*it)) {
            LoadBalance load_balance(*it);
            show_path->communities.push_back(load_balance.ToString());
            load_balance.ShowAttribute(&show_path->load_balance);
        } else {
            char temp[50];
            int len = snprintf(temp, sizeof(temp), "ext community: ");
            for (size_t i = 0; i < it->size(); i++) {
                len += snprintf(temp+len, sizeof(temp) - len, "%02x", (*it)[i]);
            }
            show_path->communities.push_back(string(temp));
        }
    }
}

static void FillOriginVnPathInfo(const OriginVnPath *ovnpath,
    ShowRoutePath *show_path) {
    const OriginVnPath::OriginVnList &v = ovnpath->origin_vns();
    for (OriginVnPath::OriginVnList::const_iterator it = v.begin();
         it != v.end(); ++it) {
        OriginVn origin_vn(*it);
        show_path->origin_vn_path.push_back(origin_vn.ToString());
    }
}

static void FillPmsiTunnelInfo(const PmsiTunnel *pmsi_tunnel, bool label_is_vni,
    ShowRoutePath *show_path) {
    ShowPmsiTunnel spt;
    spt.set_type(pmsi_tunnel->pmsi_tunnel().GetTunnelTypeString());
    spt.set_ar_type(pmsi_tunnel->pmsi_tunnel().GetTunnelArTypeString());
    spt.set_identifier(pmsi_tunnel->identifier().to_string());
    spt.set_label(pmsi_tunnel->GetLabel(label_is_vni));
    spt.set_flags(pmsi_tunnel->pmsi_tunnel().GetTunnelFlagsStrings());
    show_path->set_pmsi_tunnel(spt);
}

void BgpRoute::FillRouteInfo(const BgpTable *table,
    ShowRoute *show_route) const {
    const RoutingInstance *ri = table->routing_instance();

    show_route->set_prefix(ToString());
    show_route->set_last_modified(
        integerToString(UTCUsecToPTime(last_change_at())));

    vector<ShowRoutePath> show_route_paths;
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        ShowRoutePath srp;
        const IPeer *peer = path->GetPeer();
        if (peer) {
            srp.set_source(peer->ToString());
        }

        srp.set_protocol(path->GetSourceString());

        const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
        if (bgp_peer) {
            srp.set_local_as(bgp_peer->local_as());
            srp.set_peer_as(bgp_peer->peer_as());
            srp.set_peer_router_id(bgp_peer->bgp_identifier_string());
        }

        const BgpAttr *attr = path->GetAttr();
        if (attr->as_path() != NULL)
            srp.set_as_path(attr->as_path()->path().ToString());
        srp.set_local_preference(attr->local_pref());
        srp.set_med(attr->med());
        srp.set_next_hop(attr->nexthop().to_string());
        srp.set_label(path->GetLabel());
        srp.set_flags(path->GetFlagsStringList());
        srp.set_last_modified(
            integerToString(UTCUsecToPTime(path->time_stamp_usecs())));
        if (path->IsReplicated()) {
            const BgpSecondaryPath *replicated;
            replicated = static_cast<const BgpSecondaryPath *>(path);
            srp.set_replicated(true);
            srp.set_primary_table(replicated->src_table()->name());
        } else {
            srp.set_replicated(false);
            Address::Family vpn_family =
                Address::VpnFamilyFromFamily(table->family());
            const RoutePathReplicator *replicator =
                table->server()->replicator(vpn_family);
            if (replicator) {
                srp.set_secondary_tables(
                    replicator->GetReplicatedTableNameList(table, this, path));
            }
        }
        if (attr->cluster_list()) {
            FillRoutePathClusterListInfo(attr->cluster_list(), &srp);
        }
        if (attr->community()) {
            FillRoutePathCommunityInfo(attr->community(), &srp);
        }
        if (attr->ext_community()) {
            FillRoutePathExtCommunityInfo(table, attr->ext_community(), &srp);
        }
        if (srp.get_origin_vn().empty() &&
            !table->IsVpnTable() && path->IsVrfOriginated()) {
            srp.set_origin_vn(ri->GetVirtualNetworkName());
        }
        if (attr->origin_vn_path()) {
            FillOriginVnPathInfo(attr->origin_vn_path(), &srp);
        }
        if (attr->pmsi_tunnel()) {
            const ExtCommunity *extcomm = attr->ext_community();
            bool label_is_vni =  extcomm && extcomm->ContainsTunnelEncapVxlan();
            FillPmsiTunnelInfo(attr->pmsi_tunnel(), label_is_vni, &srp);
        }
        show_route_paths.push_back(srp);
    }
    show_route->set_paths(show_route_paths);
}
