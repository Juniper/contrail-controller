/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db/db_graph_edge.h"

#include "db/db_graph.h"

DBGraphEdge::DBGraphEdge(Edge edge_id)
  : edge_id_(edge_id) {
}

DBGraphVertex *DBGraphEdge::source(DBGraph *graph) {
    Vertex s = boost::source(edge_id_, *graph->graph());
    return graph->vertex_data(s);
}

const DBGraphVertex *DBGraphEdge::source(DBGraph *graph) const {
    Vertex s = boost::source(edge_id_, *graph->graph());
    return graph->vertex_data(s);
}

DBGraphVertex *DBGraphEdge::target(DBGraph *graph) {
    Vertex t = boost::target(edge_id_, *graph->graph());
    return graph->vertex_data(t);
}

const DBGraphVertex *DBGraphEdge::target(DBGraph *graph) const {
    Vertex t = boost::target(edge_id_, *graph->graph());
    return graph->vertex_data(t);
}

