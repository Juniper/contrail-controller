/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_IFMAP_UTIL_H__
#define __IFMAP_IFMAP_UTIL_H__

#include <string>
#include <vector>

#include "db/db_graph.h"

struct IFMapTypenameFilter : public DBGraph::VisitorFilter {
    virtual bool VertexFilter(const DBGraphVertex *vertex) const;
 
    virtual bool EdgeFilter(const DBGraphVertex *source,
                            const DBGraphVertex *target,
                            const DBGraphEdge *edge) const;

    std::vector<std::string> exclude_vertex;
    std::vector<std::string> exclude_edge;
};

struct IFMapTypenameWhiteList : public DBGraph::VisitorFilter {
    virtual bool VertexFilter(const DBGraphVertex *vertex) const;
 
    virtual bool EdgeFilter(const DBGraphVertex *source,
                            const DBGraphVertex *target,
                            const DBGraphEdge *edge) const;

    std::vector<std::string> include_vertex;
    std::vector<std::string> include_edge;
};

#endif
