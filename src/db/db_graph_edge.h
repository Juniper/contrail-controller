/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_DB_GRAPH_EDGE_H__
#define __DB_DB_GRAPH_EDGE_H__

#include "base/util.h"

#include "db/db_entry.h"
#include "db/db_graph_base.h"

class DBGraph;
class DBGraphVertex;

class DBGraphEdge : public DBEntry {
public:
    typedef DBGraphBase::vertex_descriptor Vertex;
    typedef DBGraphBase::edge_descriptor Edge;

    DBGraphEdge();

    void SetEdge(Edge edge);

    Edge edge_id() const {
        assert(!IsDeleted());
        // Don't access the edge id after deleting from graph
        return edge_id_;
    }

    DBGraphVertex *source(DBGraph *graph);
    const DBGraphVertex *source(DBGraph *graph) const;

    DBGraphVertex *target(DBGraph *graph);
    const DBGraphVertex *target(DBGraph *graph) const;

    virtual const std::string &name() const = 0;
private:
    Edge edge_id_;

    DISALLOW_COPY_AND_ASSIGN(DBGraphEdge);
};

#endif
