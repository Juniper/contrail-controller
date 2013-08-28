/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "routing-policy/policy_vertex.h"

#include "routing-policy/policy_edge.h"
#include "routing-policy/policy_graph.h"

using namespace boost;

PolicyGraphVertex::adjacency_iterator::adjacency_iterator()
    : graph_(NULL) {
}

PolicyGraphVertex::adjacency_iterator::adjacency_iterator(PolicyGraph *graph,
                                                      Vertex vertex)
    : graph_(graph) {
    boost::tie(iter_, end_) = adjacent_vertices(vertex, *graph_->graph());
}

PolicyGraphVertex &PolicyGraphVertex::adjacency_iterator::dereference() const {
    Vertex adj = *iter_;
    return *(graph_->vertex_data(adj));
}

PolicyGraphVertex::edge_iterator::edge_iterator()
    : graph_(NULL), vertex_(NULL) {
}

PolicyGraphVertex::edge_iterator::edge_iterator(PolicyGraph *graph,
                                            PolicyGraphVertex *vertex)
    : graph_(graph), vertex_(vertex) {
    boost::tie(iter_, end_) = out_edges(vertex->vertex(), *graph_->graph());
}

PolicyGraphEdge &PolicyGraphVertex::edge_iterator::dereference() const {
    Edge edge = *iter_;
    return *(graph_->edge_data(edge));
}

PolicyGraphVertex *PolicyGraphVertex::edge_iterator::target() const {
    Edge descriptor = *iter_;
    PolicyGraphEdge *edge = graph_->edge_data(descriptor);
    PolicyGraphVertex *v_target = edge->target(graph_);
    if (v_target == vertex_) {
        return edge->source(graph_);
    }
    return v_target;
}

PolicyGraphVertex *PolicyGraphVertex::parent_vertex(PolicyGraph *graph) {
    assert(boost::in_degree(vertex_id_, *graph->graph()) == 1);
    PolicyGraphBase::inv_adjacency_iterator parentIt, parentEnd;
    boost::tie(parentIt, parentEnd) 
        = boost::inv_adjacent_vertices(vertex_id_, *graph->graph());
    return graph->vertex_data(*parentIt);
}

PolicyGraphEdge  *PolicyGraphVertex::in_edge(PolicyGraph *graph) {
    assert(boost::in_degree(vertex_id_, *graph->graph()) == 1);
    PolicyGraphBase::in_edge_iterator inIt, inItEnd;
    boost::tie(inIt, inItEnd) = in_edges(vertex_id_, *graph->graph());
    return graph->edge_data(*inIt);
}
