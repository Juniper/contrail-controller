/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_route.h"

#include "bgp/bgp_attr.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_table.h"

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
// Find path added by peer with given path id.  Skips secondary paths.
//
BgpPath *BgpRoute::FindPath(const IPeer *peer, uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {

        //
        // Skip secondary paths.
        //
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        if (path->GetPeer() == peer && path->GetPathId() == path_id) {
            return path;
        }
    }
    return NULL;
}

//
// Find path added by peer with given path id.  Skips secondary paths.
// Const version.
//
const BgpPath *BgpRoute::FindPath(const IPeer *peer, uint32_t path_id) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {

        //
        // Skip secondary paths.
        //
        if (dynamic_cast<const BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        if (path->GetPeer() == peer && path->GetPathId() == path_id) {
            return path;
        }
    }
    return NULL;
}

//
// Find path added by peer with given path id and path source.  
// Skips secondary paths.
//
BgpPath *BgpRoute::FindPath(const IPeer *peer, uint32_t path_id, 
                            BgpPath::PathSource src) {
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
// Find path added by peer with given path id and source.  
// Skips secondary paths.
// Const version.
//
const BgpPath *BgpRoute::FindPath(const IPeer *peer, uint32_t path_id, 
                                  BgpPath::PathSource src) const {
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); ++it) {

        // Skip secondary paths.
        if (dynamic_cast<const BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
        if (path->GetPeer() == peer && path->GetPathId() == path_id && 
            path->GetSource() == src) {
            return path;
        }
    }
    return NULL;
}

//
// Remove path added by peer with given path id.  Skips secondary paths.
// Return true if the path is found and removed, false otherwise.
//
bool BgpRoute::RemovePath(const IPeer *peer, uint32_t path_id) {
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
         BgpPath *path = static_cast<BgpPath *>(it.operator->());

        //
        // Skip secondary paths.
        //
        if (dynamic_cast<BgpSecondaryPath *>(it.operator->())) {
            continue;
        }

        if (path->GetPeer() == peer && path->GetPathId() == path_id) {
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
bool BgpRoute::RemovePath(const IPeer *peer, uint32_t path_id, 
                          BgpPath::PathSource src) {
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
        const IPeer *peer, uint32_t path_id, BgpPath::PathSource src) {
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
        const IPeer *peer, uint32_t path_id, BgpPath::PathSource src) {
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
