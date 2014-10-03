/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_route.h"

#include <boost/date_time/posix_time/posix_time.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "bgp/bgp_attr.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_table.h"
#include "bgp/extended-community/mac_mobility.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"


BgpRoute::BgpRoute() {
}

BgpRoute::~BgpRoute() {
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
    const Path *prev_front = front();

    insert(path);

    Sort(&BgpTable::PathSelection, prev_front);

    // Update counters.
    BgpTable *table = static_cast<BgpTable *>(get_table());
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
// Find path added by peer with given path id and path source.
// Skips secondary paths.
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
        if (path->GetPeer() == peer && path->GetPathId() == path_id &&
            path->GetSource() == src) {
            return path;
        }
    }
    return NULL;
}

//
// Remove path added by peer with given path id and source.
// Skips secondary paths.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemovePath(BgpPath::PathSource src,const IPeer *peer,
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
// Check if the route is valid.
//
bool BgpRoute::IsValid() const {
    if (IsDeleted())
        return false;

    const BgpPath *path = BestPath();
    if (!path || !path->IsFeasible())
        return false;

    return true;
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

void BgpRoute::FillRouteInfo(BgpTable *table, ShowRoute *show_route) {
    const RoutingInstanceMgr *ri_mgr = table->routing_instance()->manager();

    show_route->set_prefix(ToString());
    show_route->set_last_modified(integerToString(UTCUsecToPTime(last_change_at())));

    std::vector<ShowRoutePath> show_route_paths;
    for(Route::PathList::const_iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        ShowRoutePath srp;
        const IPeer *peer = path->GetPeer();
        if (peer) {
            srp.set_source(peer->ToString());
        }

        if (path->GetSource() == BgpPath::BGP_XMPP) {
            if (peer)
                srp.set_protocol(peer->IsXmppPeer() ? "XMPP" : "BGP");
            else
                srp.set_protocol("Local");
        } else if (path->GetSource() == BgpPath::ServiceChain) {
            srp.set_protocol("ServiceChain");
        } else if (path->GetSource() == BgpPath::StaticRoute) {
            srp.set_protocol("StaticRoute");
        }

        const BgpAttr *attr = path->GetAttr();
        if (attr->as_path() != NULL)
            srp.set_as_path(attr->as_path()->path().ToString());
        srp.set_local_preference(attr->local_pref());
        srp.set_next_hop(attr->nexthop().to_string());
        srp.set_label(path->GetLabel());
        srp.set_flags(path->GetFlags());
        srp.set_last_modified(integerToString(UTCUsecToPTime(path->time_stamp_usecs())));
        if (path->IsReplicated()) {
            const BgpSecondaryPath *replicated;
            replicated = static_cast<const BgpSecondaryPath *>(path);
            srp.set_replicated(true);
            srp.set_primary_table(replicated->src_table()->name());
        } else {
            srp.set_replicated(false);
        }
        if (attr->community()) {
            CommunitySpec comm;
            comm.communities = attr->community()->communities();
            srp.communities.push_back(comm.ToString());
        }
        if (attr->ext_community()) {
            ExtCommunitySpec ext_comm;
            const ExtCommunity::ExtCommunityList &v =
                attr->ext_community()->communities();
            for (ExtCommunity::ExtCommunityList::const_iterator it = v.begin();
                 it != v.end(); ++it) {
                if (ExtCommunity::is_route_target(*it)) {
                    RouteTarget rt(*it);
                    srp.communities.push_back(rt.ToString());
                } else if (ExtCommunity::is_mac_mobility(*it)) {
                    MacMobility mm(*it);
                    srp.communities.push_back(mm.ToString());
                    srp.set_sequence_no(mm.ToString());
                } else if (ExtCommunity::is_origin_vn(*it)) {
                    OriginVn origin_vn(*it);
                    srp.communities.push_back(origin_vn.ToString());
                    int vn_index = origin_vn.vn_index();
                    srp.set_origin_vn(
                                  ri_mgr->GetVirtualNetworkByVnIndex(vn_index));
                } else if (ExtCommunity::is_security_group(*it)) {
                    SecurityGroup sg(*it);
                    srp.communities.push_back(sg.ToString());
                } else if (ExtCommunity::is_tunnel_encap(*it)) {
                    TunnelEncap encap(*it);
                    srp.communities.push_back(encap.ToString());
                    TunnelEncapType::Encap id = encap.tunnel_encap();
                    srp.tunnel_encap.push_back(
                               TunnelEncapType::TunnelEncapToString(id));
                } else {
                    char temp[50];
                    int len = snprintf(temp, sizeof(temp), "ext community: ");

                    for (size_t i=0; i < it->size(); i++) {
                        len += snprintf(temp+len, sizeof(temp) - len, "%02x",
                                        (*it)[i]);
                    }
                    srp.communities.push_back(std::string(temp));
                }
            }
        }
        show_route_paths.push_back(srp);
    }
    show_route->set_paths(show_route_paths);
}
