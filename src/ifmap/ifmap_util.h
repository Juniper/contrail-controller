/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_IFMAP_UTIL_H__
#define __IFMAP_IFMAP_UTIL_H__

#include <string>
#include <vector>

#include "db/db_graph.h"

typedef std::map<std::string,
        DBGraph::VisitorFilter::AllowedEdgeSet> VertexEdgeMap;

struct IFMapTypenameFilter : public DBGraph::VisitorFilter {
    virtual bool VertexFilter(const DBGraphVertex *vertex) const;

    virtual bool EdgeFilter(const DBGraphVertex *source,
                            const DBGraphVertex *target,
                            const DBGraphEdge *edge) const;

    virtual AllowedEdgeRetVal AllowedEdges(const DBGraphVertex *source) const;

    std::set<std::string> exclude_vertex;
    VertexEdgeMap exclude_edge;
};

struct IFMapTypenameWhiteList : public DBGraph::VisitorFilter {
    virtual bool VertexFilter(const DBGraphVertex *vertex) const;

    virtual bool EdgeFilter(const DBGraphVertex *source,
                            const DBGraphVertex *target,
                            const DBGraphEdge *edge) const;

    virtual AllowedEdgeRetVal AllowedEdges(const DBGraphVertex *source) const;

    VertexEdgeMap include_vertex;
};

#endif
