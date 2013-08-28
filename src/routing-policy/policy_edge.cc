/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "routing-policy/policy_edge.h"

#include "routing-policy/policy_graph.h"

PolicyGraphEdge::PolicyGraphEdge(Edge edge_id)
  : edge_id_(edge_id) {
}

PolicyGraphVertex *PolicyGraphEdge::source(PolicyGraph *graph) {
    Vertex s = boost::source(edge_id_, *graph->graph());
    return graph->vertex_data(s);
}

const PolicyGraphVertex *PolicyGraphEdge::source(PolicyGraph *graph) const {
    Vertex s = boost::source(edge_id_, *graph->graph());
    return graph->vertex_data(s);
}

PolicyGraphVertex *PolicyGraphEdge::target(PolicyGraph *graph) {
    Vertex t = boost::target(edge_id_, *graph->graph());
    return graph->vertex_data(t);
}

const PolicyGraphVertex *PolicyGraphEdge::target(PolicyGraph *graph) const {
    Vertex t = boost::target(edge_id_, *graph->graph());
    return graph->vertex_data(t);
}
