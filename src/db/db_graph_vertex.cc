/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_graph_vertex.h"

#include "db/db_graph.h"
#include "db/db_graph_edge.h"

using namespace boost;

DBGraphVertex::adjacency_iterator::adjacency_iterator()
    : graph_(NULL) {
}

DBGraphVertex::adjacency_iterator::adjacency_iterator(DBGraph *graph,
                                                      Vertex vertex)
    : graph_(graph) {
    boost::tie(iter_, end_) = adjacent_vertices(vertex, *graph_->graph());
}

DBGraphVertex &DBGraphVertex::adjacency_iterator::dereference() const {
    Vertex adj = *iter_;
    return *(graph_->vertex_data(adj));
}

DBGraphVertex::edge_iterator::edge_iterator()
    : graph_(NULL), vertex_(NULL) {
}

DBGraphVertex::edge_iterator::edge_iterator(DBGraph *graph,
                                            DBGraphVertex *vertex)
    : graph_(graph), vertex_(vertex) {
    boost::tie(iter_, end_) = out_edges(vertex->vertex(), *graph_->graph());
}

DBGraphEdge &DBGraphVertex::edge_iterator::dereference() const {
    Edge edge = *iter_;
    return *(graph_->edge_data(edge));
}

DBGraphVertex *DBGraphVertex::edge_iterator::target() const {
    Edge descriptor = *iter_;
    DBGraphEdge *edge = graph_->edge_data(descriptor);
    DBGraphVertex *v_target = edge->target(graph_);
    if (v_target == vertex_) {
        return edge->source(graph_);
    }
    return v_target;
}

bool DBGraphVertex::HasAdjacencies(DBGraph *graph) const {
    DBGraphBase::adjacency_iterator iter, end;
    boost::tie(iter, end) = adjacent_vertices(vertex_id_, *graph->graph());
    return (iter != end);
}
