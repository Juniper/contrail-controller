/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_route.h"

#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/extended-community/default_gateway.h"
#include "bgp/extended-community/es_import.h"
#include "bgp/extended-community/esi_label.h"
#include "bgp/extended-community/etree.h"
#include "bgp/extended-community/load_balance.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/extended-community/multicast_flags.h"
#include "bgp/extended-community/router_mac.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/extended-community/source_as.h"
#include "bgp/extended-community/sub_cluster.h"
#include "bgp/extended-community/tag.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

using std::string;
using std::vector;
using std::ostringstream;

BgpRoute::BgpRoute() {
}

BgpRoute::~BgpRoute() {
    assert(GetPathList().empty());
}

//
// Return the best path for this route.
// Skip aliased paths.
//
const BgpPath *BgpRoute::BestPath() const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        if (path->GetFlags() & BgpPath::AliasedPath)
            continue;
        return path;
    }
    return NULL;
}

//
// Insert given path and redo path selection.
//
void BgpRoute::InsertPath(BgpPath *path) {
    assert(!IsDeleted());
    const Path *prev_front = front();

    BgpTable *table = static_cast<BgpTable *>(get_table());
    // 'table' is not expected to be null, suspect it is checked here because of
    // unit tests.
    if (table) {
        // Default Tunnel Encapsulation processing is done if configured for
        // the address family on the peer.
        // If routing policy is supported on the table(family)it is handled in
        // ProcessRoutingPolicy where other changes may be made to the
        // attributes. Also, when there are routing policy configuration changes
        // the attributes needs to be re-evaluated using the original attributes
        // and Default Tunnel Encapsulation also needs to be re-applied, this
        // will be handled seamlessly since ProcessRoutingPolicy will be called
        // in that case.
        // Note that currently routing policies are not supported on VPN address
        // families, however this is ensuring that it will be handed correctly
        // in future. Additionally, for labeled inet routes, routing policies
        // are supported so the Default Tunnel Encapsulation needs to be handled
        //  in ProcessRoutingPolicy.
        // If the table(family) does not support routing policies the Default
        // Tunnel Encapsulation processing is handled here. The original and
        // modified attributes are saved and are not expected to be modified
        // further. Note that the configuration is not applied if the path
        // already has tunnel encapsulation specified.

        if (table->IsRoutingPolicySupported()) {
            if (!path->IsReplicated()) {
                // Add sub-cluster extended community to all routes
                // originated within a sub-cluster
                uint32_t subcluster_id = SubClusterId();
                if (subcluster_id) {
                    path->AddExtCommunitySubCluster(subcluster_id);
                }
            }
            RoutingInstance *rtinstance = table->routing_instance();
            rtinstance->ProcessRoutingPolicy(this, path);
        } else {
            IPeer *peer = path->GetPeer();
            if (peer) {
                // Take snapshot of original attribute
                BgpAttr *out_attr = new BgpAttr(*(path->GetOriginalAttr()));
                peer->ProcessPathTunnelEncapsulation(path, out_attr,
                    table->server()->extcomm_db(), table);
                BgpAttrPtr modified_attr =
                    table->server()->attr_db()->Locate(out_attr);
                // Update the path with new set of attributes
                path->SetAttr(modified_attr, path->GetOriginalAttr());
            }
        }
    }
    insert(path);

    Sort(&BgpTable::PathSelection, prev_front);

    // Update counters.
    if (table)
        table->UpdatePathCount(path, +1);
    path->UpdatePeerRefCount(+1, table ? table->family() : Address::UNSPEC);
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
    if (table)
        table->UpdatePathCount(path, -1);
    path->UpdatePeerRefCount(-1, table ? table->family() : Address::UNSPEC);

    delete path;
}

//
// Find first path with given path source.
// Skips secondary, aliased and resolved paths.
//
const BgpPath *BgpRoute::FindPath(BgpPath::PathSource src) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        // Skip secondary paths.
        if (dynamic_cast<const BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        if (path->GetFlags() & BgpPath::AliasedPath) {
            continue;
        }
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
// Find path added by BGP_XMPP peer.
// Skips non BGP_XMPP, secondary, aliased and resolved paths.
//
BgpPath *BgpRoute::FindPath(const IPeer *peer, bool include_secondary) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetSource() != BgpPath::BGP_XMPP) {
            continue;
        }
        if (path->GetFlags() & BgpPath::AliasedPath) {
            continue;
        }
        if (path->GetFlags() & BgpPath::ResolvedPath) {
            continue;
        }
        if (!include_secondary &&
                dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }
        if (path->GetPeer() == peer) {
            return path;
        }
    }
    return NULL;
}

const BgpPath *BgpRoute::FindPath(const IPeer *peer,
                                  bool include_secondary) const {
    return const_cast<BgpRoute *>(this)->FindPath(peer, include_secondary);
}

//
// Find path with given nexthop address.
// Skips non BGP_XMPP, aliased and resolved paths.
//
BgpPath *BgpRoute::FindPath(const IpAddress &nexthop) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetSource() != BgpPath::BGP_XMPP) {
            continue;
        }
        if (path->IsResolved() || path->IsAliased()) {
            continue;
        }
        if (path->GetAttr()->nexthop() == nexthop) {
            return path;
        }
    }
    return NULL;
}

//
// Find path added by peer with given path id and path source.
// Skips secondary, aliased and resolved paths.
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
        if (path->GetFlags() & BgpPath::AliasedPath) {
            continue;
        }
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
//
// Return true if we find such a path, false otherwise.
//
// The forwarding information we look at is just the next hop. We don't
// consider the label since there's could be transient cases where we
// have 2 paths with the same next hop and different labels.  We don't
// want to treat these as unique paths.
//
bool BgpRoute::DuplicateForwardingPath(const BgpPath *in_path) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());

        // Bail if we reached the input path since the paths are sorted.
        if (path == in_path)
            return false;

        // Check the forwarding information.
        if (path->GetAttr()->nexthop() == in_path->GetAttr()->nexthop())
            return true;
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

BgpTable *BgpRoute::table() {
    return dynamic_cast<BgpTable *>(get_table_partition()->parent());
}

const BgpTable *BgpRoute::table() const {
    return dynamic_cast<BgpTable *>(get_table_partition()->parent());
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
    vector<string> cluster_list = vector<string>();
    for (vector<uint32_t>::const_iterator it = list.begin(); it != list.end();
         ++it) {
        cluster_list.push_back(Ip4Address(*it).to_string());
    }
    show_path->set_cluster_list(cluster_list);
}

static void FillRoutePathCommunityInfo(const Community *comm,
    ShowRoutePath *show_path, vector<string> *communities) {
    comm->BuildStringList(communities);
}

static void FillRoutePathExtCommunityInfo(const BgpTable *table,
    const ExtCommunity *extcomm,
    ShowRoutePath *show_path, vector<string> *communities) {
    const RoutingInstance *ri = table->routing_instance();
    const RoutingInstanceMgr *ri_mgr = ri->manager();
    vector<string> tunnel_encap = vector<string>();

    const ExtCommunity::ExtCommunityList &v = extcomm->communities();
    for (ExtCommunity::ExtCommunityList::const_iterator it = v.begin();
        it != v.end(); ++it) {
        if (ExtCommunity::is_route_target(*it)) {
            RouteTarget rt(*it);
            communities->push_back(rt.ToString());
        } else if (ExtCommunity::is_default_gateway(*it)) {
            DefaultGateway dgw(*it);
            communities->push_back(dgw.ToString());
        } else if (ExtCommunity::is_es_import(*it)) {
            EsImport es_import(*it);
            communities->push_back(es_import.ToString());
        } else if (ExtCommunity::is_esi_label(*it)) {
            EsiLabel esi_label(*it);
            communities->push_back(esi_label.ToString());
        } else if (ExtCommunity::is_mac_mobility(*it)) {
            MacMobility mm(*it);
            communities->push_back(mm.ToString());
            show_path->set_sequence_no(integerToString(mm.sequence_number()));
        } else if (ExtCommunity::is_etree(*it)) {
            ETree etree(*it);
            communities->push_back(etree.ToString());
        } else if (ExtCommunity::is_router_mac(*it)) {
            RouterMac router_mac(*it);
            communities->push_back(router_mac.ToString());
        } else if (ExtCommunity::is_origin_vn(*it)) {
            OriginVn origin_vn(*it);
            communities->push_back(origin_vn.ToString());
            int vn_index = origin_vn.vn_index();
            show_path->set_origin_vn(
                ri_mgr->GetVirtualNetworkByVnIndex(vn_index));
        } else if (ExtCommunity::is_security_group(*it)) {
            SecurityGroup sg(*it);
            communities->push_back(sg.ToString());
        } else if (ExtCommunity::is_security_group4(*it)) {
            SecurityGroup4ByteAs sg(*it);
            communities->push_back(sg.ToString());
        } else if (ExtCommunity::is_site_of_origin(*it)) {
            SiteOfOrigin soo(*it);
            communities->push_back(soo.ToString());
        } else if (ExtCommunity::is_tunnel_encap(*it)) {
            TunnelEncap encap(*it);
            communities->push_back(encap.ToString());
            TunnelEncapType::Encap id = encap.tunnel_encap();
            tunnel_encap.push_back(TunnelEncapType::TunnelEncapToString(id));
        } else if (ExtCommunity::is_load_balance(*it)) {
            LoadBalance load_balance(*it);
            communities->push_back(load_balance.ToString());

            ShowLoadBalance show_load_balance;
            load_balance.ShowAttribute(&show_load_balance);
            show_path->set_load_balance(show_load_balance);
        } else if (ExtCommunity::is_tag(*it)) {
            Tag tag(*it);
            communities->push_back(tag.ToString());
        } else if (ExtCommunity::is_tag4(*it)) {
            Tag4ByteAs tag(*it);
            communities->push_back(tag.ToString());
        } else if (ExtCommunity::is_source_as(*it)) {
            SourceAs sas(*it);
            communities->push_back(sas.ToString());
        } else if (ExtCommunity::is_sub_cluster(*it)) {
            SubCluster sc(*it);
            communities->push_back(sc.ToString());
        } else if (ExtCommunity::is_vrf_route_import(*it)) {
            VrfRouteImport rt_import(*it);
            communities->push_back(rt_import.ToString());
        } else if (ExtCommunity::is_multicast_flags(*it)) {
            MulticastFlags mf(*it);
            communities->push_back(mf.ToString());
        } else {
            char temp[50];
            int len = snprintf(temp, sizeof(temp), "ext community: ");
            for (size_t i = 0; i < it->size(); i++) {
                len += snprintf(temp+len, sizeof(temp) - len, "%02x", (*it)[i]);
            }
            communities->push_back(string(temp));
        }
    }
    show_path->set_tunnel_encap(tunnel_encap);
}

static void FillEdgeForwardingInfo(const EdgeForwarding *edge_forwarding,
    ShowRoutePath *show_path) {
    vector<ShowEdgeForwarding> show_ef_list;
    vector<EdgeForwardingSpec::Edge *> edge_list =
        edge_forwarding->edge_forwarding().edge_list;
    for (vector<EdgeForwardingSpec::Edge *>::const_iterator it =
            edge_list.begin(); it != edge_list.end(); ++it) {
        const EdgeForwardingSpec::Edge *edge = *it;
        ShowEdgeForwarding show_ef;
        ostringstream oss;
        oss << edge->GetInboundIp4Address() << ":" << edge->inbound_label;
        show_ef.set_in_address_label(oss.str());
        oss.str("");
        oss.clear();
        oss << edge->GetOutboundIp4Address() << ":" << edge->outbound_label;
        show_ef.set_out_address_label(oss.str());
        show_ef_list.push_back(show_ef);
    }
    show_path->set_edge_forwarding(show_ef_list);
}

static void FillEdgeDiscoveryInfo(const EdgeDiscovery *edge_discovery,
    ShowRoutePath *show_path) {
    vector<ShowEdgeDiscovery> show_ed_list;
    vector<EdgeDiscoverySpec::Edge *> edge_list =
        edge_discovery->edge_discovery().edge_list;
    int idx = 0;
    for (vector<EdgeDiscoverySpec::Edge *>::const_iterator it =
            edge_list.begin();
         it != edge_list.end(); ++it, ++idx) {
        const EdgeDiscoverySpec::Edge *edge = *it;
        ShowEdgeDiscovery show_ed;
        ostringstream oss;
        uint32_t first_label, last_label;
        oss << edge->GetIp4Address();
        show_ed.set_address(oss.str());
        oss.str("");
        oss.clear();
        edge->GetLabels(&first_label, &last_label);
        oss << first_label << "-" << last_label;
        show_ed.set_labels(oss.str());
        show_ed_list.push_back(show_ed);
    }
    show_path->set_edge_discovery(show_ed_list);
}

static void FillOriginVnPathInfo(const OriginVnPath *ovnpath,
    ShowRoutePath *show_path) {
    const OriginVnPath::OriginVnList &v = ovnpath->origin_vns();
    vector<string> origin_vn_path = vector<string>();
    for (OriginVnPath::OriginVnList::const_iterator it = v.begin();
         it != v.end(); ++it) {
        OriginVn origin_vn(*it);
        origin_vn_path.push_back(origin_vn.ToString());
    }
    show_path->set_origin_vn_path(origin_vn_path);
}

static void FillPmsiTunnelInfo(const PmsiTunnel *pmsi_tunnel,
    const ExtCommunity *ext, ShowRoutePath *show_path) {
    ShowPmsiTunnel spt;
    spt.set_type(pmsi_tunnel->pmsi_tunnel().GetTunnelTypeString());
    spt.set_ar_type(pmsi_tunnel->pmsi_tunnel().GetTunnelArTypeString());
    spt.set_identifier(pmsi_tunnel->identifier().to_string());
    spt.set_label(pmsi_tunnel->GetLabel(ext));
    spt.set_flags(pmsi_tunnel->pmsi_tunnel().GetTunnelFlagsStrings());
    show_path->set_pmsi_tunnel(spt);
}

void BgpRoute::FillRouteInfo(const BgpTable *table,
    ShowRoute *show_route, const string &source, const string &protocol) const {
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

        // Filter against peer source, if specified.
        if (!source.empty() && (!peer || source != peer->ToString()))
            continue;

        // Filter against path protocol, if specified.
        if (!protocol.empty() && protocol != path->GetSourceString())
            continue;

        if (peer) {
            srp.set_source(peer->ToString());
        }

        const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
        if (bgp_peer) {
            srp.set_local_as(bgp_peer->local_as());
            srp.set_peer_as(bgp_peer->peer_as());
            srp.set_peer_router_id(bgp_peer->bgp_identifier_string());
        }

        const BgpAttr *attr = path->GetAttr();
        if (attr->edge_forwarding()) {
            FillEdgeForwardingInfo(attr->edge_forwarding(), &srp);
        }
        if (attr->edge_discovery()) {
            FillEdgeDiscoveryInfo(attr->edge_discovery(), &srp);
        }
        if (attr->sub_protocol().empty()) {
            srp.set_protocol(path->GetSourceString());
        } else {
            const string sbp = attr->sub_protocol();
            srp.set_protocol(path->GetSourceString() + " (" + sbp + ")");
        }
        srp.set_origin(attr->origin_string());
        if (attr->as_path() != NULL)
            srp.set_as_path(attr->as_path()->path().ToString());
        else if (attr->aspath_4byte() != NULL)
            srp.set_as_path(attr->aspath_4byte()->path().ToString());
        if (attr->as4_path() != NULL)
            srp.set_as4_path(attr->as4_path()->path().ToString());
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
        vector<string> communities;
        if (attr->community()) {
            FillRoutePathCommunityInfo(attr->community(), &srp, &communities);
        }
        if (attr->ext_community()) {
            FillRoutePathExtCommunityInfo(table, attr->ext_community(), &srp,
                    &communities);
        }
        if (!communities.empty()){
            srp.set_communities(communities);
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
            if (extcomm) {
                FillPmsiTunnelInfo(attr->pmsi_tunnel(), extcomm, &srp);
            }
        }
        if (attr->originator_id().to_ulong()) {
            srp.set_originator_id(attr->originator_id().to_string());
        }

        show_route_paths.push_back(srp);
    }
    show_route->set_paths(show_route_paths);
}

void BgpRoute::NotifyOrDelete() {
    if (!front()) {
        Delete();
    } else {
        Notify();
    }
}

uint32_t BgpRoute::SubClusterId() const {
    BgpTable *table = static_cast<BgpTable *>(get_table());
    const BgpConfigManager *config_manager_ = table->server()->config_manager();
    if (!config_manager_) {
        return 0;
    }
    const BgpProtocolConfig *proto =
        config_manager_->GetProtocolConfig(BgpConfigManager::kMasterInstance);
    if (!proto) {
        return 0;
    }
    return proto->subcluster_id();
}
