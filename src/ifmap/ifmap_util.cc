/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_util.h"

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace std;

bool IFMapTypenameFilter::VertexFilter(const DBGraphVertex *vertex) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(vertex);
    if (exclude_vertex.find(node->table()->Typename()) != exclude_vertex.end()) {
        return false;
    } else {
        return true;
    }
}

bool IFMapTypenameFilter::EdgeFilter(const DBGraphVertex *source,
                                     const DBGraphVertex *target,
                                     const DBGraphEdge *edge) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(source);
    VertexEdgeMap::const_iterator it = exclude_edge.find(node->table()->Typename());
    if (it == exclude_edge.end()) return true;
    const IFMapLink *link = static_cast<const IFMapLink *>(edge);
    if (it->second.find(link->name()) != it->second.end()) {
        return false;
    } else {
        return true;
    }
}

DBGraph::VisitorFilter::AllowedEdgeRetVal IFMapTypenameFilter::AllowedEdges(
                                           const DBGraphVertex *source) const {
    return std::make_pair(true, DBGraph::VisitorFilter::AllowedEdgeSet());
}

// Return true if the node-type is in the white list
bool IFMapTypenameWhiteList::VertexFilter(const DBGraphVertex *vertex) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(vertex);
    if (include_vertex.find(node->table()->Typename()) != include_vertex.end()) {
        return true;
    } else {
        return false;
    }
}

DBGraph::VisitorFilter::AllowedEdgeRetVal IFMapTypenameWhiteList::AllowedEdges(
                                           const DBGraphVertex *source) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(source);
    VertexEdgeMap::const_iterator it = include_vertex.find(node->table()->Typename());
    assert(it != include_vertex.end());
    return std::make_pair(false, it->second);
}

bool IFMapTypenameWhiteList::EdgeFilter(const DBGraphVertex *source,
                                        const DBGraphVertex *target,
                                        const DBGraphEdge *edge) const {
    const IFMapNode *node = static_cast<const IFMapNode *>(source);
    VertexEdgeMap::const_iterator it = include_vertex.find(node->table()->Typename());
    if (it == include_vertex.end()) {
        IFMAP_WARN(IFMapIdentifierNotFound, "Cant find vertex",
                   node->table()->Typename());
        return false;
    }
    const IFMapLink *link = static_cast<const IFMapLink *>(edge);
    if (it->second.find(link->name()) != it->second.end()) {
        return true;
    } else {
        return false;
    }
}
